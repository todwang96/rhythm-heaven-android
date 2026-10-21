/* Build from the repository root. Link the actual runtime implementation. */
#include "dm.h"
#include <assert.h>
#include <string.h>
struct dm_flags g_flag;
uint64_t g_t0_ns;
void ss_shader_equivalent_key(const void *, uint64_t, char[65]);
static void put(uint8_t *p, uint32_t value) { memcpy(p, &value, 4); }
int main(void) {
    uint8_t control[0x880] = {0}, code[0x100];
    memcpy(control, "\x34\x12\x76\x98", 4);
    put(control+0x700, 0x80); put(control+0x704, 0x100);
    put(control+0x708, 0x20); put(control+0x714, 1);
    for (int i=0; i<0x100; ++i) code[i] = (uint8_t)(i*7);
    char key[65], changed[65];
    ss_shader_equivalent_key(control, (uintptr_t)code, key);
    puts(key);
    control[0x7d0] ^= 1;
    ss_shader_equivalent_key(control, (uintptr_t)code, changed);
    assert(!strcmp(key, changed));
    code[0x30] ^= 1;
    ss_shader_equivalent_key(control, (uintptr_t)code, changed);
    assert(strcmp(key, changed));
    code[0x30] ^= 1; code[0xf0] ^= 1;
    ss_shader_equivalent_key(control, (uintptr_t)code, changed);
    assert(strcmp(key, changed));
    return 0;
}
