/* 节奏天国 · Android 宿主入口（JNI）。
 *
 * 只做四件事：接住 Surface → 交给 vk 后端；起一条线程跑 ss_host_run()；
 * 把显示刷新率告诉后端；退出时释放窗口。游戏逻辑、服务、输入解析全在
 * Touchable 的 ss_ 运行时里，这里不碰。
 */
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "services_fs.h"
#include "services_platform.h"
#include "services_sdk.h"
#include <time.h>
#include "ss_haptics.h"
#include "ss_touch.h"

#define TAG "RHPORT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)


/* ---- 震动（Android Vibrator） -------------------------------------------------
 * Java 侧在 onCreate 里把 Vibrator 交给 nativeSetVibrator()；HID 线程上的
 * ss_haptics_platform_apply() 经 dm_android_haptic_apply() 转成短脉冲。
 * 手机只有一个马达，只能近似幅度/频率：幅度→amplitude，尖锐度→时长。 */
static JavaVM *g_vm;
static jobject g_vibrator;
static jclass g_vibration_effect;
static jmethodID g_mid_vibrate_effect, g_mid_cancel, g_mid_create_one_shot;
static int g_haptic_ready;
static _Atomic uint64_t g_haptic_applies, g_haptic_pulses;
extern void ss_haptics_android_init(void);
extern void ss_haptics_platform_apply(ss_haptic_output output);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

static JNIEnv *haptic_env(void) {
    if (!g_vm) return NULL;
    JNIEnv *env = NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) return env;
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == JNI_OK) return env;
    return NULL;
}

