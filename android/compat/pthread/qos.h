/* pthread/qos.h → Android 上用调度策略表达，这里全部做空操作。
 * 调用点只是“给游戏线程提优先级”，提不上去不影响正确性。 */
#pragma once
#include <pthread.h>

typedef enum {
    QOS_CLASS_USER_INTERACTIVE = 0x21,
    QOS_CLASS_USER_INITIATED = 0x19,
    QOS_CLASS_DEFAULT = 0x15,
    QOS_CLASS_UTILITY = 0x11,
    QOS_CLASS_BACKGROUND = 0x09,
    QOS_CLASS_UNSPECIFIED = 0x00
} qos_class_t;

static inline int pthread_set_qos_class_self_np(qos_class_t cls, int relative) { (void)cls; (void)relative; return 0; }
static inline int pthread_get_qos_class_np(pthread_t thread, qos_class_t *cls, int *relative) {
    (void)thread; if (cls) *cls = QOS_CLASS_DEFAULT; if (relative) *relative = 0; return 0;
}
static inline int pthread_attr_set_qos_class_np(pthread_attr_t *attr, qos_class_t cls, int relative) {
    (void)attr; (void)cls; (void)relative; return 0;
}
#define QOS_MIN_RELATIVE_PRIORITY (-15)
