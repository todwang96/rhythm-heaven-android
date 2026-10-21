/* 苹果 API 的非内联实现：段表符号、clonefile、renamex_np。
 * 都是“语义正确但退化实现”，代码里已经注明代价。 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mach-o/getsect.h"
#include "sys/clonefile.h"

/* hle.c 取的是 &_mh_execute_header，只作为 getsectiondata 的哑参数使用。 */
const struct mach_header _mh_execute_header = {0, 0, 0, 0, 0, 0, 0, 0};

static int copy_file(const char *src, const char *dst) {
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) return -1;
    struct stat st;
    if (fstat(in, &st)) { close(in); return -1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, st.st_mode & 07777);
    if (out < 0) { close(in); return -1; }
    char buffer[256 * 1024];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buffer, sizeof buffer)) > 0) {
        char *p = buffer;
        while (n > 0) {
            ssize_t w = write(out, p, (size_t)n);
            if (w <= 0) { rc = -1; goto done; }
            p += w; n -= w;
        }
    }
    if (n < 0) rc = -1;
done:
    close(in);
    close(out);
    return rc;
}

static int copy_tree(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st)) return -1;
    if (S_ISREG(st.st_mode)) return copy_file(src, dst);
    if (!S_ISDIR(st.st_mode)) return -1;
    if (mkdir(dst, st.st_mode & 07777) && errno != EEXIST) return -1;
    DIR *dir = opendir(src);
    if (!dir) return -1;
    int rc = 0;
    struct dirent *entry;
    char from[4096], to[4096];
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        snprintf(from, sizeof from, "%s/%s", src, entry->d_name);
        snprintf(to, sizeof to, "%s/%s", dst, entry->d_name);
        if (copy_tree(from, to)) { rc = -1; break; }
    }
    closedir(dir);
    return rc;
}

/* 语义等价于 APFS 的写时复制克隆，只是真的复制。目录树不能已存在。 */
int clonefile(const char *src, const char *dst, int flags) {
    (void)flags;
    if (!src || !dst) { errno = EINVAL; return -1; }
    return copy_tree(src, dst);
}

/* RENAME_SWAP：先挪到临时名再交换，最后清掉原 from。
 * 不是原子操作，但进程内其它线程看不到中间态（调用方持锁）。 */
int renamex_np(const char *from, const char *to, unsigned int flags) {
    if (!(flags & RENAME_SWAP)) return rename(from, to);
    char spare[4096];
    snprintf(spare, sizeof spare, "%s.swap%ld", to, (long)getpid());
    unlink(spare);
    if (rename(to, spare)) return -1;
    if (rename(from, to)) { rename(spare, to); return -1; }
    if (rename(spare, from)) return -1;
    return 0;
}