static _Atomic int g_haptic_selftest_done;
void dm_android_haptic_apply(float intensity, float sharpness);
static void *haptic_selftest(void *unused) {
    (void)unused;
    LOGI("HAPTIC selftest start (DM_HAPTICTEST)");
    for (int i = 0; i < 3; i++) {
        dm_android_haptic_apply(0.8f, 0.5f);
        usleep(120000);
        dm_android_haptic_apply(0.0f, 0.5f);
        usleep(180000);
    }
    LOGI("HAPTIC selftest done applies=%llu pulses=%llu",
         (unsigned long long)atomic_load(&g_haptic_applies),
         (unsigned long long)atomic_load(&g_haptic_pulses));
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_rhport_rhythm_GameActivity_nativeSetVibrator(JNIEnv *env, jclass cls, jobject vibrator) {
    (void)cls;
    g_haptic_ready = 0;
    if (!vibrator) { ss_haptics_android_init(); return; }
    if (g_vibrator) (*env)->DeleteGlobalRef(env, g_vibrator);
    g_vibrator = (*env)->NewGlobalRef(env, vibrator);
    jclass vc = (*env)->GetObjectClass(env, vibrator);
    g_mid_vibrate_effect = (*env)->GetMethodID(env, vc, "vibrate", "(Landroid/os/VibrationEffect;)V");
    g_mid_cancel = (*env)->GetMethodID(env, vc, "cancel", "()V");
    (*env)->DeleteLocalRef(env, vc);
    jclass ve = (*env)->FindClass(env, "android/os/VibrationEffect");
    if (ve) {
        g_mid_create_one_shot = (*env)->GetStaticMethodID(env, ve, "createOneShot", "(JI)Landroid/os/VibrationEffect;");
        g_vibration_effect = (*env)->NewGlobalRef(env, ve);
        (*env)->DeleteLocalRef(env, ve);
    }
    g_haptic_ready = g_vibrator && g_mid_vibrate_effect && g_mid_create_one_shot && g_mid_cancel;
    ss_haptics_android_init();
    if (g_haptic_ready && getenv("DM_HAPTICTEST") && !atomic_exchange(&g_haptic_selftest_done, 1)) {
        static pthread_t thread;
        pthread_create(&thread, NULL, haptic_selftest, NULL);
        pthread_detach(thread);
    }
}

int dm_android_haptic_available(void) { return g_haptic_ready; }

/* 震动强度增益：用户要求先 +50%，再"再加强 50%" → 默认 1.5 × 1.5 = 2.25，
 * DM_HAPTIC_GAIN 可覆盖（0.0~4；0 = 启动器里选择"关闭"）。幅度映射仍是 0-255，超过 255 饱和；
 * 强脉冲基本都到满幅，弱/中等脉冲按比例增强。
 * Haptic intensity gain: default 2.25; DM_HAPTIC_GAIN overrides (0..4; 0 disables). */
static float haptic_gain(void) {
    static float gain = -1.0f;
    if (gain < 0) {
        const char *e = getenv("DM_HAPTIC_GAIN");
        float v = e ? (float)atof(e) : 2.25f;
        if (!(v >= 0.0f && v <= 4.0f)) v = 2.25f;
        gain = v;
    }
    return gain;
}

void dm_android_haptic_apply(float intensity, float sharpness) {
    if (!g_haptic_ready) return;
    if (haptic_gain() <= 0.0f) return;   /* 启动器设置：关闭震动 */
    atomic_fetch_add(&g_haptic_applies, 1);
    if (!(intensity > 0.02f) || intensity != intensity) {
        if (g_mid_cancel) { JNIEnv *env = haptic_env(); if (env) { (*env)->CallVoidMethod(env, g_vibrator, g_mid_cancel); (*env)->ExceptionClear(env); } }
        return;
    }
    /* 每帧都会来一次更新：8ms 内合并，避免刷爆振动服务。 */
    static _Atomic uint64_t last_ns;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    uint64_t prev = atomic_load(&last_ns);
    if (now - prev < 8000000ull) return;
    atomic_store(&last_ns, now);
    JNIEnv *env = haptic_env();
    if (!env) return;
    intensity *= haptic_gain();
    if (intensity > 1.0f) intensity = 1.0f;
    int amplitude = (int)(intensity * 255.0f + 0.5f);
    if (amplitude < 1) amplitude = 1;
    if (amplitude > 255) amplitude = 255;
    float sharp = sharpness == sharpness ? sharpness : 0.5f;
    if (sharp < 0) sharp = 0; if (sharp > 1) sharp = 1;
    jlong millis = (jlong)(6.0f + (1.0f - sharp) * 10.0f);
    jobject effect = (*env)->CallStaticObjectMethod(env, g_vibration_effect, g_mid_create_one_shot, millis, (jint)amplitude);
    if (!effect) { (*env)->ExceptionClear(env); return; }
    (*env)->CallVoidMethod(env, g_vibrator, g_mid_vibrate_effect, effect);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, effect);
    atomic_fetch_add(&g_haptic_pulses, 1);
    if (getenv("DM_HAPTICDBG")) {
        static _Atomic int shown;
        int n = atomic_fetch_add(&shown, 1);
        if (n < 24 || n % 120 == 0)
            LOGI("HAPTIC apply #%d intensity=%.3f sharpness=%.3f amp=%d ms=%lld applies=%llu pulses=%llu",
                 n, intensity, sharp, amplitude, (long long)millis,
                 (unsigned long long)atomic_load(&g_haptic_applies),
                 (unsigned long long)atomic_load(&g_haptic_pulses));
    }
}

#include "ss_pointer.h"


/* 运行时与后端 */
int ss_host_run(const char *directory);
void dm_vk_set_window(void *native);
void dm_vk_set_display_hz(int hz);
void dm_android_set_window(void *native);

static atomic_int g_configured, g_started;
static char g_data_dir[1024];
static char g_modules_dir[1024];
static char g_files_dir[1024];       /* 应用内部目录：profile 放这里，属主一定是自己 */
static float g_refresh_hz = 60.0f;

/* ---- 触摸输入 ---------------------------------------------------------------- */
/* Java MotionEvent -> ss_pointer / ss_input_set。菜单/地图模式把位置事件交给适配器
 * （rh_menu.c 的指针目标命中逻辑）；有按钮目标时直接按对应 NpadButton；都没有时
 * 退化成「点哪都按 A」，保证能推进开场/对话。 */
