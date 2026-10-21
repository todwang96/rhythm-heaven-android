/* ss_indirect_branch 的 Android 替身。
 *
 * 原实现（Darwin arm64）处理“改写器没法静态消掉的 X18 间接访问”：苹果内核会改 x18 和 tpidr_el0，
 * 这些点只能靠 BRK 陷入后在信号处理里补。Linux/AArch64 不碰 EL0 的 x18——
 * x18 只是 ABI 约定保留，内核不会改它——所以这层机制在 Android 上不需要。
 *
 * 接口保留，实现为空：返回成功，让调用方继续。若之后发现某些点位确有需要，
 * 再按 Linux 的 SIGILL/SIGSEGV 语义补一个真正的版本。
 */
#include <stddef.h>
#include <pthread.h>

#include "ss_indirect_branch.h"

int ss_indirect_install(const ss_trap_site *sites, size_t count, pthread_key_t x18_key) {
    (void)sites; (void)count; (void)x18_key;
    return 0;
}

int ss_indirect_uninstall(void) { return 0; }
