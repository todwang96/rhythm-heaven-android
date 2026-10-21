/* 强制预包含：给那些没 include <TargetConditionals.h> 却用 TARGET_OS_* 的文件兜底，
 * 外加两个 Darwin 的便利函数。由 -include 注入，不改任何源文件。 */
#pragma once
#include <stdint.h>
#include <time.h>

#ifndef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 0
#endif
#ifndef TARGET_OS_MAC
#define TARGET_OS_MAC 0
#endif
#ifndef TARGET_OS_OSX
#define TARGET_OS_OSX 0
#endif
#ifndef TARGET_OS_IOS
#define TARGET_OS_IOS 0
#endif
#ifndef TARGET_OS_SIMULATOR
#define TARGET_OS_SIMULATOR 0
#endif
#ifndef TARGET_OS_TV
#define TARGET_OS_TV 0
#endif
#ifndef TARGET_OS_ANDROID
#define TARGET_OS_ANDROID 1
#endif
#ifndef TARGET_CPU_ARM64
#define TARGET_CPU_ARM64 1
#endif

/* Darwin: clock_gettime_nsec_np(CLOCK_UPTIME_RAW) → 单调纳秒 */
#ifndef CLOCK_UPTIME_RAW
#define CLOCK_UPTIME_RAW 8
#endif
#ifndef CLOCK_MONOTONIC_RAW_APPROX
#define CLOCK_MONOTONIC_RAW_APPROX 9
#endif

static inline uint64_t clock_gettime_nsec_np(clockid_t clock_id) {
    struct timespec ts;
    clockid_t id = (clock_id == CLOCK_UPTIME_RAW || clock_id == CLOCK_MONOTONIC_RAW_APPROX)
                   ? CLOCK_MONOTONIC : CLOCK_REALTIME;
    clock_gettime(id, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Darwin 的 mach 扩展：让没显式 include 它们的文件也能拿到类型与常量。 */
#include "mach/mach.h"
#include "mach/thread_policy.h"
#include "mach/arm/thread_status.h"
#include "pthread/qos.h"

typedef integer_t *thread_info_t;
