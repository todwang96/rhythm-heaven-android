/* Build: xcrun clang -arch arm64 -Wall -Wextra -pthread
 * tests/common/test_thread_suspend.c -o build/checks/common/test_thread_suspend
 * Run: build/checks/common/test_thread_suspend */
#include <assert.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define R_BAD_HANDLE 0xE401u
#define R_BAD_STATE 0xFA01u
enum { O_THREAD = 1 };
typedef struct {
    uint32_t handle;
    int started, exited, wkind, ss_suspended, ss_sleep;
    mach_port_t mport;
    uint64_t *ss_tsd;
} thr_t;
typedef struct { uint8_t *base; uint64_t text_end, patch_off, size; } module_t;
static module_t g_mod[1];
static int g_nmod;
static unsigned g_key18 = 1, g_keytls = 2;
static pthread_mutex_t G = PTHREAD_MUTEX_INITIALIZER;
static thr_t controller = {.handle = 0x1001, .started = 1};
static thr_t worker = {.handle = 0x1002, .started = 1};
static thr_t inactive = {.handle = 0x1003};
static _Thread_local thr_t *current_thread;
static thr_t *cur(void) { assert(current_thread); return current_thread; }
static thr_t *h_peek(uint32_t handle, int type) {
    assert(type == O_THREAD);
    if (handle == controller.handle) return &controller;
    if (handle == worker.handle) return &worker;
    if (handle == inactive.handle) return &inactive;
    return NULL;
}
static void *prepared_address;
static size_t prepared_size;
static unsigned prepare_count;
static void dm_guest_write_prepare(void *address, size_t size) {
    prepared_address = address; prepared_size = size; ++prepare_count;
}
static _Noreturn void die(const char *format, ...) {
    va_list arguments; va_start(arguments, format);
    vfprintf(stderr, format, arguments); va_end(arguments); abort();
}
#define LOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#include "../../runtime/common/ss_thread_suspend.inc"

static pthread_mutex_t commands_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t commands_condition = PTHREAD_COND_INITIALIZER;
static unsigned command;
static _Atomic unsigned phase, completed;
static _Atomic int ready;
static uint64_t worker_tsd[3] = {0, 0x1818181818181818ull, 0x1212121212121212ull};

static void sleep_ns(int64_t nanoseconds) {
    struct timespec delay = {nanoseconds / 1000000000, nanoseconds % 1000000000};
    assert(nanosleep(&delay, NULL) == 0);
}
static void wait_value(_Atomic unsigned *value, unsigned wanted) {
    struct timespec start, now;
    assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    while (atomic_load(value) != wanted) {
        sleep_ns(1000000);
        assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        assert(now.tv_sec - start.tv_sec < 10);
    }
}
static void *worker_main(void *unused) {
    (void)unused; current_thread = &worker;
    pthread_mutex_lock(&G);
    worker.mport = pthread_mach_thread_np(pthread_self());
    worker.ss_tsd = worker_tsd;
    pthread_mutex_unlock(&G);
    atomic_store(&ready, 1);
    for (;;) {
        pthread_mutex_lock(&commands_mutex);
        while (!command) pthread_cond_wait(&commands_condition, &commands_mutex);
        unsigned requested = command; command = 0;
        pthread_mutex_unlock(&commands_mutex);
        if (requested == UINT32_MAX) return NULL;
        /* Command 1 reproduces the old unmarked host sleep. Command 2 is a
         * long sleep. Later commands check stop/resume near sleep expiry. */
        int marked = requested != 1;
        if (marked) ss_set_thread_sleep(1);
        atomic_store(&phase, requested);
        sleep_ns(requested == 2 ? 3000000000ll : 30000000);
        if (marked) ss_set_thread_sleep(0);
        atomic_fetch_add(&completed, 1);
        atomic_store(&phase, 0);
    }
}
static void start_command(unsigned requested) {
    assert(atomic_load(&phase) == 0);
    pthread_mutex_lock(&commands_mutex);
    assert(!command); command = requested;
    pthread_cond_signal(&commands_condition);
    pthread_mutex_unlock(&commands_mutex);
    wait_value(&phase, requested);
}
static arm_thread_state64_t integer_state(void) {
    arm_thread_state64_t state;
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    assert(thread_get_state(worker.mport, ARM_THREAD_STATE64,
                            (thread_state_t)&state, &count) == KERN_SUCCESS);
    return state;
}
static void check_context(void) {
    /* Canaries detect an ABI size overrun. Integer fields must match the
     * actual stopped Mach thread, except the two guest TLS substitutions. */
    struct { uint64_t before; uint64_t words[0x320 / 8]; uint64_t after; } output;
    memset(&output, 0xa5, sizeof(output));
    unsigned calls = prepare_count;
    assert(ss_get_thread_context(output.words, worker.handle) == 0);
    assert(output.before == 0xa5a5a5a5a5a5a5a5ull);
    assert(output.after == 0xa5a5a5a5a5a5a5a5ull);
    assert(prepare_count == calls + 1);
    assert(prepared_address == output.words && prepared_size == 0x320);
    arm_thread_state64_t actual = integer_state();
    for (unsigned i = 0; i < 29; ++i)
        assert(output.words[i] == (i == 18 ? worker_tsd[g_key18] : actual.__x[i]));
    assert(output.words[29] == arm_thread_state64_get_fp(actual));
    assert(output.words[30] == arm_thread_state64_get_lr(actual));
    assert(output.words[31] == arm_thread_state64_get_sp(actual));
    assert(output.words[32] == arm_thread_state64_get_pc(actual));
    assert(output.words[0x318 / 8] == worker_tsd[g_keytls]);
    assert(output.words[31] && output.words[32]);
    arm_neon_state64_t vector;
    mach_msg_type_number_t vector_count = ARM_NEON_STATE64_COUNT;
    assert(thread_get_state(worker.mport, ARM_NEON_STATE64,
                            (thread_state_t)&vector, &vector_count) == KERN_SUCCESS);
    uint8_t *bytes = (uint8_t *)output.words;
    assert(memcmp(bytes + 0x108, &actual.__cpsr, 4) == 0);
    assert(memcmp(bytes + 0x110, vector.__v, 512) == 0);
    assert(memcmp(bytes + 0x310, &vector.__fpcr, 4) == 0);
    assert(memcmp(bytes + 0x314, &vector.__fpsr, 4) == 0);
}
static void check_sleep_flag(int expected) {
    pthread_mutex_lock(&G); assert(worker.ss_sleep == expected); pthread_mutex_unlock(&G);
}

