/* Build: xcrun clang -Wall -Wextra -Wno-unused-function -pthread
 * -Iupstream/d2r-mobile/dm/host tests/common/test_haptics.c -o build/checks/common/test_haptics */
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/common/ss_haptics.h"

static ss_haptic_output emitted;
static unsigned emitted_count, prepare_count;
static void *prepared;
static size_t prepared_size;
void dm_guest_write_prepare(void *address, size_t size) {
    prepared = address;
    prepared_size = size;
    ++prepare_count;
}
uint64_t guest_ns(void) { return 0; }
void hle_bind_system_event(void *event, int clear) { (void)event; (void)clear; }
void hle_signal_system_event(void *event) { (void)event; }

#include "../../runtime/common/ss_haptics.c"
#include "../../runtime/common/ss_input.c"

void ss_haptics_platform_apply(ss_haptic_output output) {
    assert(isfinite(output.intensity) && output.intensity >= 0 && output.intensity <= 1);
    assert(isfinite(output.sharpness) && output.sharpness >= 0 && output.sharpness <= 1);
    emitted = output;
    ++emitted_count;
}

_Static_assert(offsetof(ss_vibration_value, amplitude_low) == 0, "low amplitude ABI");
_Static_assert(offsetof(ss_vibration_value, frequency_low) == 4, "low frequency ABI");
_Static_assert(offsetof(ss_vibration_value, amplitude_high) == 8, "high amplitude ABI");
_Static_assert(offsetof(ss_vibration_value, frequency_high) == 12, "high frequency ABI");