static int g_surface_w = 3047, g_surface_h = 1440;
static uint64_t g_touch_token, g_touch_mask, g_key_mask;
static int g_touch_menu;
static ss_pointer_frame g_last_frame;
static int g_have_frame;
extern void ss_input_set(uint64_t buttons, int32_t lx, int32_t ly);
static void touch_publish_input(void) { ss_input_set(g_touch_mask | g_key_mask, 0, 0); }

/* 触摸手势：竖滑=方向键、点按=A。
 * Switch 游戏菜单原本用 D-Pad/A，触屏上没有等价输入；这里在 port 层加手势适配：
 *   - 按下后 25ms 内没有明显位移 -> 点按，发 A（保持到抬手）；
 *   - 位移超过 24px -> 滑动，改为按方向发 DPAD（每 70px 一步，手指上滑=光标下移）；
 *   - 滑动过程中绝不发 A，避免"一滑就打开菜单项"。
 * 需要按下即 A（节奏游戏更快响应）时设 DM_TOUCH_TAP_MS=0；完全关闭手势设 DM_TOUCH_SWIPE=0。 */
static uint64_t touch_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}
static _Atomic int g_gest_pending_a;
static _Atomic int g_gest_a_sent;
static _Atomic int g_gest_swiped;
static volatile uint64_t g_gest_down_ms;
static volatile float g_gest_ax, g_gest_ay;
static volatile uint64_t g_gest_dpad_until;
static void gest_emit_a(void) {
    if (atomic_exchange(&g_gest_a_sent, 1)) return;
    g_touch_mask |= 1ull; touch_publish_input();
}
static void gest_emit_dpad(int bit) {
    g_touch_mask = (g_touch_mask & ~((1ull<<12)|(1ull<<13)|(1ull<<14)|(1ull<<15))) | (1ull << bit);
    touch_publish_input();
    g_gest_dpad_until = touch_now_ms() + 40;
}
static void *gesture_thread(void *unused) {
    (void)unused;
    for (;;) {
        usleep(3000);
        uint64_t now = touch_now_ms();
        long delay = getenv("DM_TOUCH_TAP_MS") ? atol(getenv("DM_TOUCH_TAP_MS")) : 60;
        if (atomic_load(&g_gest_pending_a) && !atomic_load(&g_gest_a_sent) &&
            !atomic_load(&g_gest_swiped) && now >= g_gest_down_ms + (uint64_t)delay)
            gest_emit_a();
        if (g_gest_dpad_until && now >= g_gest_dpad_until) {
            g_touch_mask &= ~((1ull<<12)|(1ull<<13)|(1ull<<14)|(1ull<<15));
            touch_publish_input(); g_gest_dpad_until = 0;
        }
    }
    return NULL;
}
static void gesture_init_once(void) {
    static int started;
    if (started) return;
    started = 1;
    pthread_t thread;
    if (!pthread_create(&thread, NULL, gesture_thread, NULL)) pthread_detach(thread);
}
static void touch_cancel(void) {
    if (g_touch_menu) { ss_pointer_push((ss_pointer_event){g_touch_token, SS_POINTER_CANCEL, 0, 0}); g_touch_menu = 0; }
    atomic_store(&g_gest_pending_a, 0);
    g_touch_mask = 0; touch_publish_input();
}
/* 场景触摸提示：rh_game.c 会按当前画面调用 ss_touch_left_set()，这里把打包值交给 Java。 */
JNIEXPORT jlong JNICALL
Java_com_rhport_rhythm_GameActivity_nativeTouchHint(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    return (jlong)ss_touch_left_get();
}

