#include "branch_probe.h"
#include "../../runtime/common/ss_indirect_branch.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t x[31], sp, nzcv, fpcr, fpsr, reserved;
    unsigned char v[32][16];
} Snapshot;
_Static_assert(sizeof(Snapshot) == 800, "Assembly snapshot layout mismatch");
extern void ss_test_snapshot(Snapshot *, Snapshot *);
extern char ss_test_trap[], ss_test_target[];
extern uintptr_t ss_test_stack_pivot(void);
extern char ss_test_sp_trap[], ss_test_sp_resume[];
extern uintptr_t ss_test_call(void);
extern char ss_test_call_trap[], ss_test_call_resume[], ss_test_call_target[];
static pthread_key_t probe_key;
static void *run_worker(void *argument) {
    uintptr_t count = (uintptr_t)argument;
    void *alternate = malloc(64 * 1024);
    if (!alternate) return (void *)5;
    stack_t stack = {.ss_sp = alternate, .ss_size = 64 * 1024}, previous;
    if (sigaltstack(&stack, &previous)) { free(alternate); return (void *)6; }
    uintptr_t result = 0;
    if (pthread_setspecific(probe_key, ss_test_target)) result = 1;
    for (uintptr_t attempt = 0; !result && attempt < count; ++attempt) {
        Snapshot before = {0}, after = {0};
        ss_test_snapshot(&before, &after);
        /* x18 belongs to Darwin. Its guest value is the checked TSD slot. */
        before.x[18] = after.x[18] = 0;
        if (memcmp(&before, &after, sizeof(before))) { result = 2; break; }
        if (before.nzcv != 0xa0000000 || before.x[16] != 0x1010 ||
            before.x[17] != 0x1011 || before.v[31][15] != 32)
            { result = 3; break; }
        if (pthread_getspecific(probe_key) != ss_test_target) { result = 4; break; }
    }
    _Alignas(16) unsigned char pivot_space[1024];
    uintptr_t new_sp = (uintptr_t)(pivot_space + sizeof(pivot_space));
    if (pthread_setspecific(probe_key, (void *)new_sp) || ss_test_stack_pivot() != new_sp)
        result = 7;
    if (pthread_setspecific(probe_key, ss_test_call_target) ||
        ss_test_call() != (uintptr_t)ss_test_call_resume)
        result = 9;
    /* This Darwin kernel checks the size even when SS_DISABLE is set. */
    if (previous.ss_flags & SS_DISABLE) {
        previous.ss_sp = alternate;
        previous.ss_size = stack.ss_size;
    }
    if (sigaltstack(&previous, NULL)) result = 8;
    else free(alternate);
    return (void *)result;
}

int ss_run_branch_probe(char *report, size_t capacity) {
    int error = pthread_key_create(&probe_key, NULL);
    if (error) { snprintf(report, capacity, "FAIL: pthread key %d", error); return 1; }
    ss_trap_site traps[] = {
        {(uintptr_t)ss_test_trap, 0, SS_BR_X18},
        {(uintptr_t)ss_test_sp_trap, (uintptr_t)ss_test_sp_resume, SS_MOV_SP_X18},
        {(uintptr_t)ss_test_call_trap, (uintptr_t)ss_test_call_resume, SS_BLR_X18}
    };
    error = ss_indirect_install(traps, 3, probe_key);
    if (error) {
        snprintf(report, capacity, "FAIL: trap install %d (%s)", error, strerror(error));
        pthread_key_delete(probe_key);
        return 1;
    }
    pthread_t threads[4];
    unsigned created = 0, failures = 0;
    uintptr_t failure_codes = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (pthread_create(&threads[i], NULL, run_worker, (void *)1000)) { ++failures; break; }
        ++created;
    }
    for (unsigned i = 0; i < created; ++i) {
        void *result = NULL;
        if (pthread_join(threads[i], &result) || result) {
            ++failures;
            failure_codes |= (uintptr_t)result < 32 ? (1u << (uintptr_t)result) : 1u;
        }
    }
    error = ss_indirect_uninstall();
    pthread_key_delete(probe_key);
    if (error) ++failures;
    snprintf(report, capacity,
             "%s: %u threads x 1000 registered BR X18 transfers (failure mask %lu).\n"
             "Checked GP registers, SP, LR, NZCV, FPCR, FPSR, SIMD and virtual x18.\n"
             "Checked 4 MOV SP, X18 stack transfers with alternate signal stacks.\n"
             "Checked 4 BLR X18 calls and their return addresses.\n"
             "This tests the CPU bridge. It does not run Silksong.",
             failures ? "FAIL" : "PASS", created, (unsigned long)failure_codes);
    return failures ? 1 : 0;
}

#ifdef SS_PROBE_CLI
int main(void) {
    char report[512];
    int result = ss_run_branch_probe(report, sizeof(report));
    puts(report);
    return result;
}
#endif
