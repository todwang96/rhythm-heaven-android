/* mach-o/getsect.h → Android 上没有 Mach-O 段表。
 * hle.c 用它找主镜像里的 __DATA,swhle 段（宿主 HLE 表）。移植后 HLE 表由编译期生成，
 * 不需要从段里读，所以这里返回 NULL，调用方会走它自己的兜底分支。 */
#pragma once
#include <stdint.h>

struct mach_header { uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved; };

/* hle.c 里取的是 &_mh_execute_header；给一个地址稳定的哑对象。 */
extern const struct mach_header _mh_execute_header;

static inline const uint8_t *getsectiondata(const struct mach_header *header, const char *segment,
                                            const char *section, unsigned long *size) {
    (void)header; (void)segment; (void)section;
    if (size) *size = 0;
    return 0;
}

static inline const uint8_t *getsectdata(const char *segment, const char *section, unsigned long *size) {
    (void)segment; (void)section;
    if (size) *size = 0;
    return 0;
}
