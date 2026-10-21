/* mach/arm/thread_status.h → 结构体与访问函数按 Darwin 命名，便于直接复用原代码。
 *
 * 注意：Android 上 thread_get_state 恒失败（见下），所以这里取到的寄存器没有实际用途；
 * 保留结构只是为了让 hle_thread.c 的调试快照路径编得过。
 * 真正的崩溃现场寄存器在 ss_diagnostics.c 里走 mcontext。 */
#pragma once
#include <stdint.h>
#include "mach/mach.h"

#define ARM_THREAD_STATE64 6
#define ARM_THREAD_STATE 1
#define ARM_THREAD_STATE64_COUNT 68
#define THREAD_STATE_NONE 13

typedef struct arm_thread_state64 {
    uint64_t __x[29];
    uint64_t __fp;
    uint64_t __lr;
    uint64_t __sp;
    uint64_t __pc;
    uint32_t __cpsr;
    uint32_t __pad;
} arm_thread_state64_t;

static inline uint64_t arm_thread_state64_get_pc(arm_thread_state64_t s) { return s.__pc; }
static inline uint64_t arm_thread_state64_get_lr(arm_thread_state64_t s) { return s.__lr; }
static inline uint64_t arm_thread_state64_get_sp(arm_thread_state64_t s) { return s.__sp; }
static inline uint64_t arm_thread_state64_get_fp(arm_thread_state64_t s) { return s.__fp; }
static inline void arm_thread_state64_set_pc_fptr(arm_thread_state64_t *s, void *pc) {
    s->__pc = (uint64_t)(uintptr_t)pc;
}

/* Darwin 的双下划线版本（hle_thread.c 用的就是这套）。 */
typedef arm_thread_state64_t __darwin_arm_thread_state64_t;
static inline uint64_t __darwin_arm_thread_state64_get_pc(arm_thread_state64_t s) { return s.__pc; }
static inline uint64_t __darwin_arm_thread_state64_get_lr(arm_thread_state64_t s) { return s.__lr; }
static inline uint64_t __darwin_arm_thread_state64_get_sp(arm_thread_state64_t s) { return s.__sp; }
static inline uint64_t __darwin_arm_thread_state64_get_fp(arm_thread_state64_t s) { return s.__fp; }
static inline void __darwin_arm_thread_state64_set_pc_fptr(arm_thread_state64_t *s, void *pc) {
    s->__pc = (uint64_t)(uintptr_t)pc;
}

typedef unsigned int *thread_state_t;

/* Linux 没有「停下来读别的线程寄存器」的接口（要 ptrace），这里恒失败，
 * 调用方会跳过寄存器打印——只影响崩溃时的诊断信息量，不影响游戏。 */
static inline int thread_get_state(mach_port_t thread, int flavor, thread_state_t state, unsigned int *count) {
    (void)thread; (void)flavor; (void)state; (void)count;
    return 5;
}


/* NEON 状态：Android 上同样取不到，只为编译。 */
#define ARM_NEON_STATE64 17
#define ARM_NEON_STATE64_COUNT 64

typedef struct arm_neon_state64 {
    uint32_t __v[32][4];
    uint32_t __fpsr;
    uint32_t __fpcr;
} arm_neon_state64_t;

static inline int thread_set_state(mach_port_t thread, int flavor, thread_state_t state, unsigned int count) {
    (void)thread; (void)flavor; (void)state; (void)count;
    return 5;
}
