#include "ss_haptics.h"
#include "dm.h"
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <stddef.h>

_Static_assert(sizeof(ss_vibration_value) == 16, "Switch VibrationValue ABI");
_Static_assert(sizeof(ss_vibration_info) == 8, "Switch VibrationDeviceInfo ABI");

extern void ss_haptics_platform_apply(ss_haptic_output) __attribute__((weak_import));
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ss_vibration_value values[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
static ss_haptic_status status;

static int device_index(uint32_t handle) {
    return handle == SS_VIBRATION_LEFT ? 0 : handle == SS_VIBRATION_RIGHT ? 1 : -1;
}
static float bounded(float value, float minimum, float maximum, float fallback) {
    return !isfinite(value) ? fallback : fmaxf(minimum, fminf(value, maximum));
}
static ss_vibration_value clean(ss_vibration_value value) {
    value.amplitude_low = bounded(value.amplitude_low, 0, 1, 0);
    value.amplitude_high = bounded(value.amplitude_high, 0, 1, 0);
    value.frequency_low = bounded(value.frequency_low, 40, 1280, 160);
    value.frequency_high = bounded(value.frequency_high, 40, 1280, 320);
    return value;
}
static int allowed_locked(void) { return status.supported && !status.suspended; }

/* The phone has one actuator. Use the stronger left/right band, then combine
 * band energy. Equal signals on both Joy-Cons must not double the strength.
 * Core Haptics exposes sharpness, not a carrier frequency in hertz. Map the
 * amplitude-weighted log frequency to its 0..1 sharpness range. */
static ss_haptic_output mixed_locked(void) {
    ss_haptic_output result = {.epoch = status.epoch};
    if (!allowed_locked()) return result;
    const ss_vibration_value *low = values[0].amplitude_low >= values[1].amplitude_low ? &values[0] : &values[1];
    const ss_vibration_value *high = values[0].amplitude_high >= values[1].amplitude_high ? &values[0] : &values[1];
    float low_power = low->amplitude_low * low->amplitude_low;
    float high_power = high->amplitude_high * high->amplitude_high;
    float power = low_power + high_power;
    if (power > 0) {
        result.intensity = fminf(1, sqrtf(power));
        result.sharpness = (low_power * log2f(low->frequency_low / 40) +
                            high_power * log2f(high->frequency_high / 40)) / (5 * power);
    }
    return result;
}
static void emit_locked(int force) {
    ss_haptic_output output = mixed_locked();
    if (!force && fabsf(output.intensity - status.output.intensity) < 0.00001f &&
        fabsf(output.sharpness - status.output.sharpness) < 0.00001f) return;
    if (status.output.intensity > 0 && output.intensity == 0) ++status.stops;
    ++status.output_changes;
    status.output = output;
    /* This hook only enqueues work. Keep submission in device update order. */
    if (ss_haptics_platform_apply) ss_haptics_platform_apply(output);
}
static void clear_locked(void) {
    values[0] = values[1] = (ss_vibration_value){0, 160, 0, 320};
    ++status.epoch;
    emit_locked(1);
}
void ss_haptics_configure(int supported) {
    pthread_mutex_lock(&lock);
    status.supported = supported != 0;
    clear_locked();
    pthread_mutex_unlock(&lock);
}
void ss_haptics_suspend_for(unsigned reason, int suspended) {
    pthread_mutex_lock(&lock);
    unsigned previous = status.suspended;
    if (suspended) status.suspended |= reason; else status.suspended &= ~reason;
    if (previous != status.suspended) clear_locked();
    pthread_mutex_unlock(&lock);
}
int ss_haptics_allowed(void) {
    pthread_mutex_lock(&lock);
    int result = allowed_locked();
    pthread_mutex_unlock(&lock);
    return result;
}
void ss_haptics_initialize(uint32_t handle) {
    int index = device_index(handle);
    if (index < 0) return;
    pthread_mutex_lock(&lock);
    values[index] = (ss_vibration_value){0, 160, 0, 320};
    emit_locked(0);
    pthread_mutex_unlock(&lock);
}
void ss_haptics_send(uint32_t handle, const ss_vibration_value *value) {
    int index = device_index(handle);
    if (index < 0 || !value) return;
    pthread_mutex_lock(&lock);
    ++status.requests;
    ss_vibration_value sanitized = clean(*value);
    if (sanitized.amplitude_low > 0 || sanitized.amplitude_high > 0) ++status.nonzero_requests;
    /* Do not replay background signals after the app becomes active. */
    values[index] = allowed_locked() ? sanitized : (ss_vibration_value){0, 160, 0, 320};
    emit_locked(0);
    if (getenv("DM_HAPTICDBG")) {
        static int shown;
        if (shown < 30) {
            ++shown;
            LOG("HID_VIBRATION handle=%#x low=%.3f@%.0fHz high=%.3f@%.0fHz (requests=%llu nonzero=%llu)",
                handle, sanitized.amplitude_low, sanitized.frequency_low,
                sanitized.amplitude_high, sanitized.frequency_high,
                (unsigned long long)status.requests, (unsigned long long)status.nonzero_requests);
        } else if ((status.requests % 300) == 0) {
            LOG("HID_VIBRATION requests=%llu nonzero=%llu output_changes=%llu stops=%llu",
                (unsigned long long)status.requests, (unsigned long long)status.nonzero_requests,
                (unsigned long long)status.output_changes, (unsigned long long)status.stops);
        }
    }
    pthread_mutex_unlock(&lock);
}
ss_vibration_value ss_haptics_actual(uint32_t handle) {
    int index = device_index(handle);
    pthread_mutex_lock(&lock);
    ss_vibration_value result = index >= 0 && allowed_locked() ? values[index] : (ss_vibration_value){0, 160, 0, 320};
    pthread_mutex_unlock(&lock);
    return result;
}
void ss_haptics_stop(void) {
    pthread_mutex_lock(&lock);
    clear_locked();
    pthread_mutex_unlock(&lock);
}
ss_haptic_status ss_haptics_status(void) {
    pthread_mutex_lock(&lock);
    ss_haptic_status result = status;
    pthread_mutex_unlock(&lock);
    return result;
}
