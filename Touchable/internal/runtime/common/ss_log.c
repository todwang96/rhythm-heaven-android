#include "hle.h"
#include <unistd.h>

static int standard_stream(const void *stream) {
    const char *names[] = {"stdout", "stderr"};
    for (int i = 0; i < 2; ++i) {
        void **symbol = guest_sym(names[i]);
        if (symbol && *symbol == stream) return 1;
    }
    return 0;
}
HLE_FORCE("write", "Forward guest console writes", int64_t, ss_write, int fd, const void *data, size_t size) {
    if (fd == 1 || fd == 2) return write(STDERR_FILENO, data, size);
    return ((int64_t (*)(int, const void *, size_t))guest_sym("write"))(fd, data, size);
}
HLE_FORCE("fwrite", "Forward guest console stream data", size_t, ss_fwrite, const void *data, size_t size, size_t count, void *stream) {
    if (standard_stream(stream)) return fwrite(data, size, count, stderr);
    return ((size_t (*)(const void *, size_t, size_t, void *))guest_sym("fwrite"))(data, size, count, stream);
}
HLE_FORCE("fputs", "Forward guest console text", int, ss_fputs, const char *text, void *stream) {
    if (standard_stream(stream)) return fputs(text, stderr);
    return ((int (*)(const char *, void *))guest_sym("fputs"))(text, stream);
}
HLE_FORCE("puts", "Forward guest console line", int, ss_puts, const char *text) {
    int result = fputs(text, stderr);
    if (result >= 0) result = fputc('\n', stderr);
    return result;
}
HLE_FORCE("vfprintf", "Format guest console text with the guest ABI", int, ss_vfprintf, void *stream, const char *format, const void *arguments) {
    if (!standard_stream(stream))
        return ((int (*)(void *, const char *, const void *))guest_sym("vfprintf"))(stream, format, arguments);
    uint64_t copy[4]; memcpy(copy, arguments, sizeof(copy));
    char *text = malloc(16384);
    if (!text) return -1;
    int result = ((int (*)(char *, size_t, const char *, const void *))guest_sym("vsnprintf"))(text, 16384, format, copy);
    if (result > 0) fwrite(text, 1, (size_t)result < 16384 ? (size_t)result : 16383, stderr);
    free(text);
    return result;
}

HLE_FORCE("_ZN2nn4diag6detail18CallAllLogObserverERKNS0_11LogMetaDataERKNS0_7LogBodyE",
    "Forward SDK diagnostic log bodies", void, ss_diag_log, const void *metadata, const void *body) {
    (void)metadata;
    struct { const char *text; uint32_t size; uint32_t pad; uint8_t head, tail; } message;
    memcpy(&message, body, sizeof(message));
    if (message.text && message.size <= 1 << 20) {
        if (message.head) fputs("[guest] ", stderr);
        fwrite(message.text, 1, message.size, stderr);
        if (message.tail && (!message.size || message.text[message.size - 1] != '\n')) fputc('\n', stderr);
    }
    ((void (*)(const void *, const void *))guest_sym(
        "_ZN2nn4diag6detail18CallAllLogObserverERKNS0_11LogMetaDataERKNS0_7LogBodyE"))(metadata, body);
}