JNIEXPORT void JNICALL
Java_com_rhport_rhythm_GameActivity_nativeTouch(JNIEnv *env, jclass cls, jint id, jfloat x, jfloat y, jint action) {
    (void)env; (void)cls; (void)id;
    gesture_init_once();
    ss_pointer_frame frame;
    if (ss_pointer_read(&frame)) { g_last_frame = frame; g_have_frame = 1; }
    else frame = g_last_frame;
    if (g_have_frame && frame.token != g_touch_token) { touch_cancel(); g_touch_token = frame.token; }
    float gx = 0, gy = 0;
    int inside = ss_pointer_location(g_surface_w, g_surface_h, x, y, &gx, &gy);
    if (action == 0) {                                   /* DOWN */
        if (!inside) return;
        int target = g_have_frame ? ss_pointer_hit(&frame, gx, gy) : -1;
        if (target >= 0 && (frame.targets[target].flags & SS_POINTER_BUTTON)) {
            int bit = frame.targets[target].button;
            if (bit >= 0 && bit < 64) { g_touch_mask |= 1ull << bit; touch_publish_input(); }
        } else if (!g_touch_menu && g_have_frame && (frame.mode == SS_POINTER_MENU || frame.mode == SS_POINTER_MAP)) {
            g_touch_menu = 1;
            ss_pointer_push((ss_pointer_event){g_touch_token, SS_POINTER_DOWN, gx, gy});
        } else if (!g_touch_menu) {
            const char *swipe = getenv("DM_TOUCH_SWIPE");
            if (!swipe || !atoi(swipe)) {                /* 默认：按下即 A（虚拟按键/D-Pad 主导） */
                g_touch_mask = 1ull; touch_publish_input();
            } else {
                atomic_store(&g_gest_a_sent, 0);
                atomic_store(&g_gest_swiped, 0);
                g_gest_ax = x; g_gest_ay = y;
                g_gest_down_ms = touch_now_ms();
                atomic_store(&g_gest_pending_a, 1);
            }
        }
    } else if (action == 1) {                             /* MOVE */
        if (g_touch_menu && inside) { ss_pointer_push((ss_pointer_event){g_touch_token, SS_POINTER_MOVE, gx, gy}); return; }
        if (!atomic_load(&g_gest_pending_a)) return;
        float dx = x - g_gest_ax, dy = y - g_gest_ay;
        if (!atomic_load(&g_gest_swiped) &&
            (dx > 24.0f || dx < -24.0f || dy > 24.0f || dy < -24.0f)) {
            atomic_store(&g_gest_swiped, 1);
            g_gest_ax = x; g_gest_ay = y;
        }
        if (atomic_load(&g_gest_swiped)) {
            dx = x - g_gest_ax; dy = y - g_gest_ay;
            int bit = -1;
            if (dy <= -70.0f) bit = 15;        /* 手指上滑 -> DPAD Down */
            else if (dy >= 70.0f) bit = 13;    /* 手指下滑 -> DPAD Up */
            else if (dx <= -70.0f) bit = 14;   /* 手指左滑 -> DPAD Right */
            else if (dx >= 70.0f) bit = 12;    /* 手指右滑 -> DPAD Left */
            if (bit >= 0) { gest_emit_dpad(bit); g_gest_ax = x; g_gest_ay = y; }
        }
    } else if (action == 2) {                             /* UP */
        if (g_touch_menu) { ss_pointer_push((ss_pointer_event){g_touch_token, SS_POINTER_UP, gx, gy}); g_touch_menu = 0; }
        if (atomic_load(&g_gest_pending_a) && !atomic_load(&g_gest_a_sent) && !atomic_load(&g_gest_swiped))
            gest_emit_a();                                /* 快速点按：抬手时补发 A */
        atomic_store(&g_gest_pending_a, 0);
        g_touch_mask = 0; touch_publish_input();
    } else {                                              /* CANCEL */
        touch_cancel();
    }
}

/* Android 按键 -> NpadButton：方便 adb shell input keyevent 直接控制菜单/校准。
 * DPAD 上下左右 + CENTER/ENTER/SPACE=A；游戏手柄键位按标准映射。 */
