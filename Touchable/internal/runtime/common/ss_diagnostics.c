#include "dm.h"
#include "niten_game.h"
#include <signal.h>
#include <sys/ucontext.h>
#include <mach/arm/thread_status.h>
#include <mach/mach.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#if defined(__ANDROID__)
#include <unwind.h>
#include <sys/syscall.h>
#include <time.h>
#include <pthread.h>
#endif

static _Atomic(int (*)(void *, int, void *)) fault_hook;
static _Atomic(void (*)(void *, size_t)) write_hook;
static void (*resource_report)(uint32_t, const char *);
static void (*wait_dump)(void);
void dm_fault_hook_register(int (*function)(void *, int, void *)) { atomic_store(&fault_hook, function); }
void dm_guest_write_prep_register(void (*function)(void *, size_t)) { atomic_store(&write_hook, function); }
void dm_guest_write_prepare(void *address, size_t size) {
    void (*function)(void *, size_t) = atomic_load(&write_hook);
    if (function) function(address, size);
}
void dm_res_report_register(void (*function)(uint32_t, const char *)) { resource_report = function; }
void (*dm_res_report_cb(void))(uint32_t, const char *) { return resource_report; }
void crash_set_wait_dump(void (*function)(void)) { wait_dump = function; }

static pthread_key_t signal_stack_key;
static pthread_once_t signal_stack_once = PTHREAD_ONCE_INIT;
static void free_signal_stack(void *pointer) { free(pointer); }
static void create_signal_key(void) {
    if (pthread_key_create(&signal_stack_key, free_signal_stack)) die("Cannot create signal stack key");
}
void crash_thread_init(void) {
    pthread_once(&signal_stack_once, create_signal_key);
    if (pthread_getspecific(signal_stack_key)) return;
    void *memory = malloc(64 * 1024);
    stack_t stack = {.ss_sp = memory, .ss_size = 64 * 1024};
    if (!memory || sigaltstack(&stack, NULL) || pthread_setspecific(signal_stack_key, memory))
        die("Cannot install guest signal stack");
}
static void describe_address(const char *label, uintptr_t address) {
    module_t *module = module_of((void *)address);
    LOG("%s %s+%#lx (%#lx)", label, module ? module->name : "host",
        module ? address - (uintptr_t)module->base : address, address);
}
void dm_backtrace(uint64_t frame) {
    for (int i = 0; frame && !(frame & 15) && i < 32; ++i) {
        uint64_t pair[2]; vm_size_t copied = 0;
        if (vm_read_overwrite(mach_task_self(), frame, sizeof(pair), (vm_address_t)pair, &copied) ||
            copied != sizeof(pair)) break;
        describe_address("frame", pair[1]);
        if (pair[0] <= frame || pair[0] - frame > 16 * 1024 * 1024) break;
        frame = pair[0];
    }
}
/* Diagnostic builds log a crash and stop. They never resume an invalid load. */
/* 瀹夸富鏄犲皠琛細宕╂簝鏃舵妸鏍堜笂钀藉湪銆屽涓诲彲鎵ц鏄犲皠銆嶉噷鐨勮繑鍥炲湴鍧€涔熸墦鍑烘潵锛坓uest 鏍堟壂鎻忓彧鐪?
 * 瀹綋妯″潡锛岃€岃繖娆?Scudo 鐨?abort 鏄涓诲爢鎹熷潖锛岃皟鐢ㄩ摼鍦?libdmandroid/libc 閲岋級銆?
 * Host mapping table: at crash time also print return addresses on the stack that fall inside
 * host executable mappings (the guest-module scan only sees guest modules, while this Scudo
 * abort is host heap corruption whose call chain lives in libdmandroid/libc). */
