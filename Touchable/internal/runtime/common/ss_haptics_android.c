/* Android haptics backend for the Touchable runtime.
 *
 * The runtime (ss_haptics.c) mixes the two Switch "Joy-Con" motor signals into one
 * {intensity, sharpness} pair and calls ss_haptics_platform_apply() on the HID thread.
 * The iOS front end implements that hook in Haptics.m with Core Haptics; here we forward
 * it to the Android Vibrator through the JNI helpers in android/src/app_android.c. */
#ifdef __ANDROID__

#include "ss_haptics.h"
#include "dm.h"

/* Implemented in app_android.c. */
extern int dm_android_haptic_available(void);
extern void dm_android_haptic_apply(float intensity, float sharpness);

void ss_haptics_platform_apply(ss_haptic_output output) {
    dm_android_haptic_apply(output.intensity, output.sharpness);
}

/* Called once by the Android host after the Java Vibrator has been handed over. */
void ss_haptics_android_init(void) {
    int available = dm_android_haptic_available();
    ss_haptics_configure(available);
    LOG("haptics: Android vibrator %s", available ? "enabled" : "unavailable (game sees no vibration device)");
}

void ss_haptics_android_suspend(int suspended) {
    ss_haptics_suspend_for(SS_HAPTICS_INACTIVE, suspended ? 1 : 0);
}

#endif /* __ANDROID__ */
