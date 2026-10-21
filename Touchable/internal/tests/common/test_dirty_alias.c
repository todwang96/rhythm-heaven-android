/* Build with nvn_dirty.c, -DNITENPHONE_SWITCH_RUNTIME, and -pthread. */
#include "nvn_dirty.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static void (*prepare_write)(void *, size_t);
void dm_guest_write_prep_register(void (*callback)(void *, size_t)) { prepare_write = callback; }
void dm_fault_hook_register(int (*callback)(void *, int, void *)) { (void)callback; }

static int failures;
static void check(const char *name, int expected, int actual) {
    printf("%s: expected %d, got %d\n", name, expected, actual);
    failures += expected != actual;
}

int main(void) {
    size_t page = (size_t)getpagesize();
    assert(page > 4096);
    uint8_t *memory = mmap(NULL, page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(memory != MAP_FAILED);
    uint8_t *small = memory, *large = memory + 4096;
    size_t large_size = page * 2;
    nvn_dirty_add_range(small, 4096);
    nvn_dirty_add_range(large, large_size);
    check("Small pool shares the first host page", 1, nvn_dirty_tracked(small, 4096));
    check("The whole large pool is tracked", 1, nvn_dirty_tracked(large, large_size));
    check("A wrapped span is not tracked", 0, nvn_dirty_tracked(large, SIZE_MAX));
    uint64_t stamp = nvn_dirty_now();
    check("Unchanged large texture stays clean", 0, nvn_dirty_since(large, large_size, stamp));
    nvn_dirty_protect(large, large_size);
    prepare_write(large + page, 1);
    large[page] = 0x5a;
    check("A real large-pool write is dirty", 1, nvn_dirty_since(large, large_size, stamp));
    nvn_dirty_remove_range(large);
    check("Removing the large pool preserves the small pool", 1, nvn_dirty_tracked(small, 4096));
    check("Removed large storage is no longer tracked", 0, nvn_dirty_tracked(large, large_size));
    nvn_dirty_remove_range(small);
    check("Removing the small pool releases its tracking", 0, nvn_dirty_tracked(small, 4096));

    nvn_dirty_add_range(large, large_size);
    nvn_dirty_add_range(small, 4096);
    nvn_dirty_remove_range(small);
    check("Removing a later small pool preserves the earlier large pool", 1,
          nvn_dirty_tracked(large, large_size));
    stamp = nvn_dirty_now();
    nvn_dirty_protect(large, large_size);
    prepare_write(large + page, 1);
    large[page] = 0xa5;
    check("The surviving large pool still tracks writes", 1, nvn_dirty_since(large, large_size, stamp));
    nvn_dirty_remove_range(large);
    check("Both pools have been removed", 0, nvn_dirty_tracked(large, large_size));
    munmap(memory, page * 4);
    return failures != 0;
}