static int key_bit_for(jint code) {
    switch (code) {
        case 19: return 13;   /* KEYCODE_DPAD_UP    -> Up */
        case 20: return 15;   /* KEYCODE_DPAD_DOWN  -> Down */
        case 21: return 12;   /* KEYCODE_DPAD_LEFT  -> Left */
        case 22: return 14;   /* KEYCODE_DPAD_RIGHT -> Right */
        case 23:              /* KEYCODE_DPAD_CENTER */
        case 66:              /* KEYCODE_ENTER */
        case 62:              /* KEYCODE_SPACE */
        case 96: return 0;    /* BUTTON_A -> A */
        case 97: return 1;    /* BUTTON_B -> B */
        case 99: return 2;    /* BUTTON_X -> X */
        case 100: return 3;   /* BUTTON_Y -> Y */
        case 102: return 6;   /* L1 -> L */
        case 103: return 7;   /* R1 -> R */
        case 104: return 8;   /* L2 -> ZL */
        case 105: return 9;   /* R2 -> ZR */
        case 108: return 10;  /* START -> Plus */
        case 109: return 11;  /* SELECT -> Minus */
        default: return -1;
    }
}
JNIEXPORT void JNICALL
Java_com_rhport_rhythm_GameActivity_nativeKey(JNIEnv *env, jclass cls, jint code, jboolean down) {
    (void)env; (void)cls;
    int bit = key_bit_for(code);
    if (bit < 0) return;
    uint64_t mask = 1ull << bit;
    if (down) g_key_mask |= mask; else g_key_mask &= ~mask;
    touch_publish_input();
}


/* 数据根下的这几个目录必须存在：hle_fs 挂不上就 die，游戏自己不会建 */
static int ensure_dir(const char *path) {
    char buffer[1024];
    snprintf(buffer, sizeof buffer, "%s", path);
    for (char *p = buffer + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(buffer, 0775) && errno != EEXIST) return errno;
        *p = '/';
    }
    if (mkdir(buffer, 0775) && errno != EEXIST) return errno;
    return 0;
}

/* <dataDir>/env.txt：一行一个 KEY=VALUE，用来临时打开宿主的调试开关。
 * 有了它，改诊断开关只改手机上的一个文本文件，不用重编 APK。 */
static void load_env_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return;
    char line[512];
    int count = 0;
    while (fgets(line, sizeof line, file)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '#' || *s == '\n' || *s == 0) continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = s, *value = eq + 1;
        char *end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        end = value + strlen(value);
        while (end > value && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = 0;
        if (*key) { setenv(key, value, 1); LOGI("env: %s=%s", key, value); count++; }
    }
    fclose(file);
    if (!count) remove(path);
}

/* ---- 配置：记目录、建子目录、把 hle_fs 要的三个挂载点指过去 ------------------ */

