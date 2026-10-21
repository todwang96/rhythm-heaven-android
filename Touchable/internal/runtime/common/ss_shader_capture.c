#include "dm.h"
#include <CommonCrypto/CommonDigest.h>
#include <mach/mach.h>
#include <sys/stat.h>

static pthread_mutex_t capture_lock = PTHREAD_MUTEX_INITIALIZER;
static void read_guest(void *out, uintptr_t address, size_t size) {
    vm_size_t got = 0;
    if (vm_read_overwrite(mach_task_self(), address, size, (vm_address_t)out, &got) || got != size)
        die("Shader capture cannot read %#lx +%#zx", address, size);
}
static void write_bytes(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(data, 1, size, file) != size || fclose(file)) die("Shader capture write failed: %s", path);
}
static void key_text(const uint8_t hash[32], char key[17]) {
    for (int i = 0; i < 8; ++i) snprintf(key + i * 2, 3, "%02x", hash[i]);
}
/* Unity gives each program its own control block. Some nn::gfx games share one
 * control block between different code blobs, so those games also hash the code. */
static void shader_key(const uint8_t *control, const uint8_t *code, uint32_t code_size, char key[17]) {
    uint8_t hash[32];
#ifdef SS_SHADER_KEY_CODE
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    CC_SHA256_Update(&context, control, 0x880);
    CC_SHA256_Update(&context, code, code_size);
    CC_SHA256_Final(hash, &context);
#else
    (void)code; (void)code_size;
    CC_SHA256(control, 0x880, hash);
#endif
    key_text(hash, key);
}
static uint32_t code_size_of(const uint8_t *control) {
    uint32_t c1_offset, code_size;
    memcpy(&c1_offset, control + 0x700, 4); memcpy(&code_size, control + 0x704, 4);
    if (memcmp(control, "\x34\x12\x76\x98", 4) || !code_size || code_size > 4 * 1024 * 1024 || c1_offset > code_size)
        die("Unexpected shader control format: offset=%#x size=%#x", c1_offset, code_size);
    return code_size;
}
/* The Metal backend resolves programs with the same key as the capture files. */
void ss_shader_key(const void *control, uint64_t address, char key[17]) {
    uint32_t code_size = code_size_of(control);
    shader_key(control, (const uint8_t *)(uintptr_t)address, code_size, key);
}
/* Hash only translator inputs. NVN may change unrelated control metadata.
 * Keep instruction bytes, constant bytes, stage, boundaries, and workgroup size. */
void ss_shader_equivalent_key(const void *control, uint64_t address, char key[65]) {
    const uint8_t *bytes = control;
    uint32_t end = code_size_of(bytes), constant, start, stage;
    memcpy(&constant, bytes + 0x700, 4); memcpy(&start, bytes + 0x708, 4);
    memcpy(&stage, bytes + 0x714, 4);
    if (start >= constant || constant > end || (stage != 0 && stage != 1 && stage != 5))
        die("Invalid shader translation bounds");
    uint32_t header[6] = {stage, constant - start, end - constant, 0, 0, 0};
    if (stage == 5) memcpy(header + 3, bytes + 0x748, 12);
    uint8_t hash[32]; CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    CC_SHA256_Update(&context, header, sizeof(header));
    CC_SHA256_Update(&context, (const uint8_t *)(uintptr_t)address + start, end - start);
    CC_SHA256_Final(hash, &context);
    for (int i = 0; i < 32; ++i) snprintf(key + 2*i, 3, "%02x", hash[i]);
}
void ss_capture_shader(const void *control, uint64_t address) {
    const char *directory = getenv("SS_SHADER_CAPTURE");
    if (!directory) return;
    uint8_t bytes[0x880]; char key[17], path[1200];
    read_guest(bytes, (uintptr_t)control, sizeof(bytes));
    uint32_t code_size = code_size_of(bytes);
    uint8_t *code = malloc(code_size);
    if (!code) die("Cannot allocate shader capture buffer");
    read_guest(code, address, code_size);
    shader_key(bytes, code, code_size, key);
    pthread_mutex_lock(&capture_lock);
    snprintf(path, sizeof(path), "%s/%s.control", directory, key);
    struct stat status;
    if (stat(path, &status)) {
        char code_path[1200]; snprintf(code_path, sizeof(code_path), "%s/%s.code", directory, key);
        write_bytes(code_path, code, code_size);
        write_bytes(path, bytes, sizeof(bytes));
        LOG("SHADER captured %s size=%#x", key, code_size);
    }
    pthread_mutex_unlock(&capture_lock);
    free(code);
}