typedef struct { unsigned long long start, end, off; char name[64]; } ss_host_map;
static ss_host_map g_host_maps[256];
static int g_host_nmaps;
static const char *map_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
void ss_host_maps_init(void) {
    FILE *file = fopen("/proc/self/maps", "r");
    if (!file) return;
    char line[512];
    g_host_nmaps = 0;
    while (fgets(line, sizeof line, file) && g_host_nmaps < (int)(sizeof g_host_maps / sizeof *g_host_maps)) {
        unsigned long long start, end, off;
        char perms[8], path[256];
        path[0] = 0;
        if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %255[^\n]", &start, &end, perms, &off, path) < 4) continue;
        if (strchr(perms, 'x') == NULL || !path[0]) continue;
        while (path[0] == ' ') memmove(path, path + 1, strlen(path));
        snprintf(g_host_maps[g_host_nmaps].name, sizeof g_host_maps[g_host_nmaps].name, "%s", map_basename(path));
        g_host_maps[g_host_nmaps].start = start;
        g_host_maps[g_host_nmaps].end = end;
        g_host_maps[g_host_nmaps].off = off;
        LOG("HOSTMAP %s start=%#llx end=%#llx off=%#llx", g_host_maps[g_host_nmaps].name, start, end, off);
        g_host_nmaps++;
    }
    fclose(file);
}
static void ss_host_stack_note(uint64_t value, int slot) {
    for (int k = 0; k < g_host_nmaps; k++) {
        if (value < g_host_maps[k].start || value >= g_host_maps[k].end) continue;
        static int shown;
        if (shown++ < 48)
            LOG("HSTACK+%#04x %s+%#llx map=%#llx", slot * 8, g_host_maps[k].name,
                (unsigned long long)(value - g_host_maps[k].start), g_host_maps[k].start);
        return;
    }
}
#if defined(__ANDROID__)
/* Real backtrace attempt: bionic's signal trampoline carries CFI, so unwinding from the
 * handler usually continues into the interrupted context (needed for the Scudo abort:
 * the malloc/free caller chain). */
