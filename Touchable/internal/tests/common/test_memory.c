#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define RESERVE (5ull << 30)
typedef struct { uint8_t *base; size_t size, ro_off, data_off, patch_off; } module_t;
static module_t g_mod[1]; static int g_nmod;
static uint8_t *g_alias, *g_as;
static size_t g_alias_cap, g_heap_cur, test_limit = 0x100000;
static size_t image_bytes(void) { return 0; }
static size_t app_pool(void) { return test_limit; }
static void dm_guest_write_prepare(void *memory, size_t size) { (void)memory; (void)size; }
static _Noreturn void die(const char *format, ...) {
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args); abort();
}
#include "../../runtime/common/ss_memory.inc"

int main(void) {
    g_alias_cap = 0x10000;
    g_as = g_alias = mmap(NULL, g_alias_cap, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(g_alias != MAP_FAILED);
    uintptr_t start = (uintptr_t)g_alias;
    assert(ss_physical_memory(start + 0x1000, 0x9000, 1) == 0);
    assert(ss_physical_used() == 0x9000);
    memset(g_alias + 0x1000, 0x5a, 0x9000);
    uint64_t base, size, state, permission;
    ss_alias_query(start + 0x2000, &base, &size, &state, &permission);
    assert(base == start + 0x1000 && size == 0x9000 && state == 5 && permission == 3);
    assert(ss_physical_memory(start + 0x2000, 0x1000, 0) == 0);
    assert(g_alias[0x1000] == 0x5a && g_alias[0x3000] == 0x5a);
    assert(ss_physical_used() == 0x8000);
    assert(ss_physical_memory(start + 0x2000, 0x1000, 1) == 0);
    for (int i = 0x2000; i < 0x3000; ++i) assert(g_alias[i] == 0);
    assert(g_alias[0x1000] == 0x5a && g_alias[0x3000] == 0x5a);
    assert(ss_physical_memory(start + 0x1000, 0x9000, 1) == 0);
    assert(ss_physical_used() == 0x9000 && g_alias[0x1000] == 0x5a);
    assert(ss_physical_memory(start + 1, 0x1000, 1) != 0);
    assert(ss_physical_memory(start - 0x1000, 0x1000, 1) != 0);
    assert(ss_physical_memory(start + g_alias_cap, 0x1000, 1) != 0);
    test_limit = 0x9000;
    assert(ss_physical_memory(start + 0xa000, 0x1000, 1) == 0x10801);
    assert(ss_physical_memory(start + 0x1000, 0x9000, 0) == 0);
    assert(ss_physical_used() == 0);
    assert(ss_physical_memory(start, 0x4000, 1) == 0);
    for (int i = 0; i < 0x4000; ++i) assert(g_alias[i] == 0);
    munmap(g_alias, g_alias_cap);
    puts("PASS: physical memory page sharing, remap, bounds, and accounting");
}