int main(void) {
    current_thread = &controller;
    controller.mport = pthread_mach_thread_np(pthread_self());
    uint8_t context[0x320];
    assert(ss_set_thread_activity(0xdead, 1) == R_BAD_HANDLE);
    assert(ss_set_thread_activity(worker.handle, 2) == 0xf001);
    assert(ss_set_thread_activity(controller.handle, 1) == R_BAD_STATE);
    assert(ss_set_thread_activity(inactive.handle, 1) == R_BAD_STATE);
    inactive.started = 1;
    assert(ss_set_thread_activity(inactive.handle, 1) == R_BAD_STATE);
    inactive.mport = controller.mport; inactive.exited = 1;
    assert(ss_set_thread_activity(inactive.handle, 1) == R_BAD_STATE);
    assert(ss_get_thread_context(context, 0xdead) == R_BAD_HANDLE);
    assert(ss_get_thread_context(context, worker.handle) == R_BAD_STATE);

    pthread_t thread; assert(pthread_create(&thread, NULL, worker_main, NULL) == 0);
    while (!atomic_load(&ready)) sleep_ns(1000000);
    assert(ss_set_thread_activity(worker.handle, 0) == R_BAD_STATE);

    start_command(1);
    /* One raw Mach snapshot proves why the old predicate rejects host
     * nanosleep. Do not run its fatal 100,000-attempt failure loop. */
    assert(thread_suspend(worker.mport) == KERN_SUCCESS);
    arm_thread_state64_t unmarked = integer_state();
    assert(!ss_guest_pc(arm_thread_state64_get_pc(unmarked)));
    check_sleep_flag(0);
    assert(!worker.wkind);
    assert(thread_resume(worker.mport) == KERN_SUCCESS);
    wait_value(&completed, 1); wait_value(&phase, 0);

    for (unsigned requested = 2; requested <= 18; ++requested) {
        start_command(requested);
        check_sleep_flag(1);
        assert(ss_set_thread_activity(worker.handle, 1) == 0);
        assert(worker.ss_suspended);
        assert(ss_set_thread_activity(worker.handle, 1) == R_BAD_STATE);
        check_context();
        unsigned previous = atomic_load(&completed);
        /* For short sleeps, their deadline passes while stopped. The worker
         * must still not clear its marker or publish completion. */
        sleep_ns(60000000);
        assert(atomic_load(&completed) == previous);
        check_sleep_flag(1);
        assert(ss_set_thread_activity(worker.handle, 0) == 0);
        assert(!worker.ss_suspended);
        assert(ss_set_thread_activity(worker.handle, 0) == R_BAD_STATE);
        wait_value(&completed, requested); wait_value(&phase, 0);
        check_sleep_flag(0);
        assert(ss_get_thread_context(context, worker.handle) == R_BAD_STATE);
    }
    pthread_mutex_lock(&commands_mutex);
    command = UINT32_MAX; pthread_cond_signal(&commands_condition);
    pthread_mutex_unlock(&commands_mutex);
    assert(pthread_join(thread, NULL) == 0);
    puts("PASS: real Mach sleep suspension, context ABI, resume, marker cleanup, 17 cycles, and invalid requests");
    return 0;
}
