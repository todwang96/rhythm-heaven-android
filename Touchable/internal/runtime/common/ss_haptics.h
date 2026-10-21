#pragma once
#include <stdint.h>

/* Switch HID passes four floats for each of the two controller motors. */
typedef struct {
    float amplitude_low, frequency_low, amplitude_high, frequency_high;
} ss_vibration_value;
typedef struct { uint32_t type, position; } ss_vibration_info;

typedef struct {
    float intensity, sharpness;
    uint64_t epoch;
} ss_haptic_output;
typedef struct {
    uint64_t requests, nonzero_requests, output_changes, stops, epoch;
    unsigned supported, suspended;
    ss_haptic_output output;
} ss_haptic_status;

enum { SS_HAPTICS_INACTIVE = 1u, SS_HAPTICS_INTERRUPTED = 2u };
enum { SS_VIBRATION_LEFT = 0x53530001u, SS_VIBRATION_RIGHT = 0x53530002u };

void ss_haptics_configure(int supported);
void ss_haptics_suspend_for(unsigned reason, int suspended);
int ss_haptics_allowed(void);
void ss_haptics_initialize(uint32_t handle);
void ss_haptics_send(uint32_t handle, const ss_vibration_value *value);
ss_vibration_value ss_haptics_actual(uint32_t handle);
void ss_haptics_stop(void);
ss_haptic_status ss_haptics_status(void);

/* The iOS front end owns the Core Haptics engine. The host CLI has no sink. */
void ss_haptics_ios_init(void);
void ss_haptics_platform_apply(ss_haptic_output output);
