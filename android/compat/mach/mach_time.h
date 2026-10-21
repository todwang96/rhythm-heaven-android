/* mach/mach_time.h → CLOCK_MONOTONIC。timebase 做成 1:1（纳秒），
 * 调用方按 numer/denom 换算后直接得到纳秒，与原语义一致。 */
#pragma once
#include <stdint.h>
#include <time.h>
#include <errno.h>

typedef struct mach_timebase_info { uint32_t numer; uint32_t denom; } mach_timebase_info_data_t;

static inline uint64_t mach_absolute_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline uint64_t mach_continuous_time(void) { return mach_absolute_time(); }

static inline int mach_timebase_info(mach_timebase_info_data_t *info) {
    if (info) { info->numer = 1; info->denom = 1; }
    return 0;
}

/* deadline 是纳秒绝对时刻（timebase 1:1）。到点前用 clock_nanosleep 睡。
 * 返回值 0 表示已到点；-1 表示被信号打断。 */
static inline int mach_wait_until(uint64_t deadline) {
    struct timespec ts;
    ts.tv_sec = (time_t)(deadline / 1000000000ull);
    ts.tv_nsec = (long)(deadline % 1000000000ull);
    int rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    return rc == 0 ? 0 : -1;
}