JNIEXPORT jint JNICALL
Java_com_rhport_rhythm_GameActivity_nativeConfigure(JNIEnv *env, jclass cls, jstring data_dir, jstring files_dir, jfloat hz) {
    (void)cls;
    if (atomic_exchange(&g_configured, 1)) return EALREADY;
    if (!data_dir) return EINVAL;
    const char *s = (*env)->GetStringUTFChars(env, data_dir, NULL);
    if (!s) return ENOMEM;
    int rc = strlen(s) >= sizeof g_data_dir ? ENAMETOOLONG : 0;
    if (!rc) snprintf(g_data_dir, sizeof g_data_dir, "%s", s);
    (*env)->ReleaseStringUTFChars(env, data_dir, s);
    if (!rc && files_dir) {
        const char *f = (*env)->GetStringUTFChars(env, files_dir, NULL);
        if (f) {
            if (strlen(f) < sizeof g_files_dir) snprintf(g_files_dir, sizeof g_files_dir, "%s", f);
            else rc = ENAMETOOLONG;
            (*env)->ReleaseStringUTFChars(env, files_dir, f);
        }
    }
    if (rc) { atomic_store(&g_configured, 0); return rc; }

    g_refresh_hz = hz > 1.0f ? hz : 60.0f;

    snprintf(g_modules_dir, sizeof g_modules_dir, "%s/modules", g_data_dir);
    char romfs[1024], save[1024], temp[1024];
    snprintf(romfs, sizeof romfs, "%s/romfs", g_data_dir);
    snprintf(save, sizeof save, "%s/save", g_data_dir);
    snprintf(temp, sizeof temp, "%s/temp", g_data_dir);
    if ((rc = ensure_dir(g_modules_dir)) || (rc = ensure_dir(romfs)) ||
        (rc = ensure_dir(save)) || (rc = ensure_dir(temp))) {
        LOGE("建目录失败 rc=%d (%s)", rc, strerror(rc));
        atomic_store(&g_configured, 0);
        return rc;
    }

    /* hle_fs 的三个挂载点（它只认环境变量；默认值是 D2R 的路径） */
    /* 单文件 RomFS 镜像优先：<rhdata>/romfs.img（电脑端从本体 NCA 提取，1 个文件代替 7000+ 小文件）。
       挂载后 hle_fs 的所有 romfs 读写都走镜像（view root 只有目录树）；DM_ROMFS_IMAGE=0 可强制用目录。 */
    {
        char image[1100], view[1100];
        snprintf(image, sizeof image, "%s/romfs.img", g_data_dir);
        snprintf(view, sizeof view, "%s/romfs-view", g_data_dir);
        struct stat st;
        const char *off = getenv("DM_ROMFS_IMAGE");
        if (!(off && off[0] == '0' && !off[1]) && !stat(image, &st) && S_ISREG(st.st_mode) && st.st_size > 80) {
            extern int romfs_image_setup_checked(const char *source, const char *root, uint64_t expected_bytes);
            if (!romfs_image_setup_checked(image, view, 0)) {
                LOGI("ROMFS image mounted: %s (%lld bytes), view=%s", image, (long long)st.st_size, view);
                snprintf(romfs, sizeof romfs, "%s", view);   /* 镜像模式：挂载点改成 view root */
            } else {
                LOGE("ROMFS image setup failed errno=%d (%s); fallback to directory", errno, strerror(errno));
            }
        }
    }
    setenv("DM_ROMFS", romfs, 1);
    setenv("DM_SAVE", save, 1);
    setenv("DM_TEMP", temp, 1);

    /* 宿主的 LOG() 写 stderr，而 Android 上 stderr 是黑洞：转到数据目录里的文件，
     * adb 直接能读（不依赖 run-as）。用无缓冲写，崩溃时最后几行也留得住。 */
    {
        char log_path[1100];
        snprintf(log_path, sizeof log_path, "%s/run.log", g_data_dir);
        int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) close(fd);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            LOGI("stdout/stderr -> %s", log_path);
        } else {
            LOGE("打不开日志文件 %s (%s)", log_path, strerror(errno));
        }
    }

    /* sprite batcher 的跳等优化这一版不启用：nvn_hook 返回 NULL 时它会自己退回
     * 「保留等待」，显式置 1 只是省掉那次版本比对和一条容易误读的红字日志。 */
    setenv("RH_BATCH_WAIT", "1", 0);
    /* 菜单适配层里有个虚表校验：main+0xd0bd18 期望是重定位过的函数指针，但这个槽
     * 既不在 DT_RELR 也不在 DT_RELA 里（同组另外三个槽都在 RELR、已通过），说明
     * 它在本版本里不是可重定位指针。先按模块自带的开关跳过菜单适配，把启动链跑通。 */
    setenv("RH_POINTER_DISABLE", "1", 0);

    /* 手机上的 rhdata/env.txt 可以覆盖上面的默认值（诊断开关用，见文件头注释） */
    {
        char env_path[1100];
        snprintf(env_path, sizeof env_path, "%s/env.txt", g_data_dir);
        load_env_file(env_path);
    }
    /* 出图节拍：目标 60 帧；面板是 120 Hz 时由 present 层按整数分之一取档。 */
    setenv("DM_PRESENT_HZ", "60", 0);

    LOGI("configure root=%s refresh=%.1f Hz", g_data_dir, (double)g_refresh_hz);
    return 0;
}

/* ---- 窗口 ------------------------------------------------------------------- */

JNIEXPORT jint JNICALL
Java_com_rhport_rhythm_GameActivity_nativeSurface(JNIEnv *env, jclass cls, jobject surface) {
    (void)cls;
    ANativeWindow *next = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (surface && !next) return EINVAL;

    /* 先摘掉旧窗口，再挂新的：出图线程不能拿到已经销毁的窗口 */
    if (!next) dm_vk_set_window(NULL);
    dm_android_set_window(next);          /* 自己持一份引用，供 hle_vi 查询 */
    if (next) {
        dm_vk_set_window(next);
        dm_vk_set_display_hz((int)(g_refresh_hz + 0.5f));
        g_surface_w = ANativeWindow_getWidth(next); g_surface_h = ANativeWindow_getHeight(next);
        LOGI("surface %dx%d refresh=%d", g_surface_w, g_surface_h, (int)(g_refresh_hz + 0.5f));
        ANativeWindow_release(next);      /* 丢掉 fromSurface 那一份，全局里还留着一份 */
    } else {
        LOGI("surface destroyed");
    }
    return 0;
}

