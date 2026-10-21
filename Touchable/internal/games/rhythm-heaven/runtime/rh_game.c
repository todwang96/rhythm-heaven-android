/* Rhythm Heaven Groove adapter. Fixed-address changes belong here and must check the pinned module. */
#include "dm.h"
#include "niten_game.h"
#include <pthread.h>
#include <unistd.h>
#include "rh_touch.h"
#include "rh_menu.h"
#include "rh_batch.h"
#include "ss_touch.h"

/* The left half of the phone screen follows the current scene (see rh_touch.c). */
static pthread_mutex_t touch_lock = PTHREAD_MUTEX_INITIALIZER;
static rh_touch_state touch_state;
static rh_touch_hint last_hint = {SS_TOUCH_MENU, 0, 0, 0};
static int have_last_hint;
static void rh_fs_open(const char *path) {
    rh_touch_hint hint;
    pthread_mutex_lock(&touch_lock);
    int changed = rh_touch_path(&touch_state, path, &hint);
    pthread_mutex_unlock(&touch_lock);
    if (!changed) return;
    last_hint = hint; have_last_hint = 1;
    ss_touch_left_set(hint.layout, hint.first, hint.second, hint.extra);
    LOG("Touch left area: layout %d buttons %d %d extra %#x (%s)", hint.layout, hint.first, hint.second, hint.extra, path);
}

/* 暂停菜单关掉后恢复最近一次场景提示（供 rh_menu.c 调用）。 */
void rh_touch_replay_last(void) {
    if (!have_last_hint) return;
    ss_touch_left_set(last_hint.layout, last_hint.first, last_hint.second, last_hint.extra);
}

/* Development input for on-device tests. RH_INPUT_FILE names a file holding "mask x y" (hex button
   mask, stick values) that is polled every 10 ms, like SS_INPUT_FILE on the Mac host. A relative
   path is under $HOME, the app container on iOS. Normal runs leave it unset. */
static void *rh_input_file(void *argument) {
    const char *path = argument;
    void ss_input_set(uint64_t, int32_t, int32_t);
    unsigned long long previous = ~0ull; int old_x = 0, old_y = 0;
    for (;;) {
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned long long mask; int x, y;
            if (fscanf(file, "%llx %d %d", &mask, &x, &y) == 3 && (mask != previous || x != old_x || y != old_y)) {
                ss_input_set(mask, x, y); previous = mask; old_x = x; old_y = y;
                LOG("TEST input mask=%#llx stick=%d,%d", mask, x, y);
            }
            fclose(file);
        }
        usleep(10000);
    }
}

static void rh_start_input_file(void) {
    const char *name = getenv("RH_INPUT_FILE"), *home = getenv("HOME");
    if (!name || !*name) return;
    static char path[1024];
    if (name[0] == '/' || !home) snprintf(path, sizeof path, "%s", name);
    else snprintf(path, sizeof path, "%s/%s", home, name);
    pthread_t thread;
    if (pthread_create(&thread, NULL, rh_input_file, path)) die("Cannot start the development input reader");
    pthread_detach(thread);
    LOG("Development input file %s", path);
}

/* The game draws its scenes at one of two sizes: handheld (1280x720) or docked (1920x1080).
   main+0xa764 stores GetPerformanceMode() == 1 in a flag, and the size getters at main+0xab50 and
   main+0xab90 select the docked pair from the table at main+0xada6d0 when that flag is set.
   Boost mode only changes the draw size; the operation mode (read at main+0x20d0) stays handheld.
   Set RH_HANDHELD_SIZE=1 to keep 1280x720. */
static void rh_select_draw_size(const module_t *main) {
    static const uint32_t expected[4] = {1280, 720, 1920, 1080};
    /* RH_DRAW_SIZE=WxH：把尺寸表的两组都改成 WxH（游戏内部分辨率），再走 docked/boost 模式。
     * 分辨率越低 GPU 负载越小、帧率越稳；升采样由呈现层完成。
     * 允许比 1920 更宽（宽屏不拉伸实验）：上限 2560x1440。 */
    const char *draw_size = getenv("RH_DRAW_SIZE");
    if (draw_size && *draw_size) {
        int w = 0, h = 0;
        if (sscanf(draw_size, "%dx%d", &w, &h) == 2 && w >= 640 && h >= 360 && w <= 2560 && h <= 1440) {
            uint32_t table_values[4] = {(uint32_t)w, (uint32_t)h, (uint32_t)w, (uint32_t)h};
            const uint64_t table = 0xada6d0;
            if (table >= main->ro_off && table + sizeof table_values <= main->data_off) {
                uint8_t *at = (uint8_t *)main->base + table;
                memcpy(at, table_values, sizeof table_values);
                extern int ss_performance_mode;
                ss_performance_mode = 1;
                LOG("Draw size: %dx%d (RH_DRAW_SIZE)", w, h);
                return;
            }
        }
    }
    const uint64_t table = 0xada6d0;
    const char *handheld = getenv("RH_HANDHELD_SIZE");
    if (handheld && *handheld && strcmp(handheld, "0")) {
        LOG("Draw size: handheld 1280x720 (RH_HANDHELD_SIZE)");
        return;
    }
    if (table < main->ro_off || table + sizeof expected > main->data_off
        || memcmp(main->base + table, expected, sizeof expected)) {
        LOG("Draw size: the size table at main+%#llx does not match; keep handheld mode", (unsigned long long)table);
        return;
    }
    extern int ss_performance_mode;
    ss_performance_mode = 1;
    LOG("Draw size: docked 1920x1080 (boost performance mode)");
}

void niten_game_install(void) {
    module_t *main = NULL;
    for (int i = 0; i < g_nmod; ++i)
        if (!strcmp(g_mod[i].name, "main")) main = &g_mod[i];
    if (!main) die("Rhythm Heaven adapter cannot find the main module");
    extern void (*g_dm_fs_open_hook)(const char *path);
    g_dm_fs_open_hook = rh_fs_open;
    rh_select_draw_size(main);
    rh_menu_install(main);
    rh_batch_install(main);
    rh_start_input_file();
}

int niten_game_host_call(int number, uint64_t *registers) {
    (void)number; (void)registers;
    return 0;
}