static int near(float a, float b) { return fabsf(a - b) < 0.0001f; }
static void send(uint32_t device, float low, float low_hz, float high, float high_hz) {
    ss_vibration_value value = {low, low_hz, high, high_hz};
    ((void (*)(const uint32_t *, const ss_vibration_value *))hle_force_ss_vibration_send.fn)(&device, &value);
}
static void check_device_abi(void) {
    uint32_t id = 0x20;
    struct { uint32_t before, handles[2], after; } block = {0x12345678, {0, 0}, 0x87654321};
    int (*enumerate)(uint32_t *, int, const uint32_t *, uint32_t) = hle_force_ss_vibration_handles.fn;
    assert(enumerate(block.handles, 2, &id, 2) == 0);
    ss_haptics_configure(1);
    assert(((unsigned (*)(void))hle_force_ss_vibration_allowed.fn)() == 1);
    assert(enumerate(block.handles, 2, &id, 2) == 2);
    assert(block.handles[0] == SS_VIBRATION_LEFT && block.handles[1] == SS_VIBRATION_RIGHT);
    assert(prepared == block.handles && prepared_size == 8);
    assert(block.before == 0x12345678 && block.after == 0x87654321);
    block.handles[1] = 0xabcdef;
    assert(enumerate(block.handles, 1, &id, 2) == 1 && block.handles[1] == 0xabcdef);
    assert(enumerate(block.handles, 0, &id, 2) == 0);
    assert(enumerate(block.handles, -1, &id, 2) == 0);
    assert(enumerate(block.handles, 2, &id, 1) == 0);
    uint32_t other = 1;
    assert(enumerate(block.handles, 2, &other, 2) == 0);
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t handle = i ? SS_VIBRATION_RIGHT : SS_VIBRATION_LEFT;
        ss_vibration_info info;
        ((void (*)(ss_vibration_info *, const uint32_t *))hle_force_ss_vibration_info_get.fn)(&info, &handle);
        assert(info.type == 1 && info.position == i + 1);
        assert(prepared == &info && prepared_size == 8);
        ((void (*)(const uint32_t *))hle_force_ss_vibration_init.fn)(&handle);
    }
    uint32_t invalid = 0;
    ss_vibration_info unknown = {99, 99};
    ss_vibration_info_get(&unknown, &invalid);
    assert(!unknown.type && !unknown.position);
}
static void check_strength_and_frequency(void) {
    ss_haptics_stop();
    send(SS_VIBRATION_LEFT, 0.4f, 160, 0, 320);
    assert(near(emitted.intensity, 0.4f) && near(emitted.sharpness, 0.4f));
    unsigned count = emitted_count;
    send(SS_VIBRATION_RIGHT, 0.4f, 160, 0, 320);
    assert(emitted_count == count); /* Equal Joy-Cons do not double output. */
    send(SS_VIBRATION_LEFT, 0, 160, 0.5f, 320);
    assert(near(emitted.intensity, sqrtf(0.4f * 0.4f + 0.5f * 0.5f)));
    assert(emitted.sharpness > 0.4f && emitted.sharpness < 0.6f);
    send(SS_VIBRATION_RIGHT, 0, 160, 0, 320);
    assert(near(emitted.intensity, 0.5f) && near(emitted.sharpness, 0.6f));
    send(SS_VIBRATION_LEFT, 0, 160, 0.5f, 640);
    assert(near(emitted.intensity, 0.5f) && near(emitted.sharpness, 0.8f));
    ss_vibration_value actual;
    uint32_t left = SS_VIBRATION_LEFT;
    ((void (*)(ss_vibration_value *, const uint32_t *))hle_force_ss_vibration_actual.fn)(&actual, &left);
    assert(near(actual.amplitude_high, 0.5f) && near(actual.frequency_high, 640));
    assert(prepared == &actual && prepared_size == 16);
    send(SS_VIBRATION_LEFT, 1, 160, 1, 320);
    assert(near(emitted.intensity, 1));
    send(SS_VIBRATION_LEFT, 0, 160, 0, 320); /* The game's off/stop signal. */
    assert(emitted.intensity == 0);
    ss_haptic_status state = ss_haptics_status();
    assert(state.requests > 0 && state.nonzero_requests > 0 && state.stops > 0);
}
static void check_lifecycle(void) {
    unsigned (*permitted)(void) = hle_force_ss_vibration_allowed.fn;
    assert(permitted() == 1);
    send(SS_VIBRATION_LEFT, 0.5f, 160, 0.4f, 320);
    uint64_t epoch = emitted.epoch;
    ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, 1);
    assert(!ss_haptics_allowed() && emitted.intensity == 0 && emitted.epoch > epoch);
    assert(permitted() == 1); /* Focus does not change system permission. */
    ss_haptics_suspend_for(SS_HAPTICS_INTERRUPTED, 1);
    ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, 0);
    assert(!ss_haptics_allowed()); /* Audio interruption is still active. */
    assert(permitted() == 1);
    send(SS_VIBRATION_RIGHT, 1, 160, 1, 320);
    assert(emitted.intensity == 0);
    ss_haptics_suspend_for(SS_HAPTICS_INTERRUPTED, 0);
    assert(ss_haptics_allowed() && emitted.intensity == 0);
    assert(ss_haptics_actual(SS_VIBRATION_RIGHT).amplitude_high == 0); /* No stale replay. */
    send(SS_VIBRATION_LEFT, 0.3f, 160, 0, 320);
    assert(near(emitted.intensity, 0.3f));
    ss_pad_styles(0);
    assert(emitted.intensity == 0);
    ss_pad_styles(3);
    send(SS_VIBRATION_LEFT, 0.3f, 160, 0, 320);
    uint32_t full_key = 0;
    ss_pad_ids(&full_key, 1);
    assert(emitted.intensity == 0);
    uint32_t handheld = 0x20;
    ss_pad_ids(&handheld, 1);
    ss_haptics_configure(0);
    send(SS_VIBRATION_LEFT, 1, 160, 1, 320);
    assert(!ss_haptics_allowed() && emitted.intensity == 0);
    assert(permitted() == 0); /* A device without a sink reports no support. */
    ss_haptics_configure(1);
}
static void check_invalid_values(void) {
    send(SS_VIBRATION_LEFT, NAN, NAN, -5, INFINITY);
    assert(emitted.intensity == 0);
    ss_vibration_value actual = ss_haptics_actual(SS_VIBRATION_LEFT);
    assert(actual.frequency_low == 160 && actual.frequency_high == 320);
    send(SS_VIBRATION_LEFT, 8, -100, 5, 99999);
    assert(emitted.intensity == 1 && near(emitted.sharpness, 0.5f));
    actual = ss_haptics_actual(SS_VIBRATION_LEFT);
    assert(actual.amplitude_low == 1 && actual.frequency_low == 40);
    assert(actual.amplitude_high == 1 && actual.frequency_high == 1280);
    unsigned count = emitted_count;
    send(0xdeadbeef, 1, 160, 1, 320);
    assert(emitted_count == count);
    actual = ss_haptics_actual(0xdeadbeef);
    assert(actual.amplitude_low == 0 && actual.amplitude_high == 0);
    ss_haptics_stop();
}
static void *write_channel(void *argument) {
    uint32_t handle = (uint32_t)(uintptr_t)argument;
    for (unsigned i = 0; i < 10000; ++i) {
        ss_vibration_value value = {(i % 7) / 7.0f, 80 + i % 800, (i % 11) / 11.0f, 160 + i % 1000};
        ss_haptics_send(handle, &value);
        ss_haptic_status state = ss_haptics_status();
        assert(state.output.intensity >= 0 && state.output.intensity <= 1);
    }
    return NULL;
}
int main(void) {
    check_device_abi();
    check_strength_and_frequency();
    check_lifecycle();
    check_invalid_values();
    pthread_t left, right;
    assert(pthread_create(&left, NULL, write_channel, (void *)(uintptr_t)SS_VIBRATION_LEFT) == 0);
    assert(pthread_create(&right, NULL, write_channel, (void *)(uintptr_t)SS_VIBRATION_RIGHT) == 0);
    assert(pthread_join(left, NULL) == 0 && pthread_join(right, NULL) == 0);
    ss_haptics_stop();
    assert(emitted.intensity == 0 && prepare_count > 0);
    puts("PASS: Switch HD vibration ABI, intensity, frequency, dual-channel mix, stop, focus/audio gates, invalid values, concurrent game updates");
    return 0;
}
