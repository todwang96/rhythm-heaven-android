#include "ss_indirect_branch.h"

#if !defined(__APPLE__) || !defined(__aarch64__)
#error This module needs Darwin arm64.
#endif
#if __has_feature(ptrauth_calls)
#error Build this guest bridge for arm64, not arm64e.
#endif

#include <errno.h>
#include <limits.h>
#include <mach/arm/thread_status.h>
#include <signal.h>
#include <string.h>
#include <sys/ucontext.h>

enum { SS_MAX_TRAPS = 256 };
static ss_trap_site registered_traps[SS_MAX_TRAPS];
static size_t registered_count;
static pthread_key_t virtual_x18_key;
static struct sigaction previous_action;
static volatile sig_atomic_t installed;

static uintptr_t read_virtual_x18(void) {
    uintptr_t base;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
    return ((const volatile uintptr_t *)(base & ~(uintptr_t)7))[virtual_x18_key];
}

static void handle_trap(int number, siginfo_t *info, void *context) {
    int saved_errno = errno;
    ucontext_t *uc = context;
    uintptr_t pc = arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    /* Darwin reports BRK with si_code == 0 on the tested macOS kernel. The
     * exact registered PC, not si_code alone, is the authority here.
     */
    if (info->si_code == 0 || info->si_code == TRAP_BRKPT) {
        for (size_t i = 0; i < registered_count; ++i) {
            const ss_trap_site *site = &registered_traps[i];
            if (site->address != pc) continue;
            uintptr_t target = read_virtual_x18();
            if (!target || (target & (site->kind == SS_MOV_SP_X18 ? 15 : 3))) break;
            if (site->kind == SS_MOV_SP_X18) {
                arm_thread_state64_set_sp(uc->uc_mcontext->__ss, target);
                target = site->resume;
            }
            if (site->kind == SS_BLR_X18)
                arm_thread_state64_set_lr_fptr(uc->uc_mcontext->__ss,
                                               (void (*)(void))site->resume);
            /* Build for arm64, not arm64e. Guest pointers have no PAC. The
             * kernel restores all other GP, SIMD and status registers.
             */
            arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss,
                                           (void (*)(void))target);
            errno = saved_errno;
            return;
        }
    }
    /* Never consume a debugger trap or a trap at an unregistered address. */
    sigaction(number, &previous_action, NULL);
    raise(number);
    errno = saved_errno;
}

int ss_indirect_install(const ss_trap_site *traps, size_t count, pthread_key_t key) {
    if (installed) return EALREADY;
    if (!traps || !count || count > SS_MAX_TRAPS || key >= PTHREAD_KEYS_MAX)
        return EINVAL;
    for (size_t i = 0; i < count; ++i) {
        if (!traps[i].address || (traps[i].address & 3)) return EINVAL;
        if (traps[i].kind != SS_BR_X18 && traps[i].kind != SS_MOV_SP_X18 &&
            traps[i].kind != SS_BLR_X18)
            return EINVAL;
        if (traps[i].kind != SS_BR_X18 && (!traps[i].resume || (traps[i].resume & 3)))
            return EINVAL;
        if (*(const uint32_t *)traps[i].address != (0xd4200000u | (0x6e18u << 5)))
            return EINVAL;
        for (size_t j = 0; j < i; ++j)
            if (traps[j].address == traps[i].address) return EINVAL;
    }
    if (sigaction(SIGTRAP, NULL, &previous_action)) return errno;
    if (previous_action.sa_handler != SIG_DFL) return EBUSY;
    /* Check the TSD layout used by the upstream rewriter. Do this outside
     * the signal handler. Restore the caller's original slot value.
     */
    void *saved = pthread_getspecific(key);
    uintptr_t marker = (uintptr_t)&registered_count;
    int error = pthread_setspecific(key, (void *)marker);
    if (error) return error;
    virtual_x18_key = key;
    int correct = read_virtual_x18() == marker;
    error = pthread_setspecific(key, saved);
    if (error) return error;
    if (!correct) return ENOTSUP;
    memcpy(registered_traps, traps, count * sizeof(*traps));
    registered_count = count;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = handle_trap;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGTRAP, &action, NULL)) return errno;
    installed = 1;
    return 0;
}

int ss_indirect_uninstall(void) {
    if (!installed) return ENOENT;
    if (sigaction(SIGTRAP, &previous_action, NULL)) return errno;
    installed = 0;
    registered_count = 0;
    return 0;
}
