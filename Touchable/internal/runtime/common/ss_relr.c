/* ELF64 RELR: an even word gives an address; an odd word gives 63 bits.
 * Format: https://llvm.org/doxygen/Object_2ELF_8cpp_source.html
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int ss_apply_relr(uint8_t *image, size_t size, size_t writable_start,
                  uint64_t table_offset, uint64_t table_size, size_t *applied) {
    if (!image || !applied || table_offset > size || table_size > size - table_offset ||
        table_size % 8 || table_offset % 8) return -1;
    /* First validate the full table. Do not leave a half-relocated image. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        uint64_t cursor = 0;
        size_t count = 0;
        int have_cursor = 0;
        for (uint64_t position = table_offset; position < table_offset + table_size; position += 8) {
            uint64_t entry;
            memcpy(&entry, image + position, 8);
            if (!(entry & 1)) {
                if ((entry & 7) || entry < writable_start || entry > size || size - entry < 8)
                    return -1;
                if (entry < table_offset + table_size && entry + 8 > table_offset) return -1;
                if (pass) *(uint64_t *)(image + entry) += (uintptr_t)image;
                ++count;
                cursor = entry + 8;
                have_cursor = 1;
            } else {
                if (!have_cursor || cursor > UINT64_MAX - 504) return -1;
                for (unsigned bit = 1; bit < 64; ++bit) {
                    if (!((entry >> bit) & 1)) continue;
                    uint64_t target = cursor + (bit - 1) * 8;
                    if (target < writable_start || target > size || size - target < 8) return -1;
                    if (target < table_offset + table_size && target + 8 > table_offset) return -1;
                    if (pass) *(uint64_t *)(image + target) += (uintptr_t)image;
                    ++count;
                }
                cursor += 504;
            }
        }
        *applied = count;
    }
    return 0;
}
