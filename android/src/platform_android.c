/* 节奏天国 · Android 平台层：vickwv 的 vk 后端与 Touchable 的 ss_ 运行时之间那层薄封装。
 *
 * 这里只放「后端要、平台必须给、且与游戏无关」的那几个符号。真正的 Android 宿主
 * （JNI、ANativeWindow 生命周期、触摸、音频）在 app_android.c 里，本文件不碰 JNI。
 */
#include <android/native_window.h>
#include <pthread.h>

/* ---- 窗口：由 JNI 侧在 surfaceCreated / surfaceDestroyed 时设置 ---------------- */

static ANativeWindow *g_window;
static pthread_mutex_t g_window_lock = PTHREAD_MUTEX_INITIALIZER;

void *dm_android_native_window(void) {
    pthread_mutex_lock(&g_window_lock);
    ANativeWindow *window = g_window;
    pthread_mutex_unlock(&g_window_lock);
    return window;
}

/* JNI 调用：接管一个 ANativeWindow（传 NULL 表示窗口销毁）。acquire 后旧的才 release，
 * 避免后端在出图线程里用到已经销毁的窗口。 */
void dm_android_set_window(void *native) {
    ANativeWindow *next = (ANativeWindow *)native;
    if (next) ANativeWindow_acquire(next);
    pthread_mutex_lock(&g_window_lock);
    ANativeWindow *old = g_window;
    g_window = next;
    pthread_mutex_unlock(&g_window_lock);
    if (old) ANativeWindow_release(old);
}

/* ---- 出图计数：vickwv 的 present.inc 每帧调，用来算帧间隔 / CPU 占用 ------------- */

void dm_android_nvn_presented(uint64_t frame) {
    (void)frame;                       /* 先不统计；需要性能浮层时再接 dm_android_nvn_work_* */
}

/* ---- 截图：宿主带的 DM_NVN_SHOT_UI 用，vk 后端暂未实现 ------------------------ */

void dm_gfx_shot_at(long k, const char *path) {
    (void)k;
    (void)path;
}

/* ---- 昼夜加亮：D2R 的 dm_bright.c 提供强定义；节奏天国不需要，恒 1.0（原样） ---- */

float dm_bright_k(void) {
    return 1.0f;
}

/* ---- NVN 函数包装：rh_batch.c 用来接管 nvnQueueFinish / nvnSyncWait ------------
 *
 * 语义实现见 hle_nvn.c 的槽位表（nvn_slot[i].fn，跳板按 i*16 寻址）。第一帧阶段
 * 返回 NULL 即可：rh_batch.c 在装钩子前会先比对该版本的代码字节与虚表，任何一项
 * 不符就整段跳过、保留 GPU 等待；此外 RH_BATCH_WAIT=1 也能直接短路。
 * 等画面出来、要追「CPU 领先 GPU 一帧」这个性能项时，再照 Touchable 的原实现接槽位。 */

void *nvn_hook(const char *name, void *function) {
    (void)name;
    (void)function;
    return 0;
}

/* ---- 出图节拍：ss_platform.c 用 nvn_present_hz 设目标帧率 --------------------
 * Android 侧由 nvn_platform_present.inc 的 nvn_present_config 统一管；
 * display_intervals = 0 表示「按整帧计」，与 iOS 上 hz 的语义一致。 */

void nvn_present_config(int hz, int display_intervals);

void nvn_present_hz(int hz) {
    nvn_present_config(hz, 0);
}

/* ---- 文件打开观察钩子 --------------------------------------------------------
 * rh_game.c 会把它设成自己的 rh_fs_open() 用来观察资源加载。上游（Touchable 补丁）
 * 是在 hle_fs.c 的 open 里回调它；这一版 FS 由 ss_fs 走 Android 平台层，暂未接回调，
 * 所以这里只提供符号，保证 rh_game.c 的赋值成立。等要看资源加载时再接到 ss_fs 的打开路径。 */
void (*g_dm_fs_open_hook)(const char *path);

/* ---- 诊断后端：abort-observer 的初始化只问「有没有落盘的地方」 ----------------
 * 上游 vickwv 由 dm/android/diagnostics.c 提供（D2R 的 HUD/异常帧统计用）。
 * 这边没有那份，但宿主的崩溃处理本来就把栈打到 rhdata/run.log，所以算"就绪"，
 * 让 abort-observer 能注册上；真正的崩溃日志走 ss_crash_install 那条路。 */
int dm_android_diagnostics_ready(void) {
    return 1;
}
