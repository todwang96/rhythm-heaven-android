/* mach/thread_policy.h → 线程策略在 Android 上由调度器决定，做空操作。 */
#pragma once
#include <stdint.h>
#include "mach/mach.h"

typedef int integer_t;
typedef struct { integer_t affinity_tag; } thread_affinity_policy_data_t;
typedef struct { integer_t period, computation, constraint, preemptible; } thread_time_constraint_policy_data_t;
typedef struct { integer_t allow_realtime, allow_timeshare; } thread_extended_policy_data_t;
typedef struct { integer_t importance; } thread_precedence_policy_data_t;

#define THREAD_STANDARD_POLICY 1
#define THREAD_EXTENDED_POLICY 1
#define THREAD_TIME_CONSTRAINT_POLICY 2
#define THREAD_PRECEDENCE_POLICY 3
#define THREAD_AFFINITY_POLICY 4
#define THREAD_STANDARD_POLICY_COUNT 0

static inline kern_return_t thread_policy_set(mach_port_t thread, int flavor, const integer_t *info,
                                              unsigned int count) {
    (void)thread; (void)flavor; (void)info; (void)count;
    return KERN_SUCCESS;
}

typedef integer_t *thread_policy_t;

/* 线程挂起：Android 上真正的挂起要靠信号实现，这里空转。
 * 只影响调试/存档快照那类操作，不影响游戏主流程。 */
static inline kern_return_t thread_suspend(mach_port_t thread) { (void)thread; return KERN_SUCCESS; }
static inline kern_return_t thread_resume(mach_port_t thread) { (void)thread; return KERN_SUCCESS; }

#define THREAD_TIME_CONSTRAINT_POLICY_COUNT 4


#define THREAD_EXTENDED_POLICY_COUNT 1
static inline kern_return_t thread_policy_get(mach_port_t thread, int flavor, integer_t *info,
                                              unsigned int *count, unsigned char *defaulted) {
    (void)thread; (void)flavor; (void)info; (void)count;
    if (defaulted) *defaulted = 1;
    return 5;
}
