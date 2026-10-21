/* 节奏天国：sprite batcher 的「提交时拷贝」钩子。
 *
 * Touchable 的 iOS 宿主用 mtl_copy_on_submit 把 B1/B2 两个 uniform buffer 在每次提交时
 * 各留一份不可变副本，从而省掉 Begin/End 之间的 GPU 等待（rh_batch.c 的跳等优化）。
 *
 * 这一版先返回 0（= 不支持）。rh_batch.c 的契约是：copy_on_submit 失败就保留等待，
 * 功能完全正确，只是 CPU 不能领先 GPU 一帧。第一帧阶段这样最省事；
 * 等画面出来了、要追性能时再把这份拷贝语义接到 vk 后端的提交编码里。
 */
#include <stdint.h>

int mtl_copy_on_submit(uint64_t address, uint64_t size) {
    (void)address;
    (void)size;
    return 0;
}

void mtl_copy_on_submit_stats(uint64_t *copies, uint64_t *bytes) {
    if (copies) *copies = 0;
    if (bytes) *bytes = 0;
}
