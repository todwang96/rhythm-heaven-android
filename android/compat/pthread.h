/* pthread 的 Darwin 扩展：取当前线程的栈地址/大小。
 * Linux 上用 pthread_getattr_np 拿到同样的信息。
 * 注意先 include_next 拿到真正的 <pthread.h>，本文件只做“追加”。 */
#pragma once
#include_next <pthread.h>
#include <stddef.h>

static inline void *pthread_get_stackaddr_np(pthread_t thread) {
    pthread_attr_t attr;
    void *address = 0;
    size_t size = 0;
    if (pthread_getattr_np(thread, &attr) == 0) {
        pthread_attr_getstack(&attr, &address, &size);
        pthread_attr_destroy(&attr);
    }
    return address ? (void *)((char *)address + size) : 0;   /* Darwin 返回栈顶 */
}

static inline size_t pthread_get_stacksize_np(pthread_t thread) {
    pthread_attr_t attr;
    void *address = 0;
    size_t size = 0;
    if (pthread_getattr_np(thread, &attr) == 0) {
        pthread_attr_getstack(&attr, &address, &size);
        pthread_attr_destroy(&attr);
    }
    return size;
}

/* Darwin 的 pthread_setname_np 只收名字（作用于当前线程）；Linux 要两个参数。
 * 树里两种写法都有（上游 dm/ 是 Darwin 单参，vickwv 的 Android 代码是原生双参），
 * 所以按实参个数分派，不要一刀切成单参。
 * 内部的 pthread_setname_np 来自 DM__SETNAME_1 的替换列表，展开时已被涂蓝，不会递归。 */
static inline int darwin_setname_np(const char *name) {
    return pthread_setname_np(pthread_self(), name);
}
#define DM__SETNAME_1(name)          darwin_setname_np((name))
#define DM__SETNAME_2(thread, name)  pthread_setname_np((thread), (name))
#define DM__SETNAME_PICK(_1, _2, NAME, ...) NAME
#define pthread_setname_np(...) \
    DM__SETNAME_PICK(__VA_ARGS__, DM__SETNAME_2, DM__SETNAME_1)(__VA_ARGS__)
