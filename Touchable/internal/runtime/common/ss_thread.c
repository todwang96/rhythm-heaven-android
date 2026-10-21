#include "dm.h"
#include <signal.h>
#include <unistd.h>

extern void dm_gate(void);
#ifdef __ANDROID__
/* Android 的 guest TLS：pthread 槽只是"存储"，guest 读的是 dm_android_slots() 那个数组
 * （指针放在 bionic TCB 的应用槽 2，跳板按 tpidr_el0+0x10 取）。改完 pthread 槽必须
 * 同步一次，否则 guest 读到 0，经跳板间接调用就是 PC=0。 */
void dm_android_slots_sync(void);
#define GUEST_SLOTS_SYNC() dm_android_slots_sync()
#else
#define GUEST_SLOTS_SYNC() ((void)0)
#endif
static pthread_key_t allocation_key;
static pthread_once_t allocation_once = PTHREAD_ONCE_INIT;
void crash_thread_init(void);
static void free_state(void *pointer) {
    void **state = pointer;
    /* TLS / TLR 两个块在 Android 上由 dm/android/thread_slots.c 的 guest-slot context 析构
     * 统一释放（它在 dm_android_slots_sync() 时记下两个块的基址，并跳过 TLR 块内部那个
     * 合并布局的线程对象指针）。这里只释放指针数组本身——否则两个 pthread key 的析构
     * 会对同一批块各 free 一次，Scudo 报 "corrupted chunk header"/double free。
     * On Android the TLS / TLR blocks are freed by dm/android/thread_slots.c's guest-slot
     * context destructor (it records both block bases at dm_android_slots_sync() and skips the
     * merged-layout interior thread-object pointer). Only the state array itself is freed here;
     * otherwise the two pthread key destructors free the same blocks twice and Scudo reports
     * "corrupted chunk header"/double free. */
    free(state);
}
static void create_key(void) {
    if (pthread_key_create(&allocation_key, free_state)) die("Cannot create guest state key");
}
void guest_thread_init(void) {
    crash_thread_init();
    pthread_once(&allocation_once, create_key);
    if (pthread_getspecific(allocation_key)) {
        if (getenv("DM_THREADDBG")) LOG("THREADDBG guest_thread_init(reentry) tid=%ld pt=%ld tp=%p", (long)gettid(), (long)pthread_self(), (void *)__builtin_thread_pointer());
        GUEST_SLOTS_SYNC();
        return;
    }
    void **state = calloc(2, sizeof(void *));
    if (!state) die("Cannot allocate guest thread state");
    state[0] = calloc(1, 8192);
    uint8_t *region = calloc(1, 0x600);
    state[1] = region;
    if (!state[0] || !state[1]) die("Cannot allocate guest TLS");
    *(void **)(region + 0x1f8) = region + 0x200;
    if (pthread_setspecific(allocation_key, state) ||
        pthread_setspecific(g_key18, NULL) ||
        pthread_setspecific(g_keygate, (void *)dm_gate) ||
        pthread_setspecific(g_keytls, state[0]) ||
        pthread_setspecific(g_keytlr, region)) die("Cannot initialize guest thread slots");
    if (getenv("DM_THREADDBG")) LOG("THREADDBG guest_thread_init(new) tid=%ld pt=%ld tp=%p tls=%p tlr=%p", (long)gettid(), (long)pthread_self(), (void *)__builtin_thread_pointer(), state[0], region);
    GUEST_SLOTS_SYNC();
#ifdef __ANDROID__
    { extern uintptr_t *dm_android_slots(void); uintptr_t *s = dm_android_slots();
      if (s) LOG("guest 槽初始化：K18=%#lx KTLS=%#lx KTLR=%#lx KGATE=%#lx",
                 (unsigned long)s[1], (unsigned long)s[2], (unsigned long)s[3], (unsigned long)s[4]); }
#endif
}
