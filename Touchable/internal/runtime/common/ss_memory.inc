/* Included by hle_svc.c. Track Horizon pages separately from Darwin pages. */
static pthread_mutex_t ss_memory_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t ss_alias_pages[0x100000];
static size_t ss_alias_used;
static uint32_t ss_physical_memory(uint64_t address, uint64_t length, int map) {
    uint64_t start = (uintptr_t)g_alias;
    if ((address | length) & 0xfff || !length || address < start || address - start > g_alias_cap ||
        length > g_alias_cap - (address - start)) return 0xd401;
    size_t first = (address - start) >> 12, count = length >> 12;
    size_t host_page = (size_t)getpagesize();
    size_t lo = first & ~(host_page / 4096 - 1), hi = (first + count + host_page / 4096 - 1) & ~(host_page / 4096 - 1);
    pthread_mutex_lock(&ss_memory_lock);
    if (map) {
        size_t added = 0;
        for (size_t i = first; i < first + count; ++i) added += !ss_alias_pages[i];
        if (g_heap_cur + ss_alias_used + added * 4096 + image_bytes() > app_pool()) {
            pthread_mutex_unlock(&ss_memory_lock); return 0x10801;
        }
        void *base = g_alias + lo * 4096;
        size_t size = (hi - lo) * 4096;
        dm_guest_write_prepare(base, size);
        if (mprotect(base, size, PROT_READ | PROT_WRITE)) die("Physical map protection failed: %d", errno);
        for (size_t i = first; i < first + count; ++i) if (!ss_alias_pages[i]) {
            memset(g_alias + i * 4096, 0, 4096);
            ss_alias_pages[i] = 1;
        }
        ss_alias_used += added * 4096;
    } else {
        for (size_t i = first; i < first + count; ++i) if (ss_alias_pages[i]) {
            ss_alias_pages[i] = 0;
            ss_alias_used -= 4096;
        }
        size_t run = hi, step = host_page / 4096;
        for (size_t p = lo; p <= hi; p += step) {
            int live = p == hi;
            if (p < hi) for (size_t i = p; i < p + step; ++i) live |= ss_alias_pages[i];
            if (!live && run == hi) run = p;
            if (live && run != hi) {
                void *base = g_alias + run * 4096; size_t size = (p - run) * 4096;
                if (mprotect(base, size, PROT_NONE)) die("Physical unmap protection failed: %d", errno);
                madvise(base, size, MADV_FREE);
                run = hi;
            }
        }
    }
    pthread_mutex_unlock(&ss_memory_lock);
    return 0;
}
static void ss_alias_query(uint64_t address, uint64_t *base, uint64_t *size, uint64_t *state, uint64_t *permission) {
    size_t page = (address - (uintptr_t)g_alias) >> 12, lo = page, hi = page + 1, limit = g_alias_cap >> 12;
    pthread_mutex_lock(&ss_memory_lock);
    int mapped = ss_alias_pages[page];
    while (lo && ss_alias_pages[lo - 1] == mapped) --lo;
    while (hi < limit && ss_alias_pages[hi] == mapped) ++hi;
    pthread_mutex_unlock(&ss_memory_lock);
    *base = (uintptr_t)g_alias + lo * 4096; *size = (hi - lo) * 4096;
    *state = mapped ? 5 : 0; *permission = mapped ? 3 : 0;
}
static size_t ss_physical_used(void) {
    pthread_mutex_lock(&ss_memory_lock); size_t used = ss_alias_used; pthread_mutex_unlock(&ss_memory_lock);
    return used;
}
static void ss_external_query(uint64_t address, uint64_t *base, uint64_t *size, uint64_t *state, uint64_t *permission) {
    const uint64_t end = 1ull << 39;
    if (address >= end) { *base = end; *size = -end; *state = 0x10; *permission = 0; return; }
    uint64_t lo = 0, hi = end;
    for (int i = 0; i < g_nmod; ++i) {
        module_t *module = &g_mod[i];
        uint64_t start = (uintptr_t)module->base, finish = start + module->size;
        if (start <= address && address < finish) {
            uint64_t offset = address - start, next;
            if (offset < module->ro_off) { offset = 0; next = module->ro_off; *permission = 5; *state = 3; }
            else if (offset < module->data_off) { offset = module->ro_off; next = module->data_off; *permission = 1; *state = 3; }
            else if (offset < module->patch_off) { offset = module->data_off; next = module->patch_off; *permission = 3; *state = 4; }
            /* Rewritten instruction blocks have no separate NSO header. */
            else { offset = module->patch_off; next = module->size; *permission = 5; *state = 0x14; }
            *base = start + offset; *size = next - offset;
            return;
        }
        if (finish <= address && finish > lo) lo = finish;
        if (start > address && start < hi) hi = start;
    }
    uint64_t top = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
    uint64_t bottom = top - pthread_get_stacksize_np(pthread_self());
    if (bottom <= address && address < top) { *base = bottom; *size = top - bottom; *state = 0xb; *permission = 3; return; }
    if (top <= address && top > lo) lo = top;
    if (bottom > address && bottom < hi) hi = bottom;
    uint64_t start = (uintptr_t)g_as, finish = start + RESERVE;
    if (finish <= address && finish > lo) lo = finish;
    if (start > address && start < hi) hi = start;
    *base = lo; *size = hi - lo; *state = 0; *permission = 0;
}
