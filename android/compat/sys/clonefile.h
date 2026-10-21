/* sys/clonefile.h → Android 上没有 APFS 写时复制。
 *
 * 调用点（hle_fs.c）用它做“挂载时把已提交目录整树克隆到工作目录”和“提交时原子换出”。
 * 这里退化成逐文件递归复制 + rename：语义正确，代价从 O(1) 变成 O(体积)。
 * 存档只有几十 MB，可以接受；如果以后要快，可以在有 FICLONE 的文件系统上换成 ioctl。
 */
#pragma once
#include <stddef.h>

#define CLONE_NOFOLLOW 0x0001
#define CLONE_NOOWNERCOPY 0x0002

#define RENAME_SWAP 0x0002
#define RENAME_EXCL 0x0004

int clonefile(const char *src, const char *dst, int flags);
int renamex_np(const char *from, const char *to, unsigned int flags);