static _Unwind_Reason_Code crash_unwind_step(struct _Unwind_Context *context, void *argument) {
    int *count = (int *)argument;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(context);
    if (pc) {
        module_t *m = module_of((void *)pc);
        (*count)++;
        if (*count <= 48)
            LOG("UNWIND[%d] %s+%#lx abs=%#lx", *count, m ? m->name : "host",
                m ? (unsigned long)(pc - (uintptr_t)m->base) : (unsigned long)pc, (unsigned long)pc);
    }
    return *count > 48 ? _URC_END_OF_STACK : _URC_NO_REASON;
}
#endif
static void crash(int number, siginfo_t *info, void *context) {
    ucontext_t *uc = context;
    int (*function)(void *, int, void *) = atomic_load(&fault_hook);
#if defined(__ANDROID__)
    /* Linux aarch64 鐨?mcontext_t 没有 __es/__ss：ESR 涓嶄笅鍙戝埌鐢ㄦ埛鎬侊紝鍐?璇诲垽瀹氫氦鍥?fault hook 鑷繁澶勭悊銆?*/
    /* Android 不给用户态 ESR(WnR)，但只读保护页只会因写而缺页：先按写处理，
     * 让 nvn_dirty 的缺页钩子能解保护并重执行；再退回读语义。 */
    if ((number == SIGSEGV || number == SIGBUS) && function) {
        if (function(info->si_addr, 1, context)) return;
        if (function(info->si_addr, 0, context)) return;
    }
    LOG("CRASH signal=%d address=%p", number, info->si_addr);
    describe_address("PC", (uint64_t)uc->uc_mcontext.pc);
    describe_address("LR", (uint64_t)uc->uc_mcontext.regs[30]);
    describe_address("SP", (uint64_t)uc->uc_mcontext.sp);
    describe_address("FP", (uint64_t)uc->uc_mcontext.regs[29]);
    for (int i = 0; i < 29; ++i) LOG("x%d=%#llx", i, (unsigned long long)uc->uc_mcontext.regs[i]);
    /* guest TLS 妲斤細鏀瑰啓鍚庣殑浠ｇ爜鎶?x18 / tpidr / tpidrro / 门入口放在这里，
     * 鍊煎鏋滄槸灏忔暟瀛楋紙< 16 MB锛夎鏄庢嬁浜嗚８鍋忕Щ褰撴寚閽堬紝浼氳褰撳嚱鏁板湴鍧€璋冪敤銆?*/
    { extern uintptr_t *dm_android_slots(void);
      uintptr_t *slots = dm_android_slots();
      if (slots) for (int i = 0; i < 9; ++i)
          LOG("slot[%d]=%#llx", i, (unsigned long long)slots[i]); }
    LOG("CRASH tid=%ld", (long)syscall(SYS_gettid));
    { int unwind_count = 0; _Unwind_Backtrace(crash_unwind_step, &unwind_count); }
    /* 鏍堟壂鎻忥細鎵撳嵃銆岃惤鍦ㄦ煇涓浣撴ā鍧楅噷銆嶇殑 8 瀛楄妭锛堢矖绮掑害杩斿洖鍦板潃閾撅級锛?
     * 浠ュ強钀藉湪瀹夸富鍙墽琛屾槧灏勯噷鐨勮繑鍥炲湴鍧€锛圚STACK锛岀敤浜庡畾浣?Scudo 鐨?malloc/free 璋冪敤閾撅級銆?*/
    { uint64_t sp = uc->uc_mcontext.sp;
      const uint64_t *p = (const uint64_t *)sp;
      for (int i = 0; i < 512 && p; ++i) {
          uint64_t v = p[i];
          module_t *m = module_of((void *)v);
          if (m) LOG("stack+%#03x %s+%#lx", i * 8, m->name, (unsigned long)(v - (uintptr_t)m->base));
          else ss_host_stack_note(v, i);
      } }
    dm_backtrace((uint64_t)uc->uc_mcontext.regs[29]);
#else
    if ((number == SIGSEGV || number == SIGBUS) && function &&
        function(info->si_addr, (uc->uc_mcontext->__es.__esr >> 6) & 1, context)) return;
    LOG("CRASH signal=%d address=%p", number, info->si_addr);
    describe_address("PC", arm_thread_state64_get_pc(uc->uc_mcontext->__ss));
    describe_address("LR", arm_thread_state64_get_lr(uc->uc_mcontext->__ss));
    for (int i = 0; i < 29; ++i) LOG("x%d=%#llx", i, uc->uc_mcontext->__ss.__x[i]);
    dm_backtrace(arm_thread_state64_get_fp(uc->uc_mcontext->__ss));
#endif
    _exit(128 + number);
}
void ss_crash_install(void) {
    crash_thread_init();
    struct sigaction action = {0};
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    action.sa_sigaction = crash;
    sigemptyset(&action.sa_mask);
    int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i)
        if (sigaction(signals[i], &action, NULL)) die("Cannot install crash handler");
}

/* Optional upstream observation hooks are unused by this host. */
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
void dm_prof_mark(const char *stage) { LOG("BOOT %s", stage); }
void uimsg_subscribe(void (*callback)(const char *, const char *, const char *)) { (void)callback; }
void crash_watch_frame(void) {}
uint64_t svc_host_call(int number, uint64_t *registers) {
    if (niten_game_host_call(number, registers)) return 1;
    die("Unhandled game host call: %#x", number);
}

