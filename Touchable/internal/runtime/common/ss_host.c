/* Shared Switch bootstrap. Game changes belong in each game adapter. */
#include "dm.h"
#include "ss_indirect_branch.h"
#include "ss_display.h"
#include "niten_game.h"
#include "niten_game_config.h"
#include "ss_haptics.h"
#include <errno.h>
#include <unistd.h>

struct dm_flags g_flag;
uint64_t g_t0_ns;
void guest_thread_init(void);
void ss_crash_install(void);
void ss_sample_install(int samples);
void ss_host_maps_init(void);
void ss_stall_watch_install(int seconds);
void dm_vk_set_shader_root(const char *path);
uint32_t svc_thread_main_init(void);
void stubs_report(void);

static void *required(const char *name) {
    void *symbol = guest_sym(name);
    if (!symbol) die("Missing boot symbol: %s", name);
    return symbol;
}

static void install_traps(const char *directory) {
    ss_trap_site sites[256];
    size_t count = 0;
    for (int i = 0; i < g_nmod; ++i) {
        module_t *module = &g_mod[i];
        char path[1024], line[256];
        snprintf(path, sizeof(path), "%s/%s.indirect.tsv", directory, module->name);
        FILE *file = fopen(path, "r");
        if (!file) {
            if (errno != ENOENT) die("Cannot open trap table: %s", path);
            continue;
        }
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == '#') continue;
            unsigned long trap, original;
            char kind[32], extra;
            if (sscanf(line, "%lx %lx %31s %c", &trap, &original, kind, &extra) != 3)
                die("Invalid trap table: %s", path);
            ss_trap_kind type = !strcmp(kind, "br_x18") ? SS_BR_X18 :
                !strcmp(kind, "blr_x18") ? SS_BLR_X18 :
                !strcmp(kind, "mov_sp_x18") ? SS_MOV_SP_X18 : 0;
            if (!type || count == 256 || trap < module->patch_off ||
                trap + 4 > module->size || original + 4 >= module->text_end)
                die("Trap table bounds: %s", path);
            sites[count++] = (ss_trap_site){(uintptr_t)module->base + trap,
                (uintptr_t)module->base + original + 4, type};
        }
        fclose(file);
    }
    int error = count ? ss_indirect_install(sites, count, g_key18) : 0;
    if (error) die("Cannot install instruction bridge: %s", strerror(error));
    LOG("%s instruction bridge: %zu sites", NITEN_GAME_ID, count);
}

static void call0(const char *name) {
    LOG("BOOT %s", name);
    ((void (*)(void))required(name))();
}

int ss_host_run(const char *directory) {
    g_t0_ns = now_ns();
    g_flag.svc = !!getenv("DM_SVC");
    g_flag.thr = !!getenv("DM_THR");
    g_flag.fs = !!getenv("DM_FS");
    g_flag.dbg = 1;
    g_flag.imports = !!getenv("DM_IMPORTS");
    g_flag.bootprof = 1;
    setenv("DM_APP_POOL_ADD_MB", "0", 0);
    ss_crash_install();
#if defined(__ANDROID__)
    /* 崩溃时能把宿主调用链（libdmandroid/libc）的返回地址打出来（HSTACK） */
    ss_host_maps_init();
    /* Vulkan 着色器缓存：<rhdata>/shaders（由 tools/make_vk_shader_cache.py 生成）。
     * ss_host_run 的 directory 是 <rhdata>/modules，所以取它的父目录再拼 shaders。
     * 必须在图形初始化前设置根路径；缺失时只提示，第一次 draw 会按后端原有逻辑报错。 */
    { char shader_root[1024];
      if (directory && snprintf(shader_root, sizeof shader_root, "%s", directory) < (int)sizeof shader_root) {
          char *slash = strrchr(shader_root, '/');
          if (slash && (size_t)(slash - shader_root) + 9 < sizeof shader_root) {
              strcpy(slash, "/shaders");
              if (access(shader_root, R_OK) == 0) dm_vk_set_shader_root(shader_root);
              else LOG("SHADER_ROOT missing: %s（先跑 tools/make_vk_shader_cache.py 并推送到设备）", shader_root);
          }
      } }
    /* DM_SAMPLE=N：用 ITIMER_PROF 每秒给正在吃 CPU 的线程打一次采样（PC/LR/栈），
     * 用来定位「卡住但没有任何日志」的忙等。默认关。 */
    { const char *samples = getenv("DM_SAMPLE"); if (samples) ss_sample_install(atoi(samples)); }
    /* DM_STALL=N：出图计数停 N 秒就 dump 一次客体线程现场（默认关；=1 用默认 8 秒）。 */
    { const char *stall = getenv("DM_STALL"); if (stall) ss_stall_watch_install(atoi(stall)); }
#endif
    const char *names[] = NITEN_GAME_MODULES;
    load_modules(directory, names, sizeof(names) / sizeof(names[0]));
    LOG("BOOT 阶段：模块已装载并重定位");
    niten_game_install();
    LOG("BOOT 阶段：%s 游戏补丁已装", NITEN_GAME_ID);
    install_traps(directory);
    LOG("BOOT 阶段：trap 表已装");
    stubs_report();
    LOG("BOOT 阶段：桩已报告");
    guest_thread_init();
    LOG("BOOT 阶段：guest 线程上下文已建");
    uint32_t handle = svc_thread_main_init();
    call0("_init_libc0");
    LOG("BOOT nnosInitialize");
    ((void (*)(uint64_t, uint64_t))required("nnosInitialize"))(handle, 0);
    call0("_init_libc1");
    call0("nninitInitializeSdkModule");
    call0("nninitStartup");
    call0("_init_libc2");
    run_module_inits();
    static char *arguments[] = {NITEN_GAME_PRODUCT, NULL};
    ((void (*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
    ((void (*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(arguments);
    call0("nnMain");
    LOG("%s nnMain returned", NITEN_GAME_ID);
    return 0;
}

#ifdef SS_HOST_CLI
void *front_metal_layer(void) { return NULL; }
#ifndef __ANDROID__
/* Android provides the real hook in ss_haptics_android.c (Android Vibrator backend). */
void ss_haptics_platform_apply(ss_haptic_output output) { (void)output; }
#endif
static void *input_file(void *argument) {
    const char *path = argument;
    void ss_input_set(uint64_t, int32_t, int32_t);
    uint64_t previous = 0; int old_x = 0, old_y = 0;
    for (;;) {
        FILE *file = fopen(path, "r");
        if (file) {
            unsigned long long mask; int x, y;
            if (fscanf(file, "%llx %d %d", &mask, &x, &y) == 3 &&
                (mask != previous || x != old_x || y != old_y)) {
                ss_input_set(mask, x, y); previous = mask; old_x = x; old_y = y;
                LOG("TEST input mask=%#llx stick=%d,%d", mask, x, y);
            }
            fclose(file);
        }
        usleep(10000);
    }
}
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: nitenphone-host MODULE_DIRECTORY\n"); return 2; }
    const char *view = getenv("SS_VIEW_SIZE");
    double width, height;
    if (view && sscanf(view, "%lfx%lf", &width, &height) == 2) ss_display_set_view(width, height);
    const char *path = getenv("SS_INPUT_FILE");
    if (path) { pthread_t thread; if (pthread_create(&thread, NULL, input_file, (void *)path)) die("Cannot start test input"); pthread_detach(thread); }
    return ss_host_run(argv[1]);
}
#endif
