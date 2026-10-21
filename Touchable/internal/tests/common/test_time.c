/* Build: xcrun clang -Wall -Wextra -Wno-unused-function -pthread
 * -Iupstream/d2r-mobile/dm/host tests/common/test_time.c -o build/checks/common/test_time */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static _Thread_local int guest_error;
static _Thread_local void *prepared_address;
static _Thread_local size_t prepared_size;
static _Thread_local unsigned prepare_count;

static int *guest_errno_location(void) { return &guest_error; }

/* Resolve only the guest errno API. Never use the host errno as guest storage. */
void *guest_sym(const char *name) {
    assert(strcmp(name, "__errno_location") == 0);
    return (void *)guest_errno_location;
}

void dm_guest_write_prepare(void *address, size_t size) {
    prepared_address = address;
    prepared_size = size;
    ++prepare_count;
}

#include "../../runtime/common/ss_time.c"

typedef ss_timespec test_timespec;
_Static_assert(sizeof(test_timespec) == 16, "Switch timespec ABI");
_Static_assert(offsetof(test_timespec, seconds) == 0, "Switch seconds offset");
_Static_assert(offsetof(test_timespec, nanoseconds) == 8, "Switch nanoseconds offset");

static int read_guest_clock(int clock_id, test_timespec *out) {
    assert(strcmp(hle_force_ss_clock_gettime.name, "clock_gettime") == 0);
    return ((int (*)(int, test_timespec *))hle_force_ss_clock_gettime.fn)(clock_id, out);
}

static int64_t host_ns(const struct timespec *value) {
    return (int64_t)value->tv_sec * 1000000000 + value->tv_nsec;
}

static int64_t guest_value_ns(const test_timespec *value) {
    assert(value->nanoseconds >= 0 && value->nanoseconds < 1000000000);
    return value->seconds * 1000000000 + value->nanoseconds;
}

static void check_clock_bounds(int guest_clock, clockid_t host_clock) {
    struct timespec before, after;
    test_timespec value = {-1, -1};
    unsigned count = prepare_count;
    guest_error = 123;
    assert(clock_gettime(host_clock, &before) == 0);
    assert(read_guest_clock(guest_clock, &value) == 0);
    assert(clock_gettime(host_clock, &after) == 0);
    int64_t result = guest_value_ns(&value);
    assert(result >= host_ns(&before) && result <= host_ns(&after));
    assert(guest_error == 123);
    assert(prepare_count == count + 1);
    assert(prepared_address == &value && prepared_size == sizeof(value));
}

static void check_failure(int clock_id, int expected_error) {
    test_timespec value = {0x12345678, 0x23456789};
    test_timespec original = value;
    guest_error = 777;
    assert(read_guest_clock(clock_id, &value) == -1);
    assert(guest_error == expected_error);
    assert(memcmp(&value, &original, sizeof(value)) == 0);
}

static pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t error_condition = PTHREAD_COND_INITIALIZER;
static unsigned arrived;

static void *check_thread_error(void *argument) {
    int clock_id = (int)(intptr_t)argument;
    int expected = clock_id == 2 ? 95 : 22;
    check_failure(clock_id, expected);
    pthread_mutex_lock(&error_mutex);
    if (++arrived == 2) pthread_cond_broadcast(&error_condition);
    while (arrived < 2) pthread_cond_wait(&error_condition, &error_mutex);
    pthread_mutex_unlock(&error_mutex);
    assert(guest_error == expected);
    return NULL;
}

int main(void) {
    check_clock_bounds(0, CLOCK_REALTIME);
    check_clock_bounds(1, CLOCK_MONOTONIC);
    test_timespec first, previous, current;
    assert(read_guest_clock(1, &first) == 0);
    previous = first;
    for (unsigned i = 0; i < 1000; ++i) {
        assert(read_guest_clock(1, &current) == 0);
        assert(guest_value_ns(&current) >= guest_value_ns(&previous));
        previous = current;
    }
    struct timespec delay = {0, 2000000};
    assert(nanosleep(&delay, NULL) == 0);
    assert(read_guest_clock(1, &current) == 0);
    assert(guest_value_ns(&current) > guest_value_ns(&first));

    for (int clock_id = 2; clock_id < 12; ++clock_id) check_failure(clock_id, 95);
    check_failure(-1, 22);
    check_failure(INT32_MIN, 22);
    check_failure(12, 22);
    check_failure(INT32_MAX, 22);
    guest_error = 777;
    assert(read_guest_clock(0, NULL) == -1 && guest_error == 14);
    guest_error = 777;
    assert(read_guest_clock(1, NULL) == -1 && guest_error == 14);

    guest_error = 456;
    pthread_t threads[2];
    assert(pthread_create(&threads[0], NULL, check_thread_error, (void *)(intptr_t)2) == 0);
    assert(pthread_create(&threads[1], NULL, check_thread_error, (void *)(intptr_t)-1) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(guest_error == 456);
    puts("PASS: guest clock mapping, output ABI, monotonic order, and thread errno");
    return 0;
}
