/* sys/xattr.h → 用 include_next 拿到 Android 的真头，再补 Darwin 的差异：
 *   1) ENOATTR 在 Linux 上叫 ENODATA；XATTR_NOFOLLOW 对应「用 l* 系列」，即 0。
 *   2) Darwin 的 getxattr/listxattr 多两个尾参（position / options）。这里给出同签名的封装，
 *      内部丢掉它们——调用方传的 position 恒为 0、options 只用到 NOFOLLOW，语义一致。 */
#pragma once
#include_next <sys/xattr.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef ENOATTR
#define ENOATTR ENODATA
#endif
#ifndef XATTR_NOFOLLOW
#define XATTR_NOFOLLOW 0
#endif
#ifndef XATTR_CREATE
#define XATTR_CREATE 0x1
#endif
#ifndef XATTR_REPLACE
#define XATTR_REPLACE 0x2
#endif

/* 先定义封装（此时 getxattr 还是真函数），再用宏把调用点导过来。 */
static inline ssize_t darwin_getxattr(const char *path, const char *name, void *value, size_t size,
                                      unsigned int position, int options) {
    (void)position; (void)options;
    return getxattr(path, name, value, size);
}
static inline ssize_t darwin_listxattr(const char *path, char *list, size_t size, int options) {
    (void)options;
    return listxattr(path, list, size);
}
static inline int darwin_setxattr(const char *path, const char *name, const void *value, size_t size,
                                  unsigned int position, int options) {
    (void)position; (void)options;
    return setxattr(path, name, value, size, 0);
}
static inline int darwin_removexattr(const char *path, const char *name, int options) {
    (void)options;
    return removexattr(path, name);
}

#define getxattr(path, name, value, size, position, options) \
    darwin_getxattr((path), (name), (value), (size), (position), (options))
#define listxattr(path, list, size, options) darwin_listxattr((path), (list), (size), (options))
#define setxattr(path, name, value, size, position, options) \
    darwin_setxattr((path), (name), (value), (size), (position), (options))
#define removexattr(path, name, options) darwin_removexattr((path), (name), (options))