/* ---- 起游戏线程 -------------------------------------------------------------- */

/* 平台层要求的两个回调：绑定时只检查是否给了，不要求真有实现。
 * 第一帧阶段把它们做成空操作即可；接暂停/恢复时再挂到游戏循环上。 */
static int suspend_graphics(int suspend) {
    (void)suspend;
    return 0;
}

static int pause_audio(int pause) {
    (void)pause;
    return 0;
}

static void *game_thread(void *unused) {
    (void)unused;
    LOGI("game thread start, modules=%s", g_modules_dir);
    /* 宿主镜像基址：崩溃日志里的 PC 减去它就是 .so 内的偏移，可直接 addr2line */
    Dl_info info;
    if (dladdr((void *)ss_host_run, &info) && info.dli_fbase) {
        LOGI("host base=%p ss_host_run=%p offset=0x%lx", info.dli_fbase, info.dli_saddr,
             (unsigned long)((char *)info.dli_saddr - (char *)info.dli_fbase));
    }

    /* 平台层先绑定再注册服务：nninitInitializeSdkModule 会按名字要 oe/aoc/fs… 的后端，
     * 注册表是空的就直接 die（"Android service backend missing: oe"）。 */
    char profile[1100], temp[1100];
    snprintf(profile, sizeof profile, "%s/profile", g_files_dir[0] ? g_files_dir : g_data_dir);
    snprintf(temp, sizeof temp, "%s/temp", g_data_dir);
    dm_android_platform_config config = {
        .mode = DM_ANDROID_HOST_ACTIVITY,
        .profile_directory = profile,
        .locale = "zh-Hans",
        .locale_source = "hardcoded",
        .content_manifest_verified = 1,
        .prefetch_present = 0,
        .audio_present = 1,
        .graphics_present = 1,
        .suspend_and_drain_graphics = suspend_graphics,
        .pause_audio = pause_audio,
    };
    int rc = dm_android_platform_bind(&config);
    if (rc) LOGE("platform bind rc=%d", rc);
    rc = dm_android_platform_register_services();
    if (rc) LOGE("platform register services rc=%d", rc);
    dm_android_service_register(DM_ANDROID_SERVICE_FS, dm_android_fs_initialize, NULL);

    dm_android_window_state window = {
        .focus = 1, .surface_available = 1,
        .width = 3047, .height = 1440, .density = 3.5f,
        .refresh_hz = g_refresh_hz, .thermal_status = -1, .generation = 1,
    };
    rc = dm_android_platform_update_window(&window);
    if (rc) LOGE("platform update window rc=%d", rc);

    rc = ss_host_run(g_modules_dir);
    LOGI("ss_host_run returned %d", rc);
    return NULL;
}

JNIEXPORT jint JNICALL
Java_com_rhport_rhythm_GameActivity_nativeStart(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    if (!atomic_load(&g_configured)) return ENODEV;
    if (!g_data_dir[0]) return ENODEV;
    if (atomic_exchange(&g_started, 1)) return EALREADY;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);   /* 改写后的游戏代码吃栈 */
    pthread_t worker;
    int error = pthread_create(&worker, &attr, game_thread, NULL);
    pthread_attr_destroy(&attr);
    if (error) { atomic_store(&g_started, 0); return error; }
    pthread_detach(worker);
    return 0;
}

JNIEXPORT jstring JNICALL
Java_com_rhport_rhythm_GameActivity_nativeVersion(JNIEnv *env, jclass cls) {
    (void)cls;
    char text[128];
    snprintf(text, sizeof text, "rh-port %s", "0.1");
    return (*env)->NewStringUTF(env, text);
}