#if defined(__ANDROID__)
/* DM_SAMPLE=N：ITIMER_PROF 每个 CPU 绉掔粰姝ｅ湪杩愯鐨勭嚎绋嬫姇涓€娆?SIGPROF锛?
 * 鎵?PC/LR/帧链/鏍堟壂鎻忋€傜敤浜庡畾浣嶃€屾棤鏃ュ織銆佹棤 svc銆佺函蹇欑瓑銆嶇殑鍗＄偣銆?*/
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/uio.h>
static _Atomic int g_sample_left;
static int sample_read(uint64_t address, uint64_t *out, size_t size) {
    struct iovec local = {out, size}, remote = {(void *)(uintptr_t)address, size};
    return syscall(SYS_process_vm_readv, getpid(), &local, 1, &remote, 1, 0) == (ssize_t)size;
}
static void sample_show(const char *label, uint64_t address) {
    module_t *module = module_of((void *)(uintptr_t)address);
    if (!module) return;
    LOG("%s %s+%#lx", label, module->name, (unsigned long)(address - (uint64_t)(uintptr_t)module->base));
}
static void sample_signal(int number, siginfo_t *info, void *context) {
    (void)number; (void)info;
    ucontext_t *uc = context;
    if (atomic_fetch_sub(&g_sample_left, 1) <= 0) return;
    LOG("SAMPLE tid=%ld pc=%#llx lr=%#llx fp=%#llx sp=%#llx", (long)syscall(SYS_gettid),
        (unsigned long long)uc->uc_mcontext.pc, (unsigned long long)uc->uc_mcontext.regs[30],
        (unsigned long long)uc->uc_mcontext.regs[29], (unsigned long long)uc->uc_mcontext.sp);
    sample_show("SAMPLE_PC", (uint64_t)uc->uc_mcontext.pc);
    sample_show("SAMPLE_LR", (uint64_t)uc->uc_mcontext.regs[30]);
    uint64_t frame = uc->uc_mcontext.regs[29];
    for (int i = 0; frame && !(frame & 15) && i < 24; ++i) {
        uint64_t pair[2];
        if (!sample_read(frame, pair, sizeof pair)) break;
        sample_show("SAMPLE_FRAME", pair[1]);
        if (pair[0] <= frame) break;
        frame = pair[0];
    }
    uint64_t sp = uc->uc_mcontext.sp;
    for (int i = 0; i < 48; ++i) {
        uint64_t value;
        if (!sample_read(sp + (uint64_t)i * 8, &value, sizeof value)) break;
        module_t *module = module_of((void *)(uintptr_t)value);
        if (module) LOG("SAMPLE_STACK+%#03x %s+%#lx", i * 8, module->name,
                        (unsigned long)(value - (uint64_t)(uintptr_t)module->base));
    }
}
void ss_sample_install(int samples) {
    struct sigaction action = {0};
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    action.sa_sigaction = sample_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPROF, &action, NULL)) die("Cannot install sample handler");
    atomic_store(&g_sample_left, samples);
    struct itimerval timer = {{1, 0}, {1, 0}};
    if (setitimer(ITIMER_PROF, &timer, NULL)) die("Cannot start sample timer");
    LOG("SAMPLE enabled samples=%d", samples);
}
/* 卡顿看门狗：present 计数（ss_progress_tick，出图时调用）超过 N 秒没动，就导出一次客体
 * 线程现场（svc_thread_dump：各线程在等什么 + 最近一次 svc 的来处），最多 3 次。
 * Stall watchdog: when the present counter (ss_progress_tick, called on present) has not moved
 * for N seconds, dump the guest thread states once (svc_thread_dump: what each thread waits on
 * and where its last SVC came from), at most 3 times. */
static _Atomic unsigned long long g_progress;
void ss_progress_tick(void) { atomic_fetch_add(&g_progress, 1); }
static uint64_t progress_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}
static void *stall_watch(void *argument) {
    int limit = (int)(intptr_t)argument, dumps = 0;
    uint64_t start = progress_now_ms(), last_change = start;
    unsigned long long last = atomic_load(&g_progress);
    for (;;) {
        sleep(1);
        uint64_t now = progress_now_ms();
        unsigned long long progress = atomic_load(&g_progress);
        if (progress != last) { last = progress; last_change = now; continue; }
        if (now - start < (uint64_t)limit * 1000 || now - last_change < (uint64_t)limit * 1000 || dumps >= 3) continue;
        uint64_t waited = now - last_change;
        dumps++; last_change = now;
        LOG("STALL: 出图计数 %llu 已停 %llu ms，导出线程现场（第 %d 次）", progress, (unsigned long long)waited, dumps);
        extern void svc_thread_dump(void);
        svc_thread_dump();
    }
    return NULL;
}
void ss_stall_watch_install(int seconds) {
    if (seconds <= 0) seconds = 8;
    pthread_t thread;
    if (pthread_create(&thread, NULL, stall_watch, (void *)(intptr_t)seconds)) { LOG("STALL watchdog create failed"); return; }
    pthread_detach(thread);
    LOG("STALL watchdog enabled %d s", seconds);
}
#endif
