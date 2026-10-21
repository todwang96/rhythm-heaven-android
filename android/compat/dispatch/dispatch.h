/* dispatch/dispatch.h → pthread 版最小子集。
 * 运行时只用到信号量（音频回调线程与游戏线程之间的唤醒）。
 * App.m 里的 dispatch_get_main_queue / dispatch_async 属于 iOS 平台层，不在这个 shim 范围内。 */
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#define DISPATCH_TIME_NOW 0ull
#define DISPATCH_TIME_FOREVER (~0ull)

typedef struct dispatch_semaphore {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    long count;
} *dispatch_semaphore_t;

static inline dispatch_semaphore_t dispatch_semaphore_create(long value) {
    dispatch_semaphore_t s = (dispatch_semaphore_t)__builtin_calloc(1, sizeof(struct dispatch_semaphore));
    if (!s) return 0;
    pthread_mutex_init(&s->lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&s->cond, &attr);
    pthread_condattr_destroy(&attr);
    s->count = value;
    return s;
}

static inline long dispatch_semaphore_signal(dispatch_semaphore_t s) {
    pthread_mutex_lock(&s->lock);
    long before = s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
    return before;
}

/* 返回 0 表示拿到，非 0 表示超时。 */
static inline long dispatch_semaphore_wait(dispatch_semaphore_t s, uint64_t timeout_ns) {
    pthread_mutex_lock(&s->lock);
    long rc = 0;
    if (timeout_ns == DISPATCH_TIME_FOREVER) {
        while (s->count <= 0) pthread_cond_wait(&s->cond, &s->lock);
    } else if (timeout_ns == DISPATCH_TIME_NOW) {
        if (s->count <= 0) rc = 1;
    } else {
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += (time_t)(timeout_ns / 1000000000ull);
        deadline.tv_nsec += (long)(timeout_ns % 1000000000ull);
        if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec += 1; deadline.tv_nsec -= 1000000000L; }
        while (s->count <= 0 && rc == 0) rc = pthread_cond_timedwait(&s->cond, &s->lock, &deadline);
    }
    if (rc == 0) s->count--;
    pthread_mutex_unlock(&s->lock);
    return rc;
}

/* dispatch_time 在 Apple 上是 nanosecond 计数，直接透传由调用方相加。 */
static inline uint64_t dispatch_time(uint64_t base, int64_t delta) { return base + (uint64_t)delta; }

/* GCD 对象在 ARC 下列，这里没有引用计数可管。 */
static inline void dispatch_release(dispatch_semaphore_t object) { (void)object; }
static inline void dispatch_retain(dispatch_semaphore_t object) { (void)object; }

#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000ull
#endif
#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC 1000000ull
#endif
#ifndef NSEC_PER_USEC
#define NSEC_PER_USEC 1000ull
#endif
#ifndef USEC_PER_SEC
#define USEC_PER_SEC 1000000ull
#endif
