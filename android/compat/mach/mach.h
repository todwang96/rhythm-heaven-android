/* mach/mach.h → pthread 等价物。任务/线程端口在 Linux 上没有对应概念，
 * 这里给出能编译且语义无害的替身：端口号统一为 0，deallocate 是空操作。 */
#pragma once
#include <stdint.h>
#include <pthread.h>
#include "mach/mach_time.h"

typedef int integer_t;
typedef unsigned int mach_port_t;
typedef int kern_return_t;

#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_INVALID_ARGUMENT 4
#define MACH_PORT_NULL 0
#define MACH_PORT_VALID(p) ((p) != MACH_PORT_NULL)

static inline mach_port_t mach_task_self(void) { return 0; }
static inline mach_port_t mach_thread_self(void) { return 0; }

/* 用 pthread_t 的比特位当端口号，保证同一线程拿到同一个值即可。 */
static inline mach_port_t mach_thread_np(pthread_t thread) {
    uintptr_t raw = 0;
    __builtin_memcpy(&raw, &thread, sizeof(raw) < sizeof(thread) ? sizeof(raw) : sizeof(thread));
    return (mach_port_t)(raw & 0xffffffffu);
}

static inline kern_return_t mach_port_deallocate(mach_port_t task, mach_port_t name) {
    (void)task; (void)name; return KERN_SUCCESS;
}

/* vm_read_overwrite / vm_write 在 Linux 上就是 memcpy：宿主与“客体”同址，没有独立地址空间。
 * 这一条是语义等价而非近似——DM 的客体内存本来就在本进程里。 */
#include <string.h>
typedef uintptr_t vm_address_t;
typedef size_t vm_size_t;
typedef int vm_prot_t;

#ifndef VM_PROT_READ
#define VM_PROT_READ 1
#define VM_PROT_WRITE 2
#endif

static inline kern_return_t vm_read_overwrite(mach_port_t task, vm_address_t address, vm_size_t size,
                                              vm_address_t data, vm_size_t *outsize) {
    (void)task;
    memcpy((void *)data, (const void *)address, (size_t)size);
    if (outsize) *outsize = size;
    return KERN_SUCCESS;
}

static inline kern_return_t vm_write(mach_port_t task, vm_address_t address, vm_address_t data, vm_size_t size) {
    (void)task;
    memcpy((void *)address, (const void *)data, (size_t)size);
    return KERN_SUCCESS;
}

static inline kern_return_t mach_vm_protect(mach_port_t task, vm_address_t address, vm_size_t size,
                                            int set_maximum, vm_prot_t protection) {
    (void)task; (void)address; (void)size; (void)set_maximum; (void)protection;
    return KERN_SUCCESS;
}

/* 线程 CPU 占用查询：Darwin 用 thread_info；Android 上直接回「不知道」。 */
typedef unsigned int mach_msg_type_number_t;
typedef struct { integer_t seconds; integer_t microseconds; } time_value_t;

typedef struct {
    integer_t suspend_count;
    integer_t run_state;
    integer_t flags;
    integer_t policy;
    time_value_t user_time, system_time;
} thread_basic_info_data_t, *thread_basic_info_t;

#define THREAD_BASIC_INFO 3
#define THREAD_BASIC_INFO_COUNT (sizeof(thread_basic_info_data_t) / sizeof(unsigned int))

static inline kern_return_t thread_info(mach_port_t thread, int flavor, void *info, mach_msg_type_number_t *count) {
    (void)thread; (void)flavor; (void)info; (void)count;
    return 5;   /* KERN_FAILURE：调用方按“查不到”处理 */
}

/* Darwin 的 pthread → mach 端口转换；Android 上端口是哑值，统一回 0。 */
static inline mach_port_t pthread_mach_thread_np(pthread_t thread) { return mach_thread_np(thread); }

/* 内存占用查询：Darwin 的 task_vm_info；Android 上按「查不到」处理。 */
typedef struct {
    unsigned long long virtual_size, region_count, resident_size, resident_size_max;
    unsigned long long phys_footprint;
} task_vm_info_data_t;

#define TASK_VM_INFO 22
#define TASK_VM_INFO_COUNT 10
typedef integer_t *task_info_t;

static inline kern_return_t task_info(mach_port_t task, int flavor, task_info_t info, mach_msg_type_number_t *count) {
    (void)task; (void)flavor; (void)info; (void)count;
    return 5;
}


/* CPU/IO 累计量：Darwin 的 THREAD_EXTENDED_INFO；Android 上同样按「查不到」处理。 */
typedef struct {
    time_value_t pth_user_time, pth_system_time;
    integer_t pth_cpu_usage, pth_policy, pth_run_state, pth_flags, pth_sleep_time, pth_curpri, pth_priority, pth_maxpriority;
    char pth_name[64];
} thread_extended_info_data_t, *thread_extended_info_t;

#define THREAD_EXTENDED_INFO 7
#define THREAD_EXTENDED_INFO_COUNT (sizeof(thread_extended_info_data_t) / sizeof(unsigned int))


/* 枚举进程内线程：Linux 上要走 /proc/self/task，这里按「查不到」处理。 */
typedef mach_port_t *thread_act_array_t;
typedef unsigned char boolean_t;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

static inline kern_return_t task_threads(mach_port_t task, thread_act_array_t *list, mach_msg_type_number_t *count) {
    (void)task;
    if (list) *list = 0;
    if (count) *count = 0;
    return 5;
}


static inline kern_return_t vm_deallocate(mach_port_t task, vm_address_t address, vm_size_t size) {
    (void)task; (void)address; (void)size;
    return KERN_SUCCESS;
}
