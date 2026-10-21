/* Included by hle_thread.c. G protects the handle table and wait state. */
static void ss_set_thread_sleep(int sleeping) {
    pthread_mutex_lock(&G);
    cur()->ss_sleep = sleeping;
    pthread_mutex_unlock(&G);
}
static int ss_guest_pc(uint64_t pc) {
    for (int i = 0; i < g_nmod; ++i) {
        uint64_t base = (uint64_t)g_mod[i].base;
        if (pc >= base && pc - base < g_mod[i].text_end) return 1;
        if (pc >= base + g_mod[i].patch_off && pc < base + g_mod[i].size) return 1;
    }
    return 0;
}
static uint32_t ss_set_thread_activity(uint32_t handle, uint32_t activity) {
    if (activity > 1) return 0xf001;
    /* Diagnostic: remember where the target thread was the last time it refused to stop.
       A thread that stays in host code never reaches a safe suspend point. */
    uintptr_t ss_last_pc = 0;
    uint32_t ss_last_wkind = 0, ss_last_sleep = 0;
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
        pthread_mutex_lock(&G);
        thr_t *thread = h_peek(handle, O_THREAD);
        if (!thread) { pthread_mutex_unlock(&G); return R_BAD_HANDLE; }
        if (thread == cur() || thread->exited || !thread->started || !thread->mport) {
            pthread_mutex_unlock(&G); return R_BAD_STATE;
        }
        if (!activity) {
            kern_return_t result = thread->ss_suspended ? thread_resume(thread->mport) : KERN_FAILURE;
            if (result == KERN_SUCCESS) thread->ss_suspended = 0;
            pthread_mutex_unlock(&G);
            return result == KERN_SUCCESS ? 0 : R_BAD_STATE;
        }
        if (thread->ss_suspended) { pthread_mutex_unlock(&G); return R_BAD_STATE; }
        if (thread_suspend(thread->mport) != KERN_SUCCESS) { pthread_mutex_unlock(&G); return R_BAD_STATE; }
        arm_thread_state64_t state; mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        kern_return_t result = thread_get_state(thread->mport, ARM_THREAD_STATE64, (thread_state_t)&state, &count);
        /* A stopped host call can hold malloc, Metal, or file-system locks.
           Stop in guest code or in a kernel-style wait that has released G. */
        if (result == KERN_SUCCESS && (ss_guest_pc(arm_thread_state64_get_pc(state)) || thread->wkind || thread->ss_sleep)) {
            thread->ss_suspended = 1;
            pthread_mutex_unlock(&G);
            return 0;
        }
        if (result == KERN_SUCCESS) {
            ss_last_pc = (uintptr_t)arm_thread_state64_get_pc(state);
            ss_last_wkind = thread->wkind;
            ss_last_sleep = thread->ss_sleep;
        }
        thread_resume(thread->mport);
        pthread_mutex_unlock(&G);
        sched_yield();
    }
    LOG("FATAL last state of target thread: pc=%#lx wkind=%u sleep=%u", (unsigned long)ss_last_pc, ss_last_wkind, ss_last_sleep);
    die("Cannot reach a safe suspend point for thread %#x", handle);
}
/* Horizon ARM64 context: integer state, SIMD state, and guest TLS pointer. */
static uint32_t ss_get_thread_context(void *output, uint32_t handle) {
    uint8_t context[0x320] = {0};
    pthread_mutex_lock(&G);
    thr_t *thread = h_peek(handle, O_THREAD);
    if (!thread || !thread->ss_suspended) {
        pthread_mutex_unlock(&G); return thread ? R_BAD_STATE : R_BAD_HANDLE;
    }
    arm_thread_state64_t integer; arm_neon_state64_t vector;
    mach_msg_type_number_t ni = ARM_THREAD_STATE64_COUNT, nv = ARM_NEON_STATE64_COUNT;
    kern_return_t a = thread_get_state(thread->mport, ARM_THREAD_STATE64, (thread_state_t)&integer, &ni);
    kern_return_t b = thread_get_state(thread->mport, ARM_NEON_STATE64, (thread_state_t)&vector, &nv);
    if (a || b) { pthread_mutex_unlock(&G); return R_BAD_STATE; }
    memcpy(context, integer.__x, 29 * 8);
    uint64_t *words = (void *)context;
    words[29] = arm_thread_state64_get_fp(integer);
    words[30] = arm_thread_state64_get_lr(integer);
    words[31] = arm_thread_state64_get_sp(integer);
    words[32] = arm_thread_state64_get_pc(integer);
    memcpy(context + 0x108, &integer.__cpsr, 4);
    memcpy(context + 0x110, vector.__v, 512);
    memcpy(context + 0x310, &vector.__fpcr, 4);
    memcpy(context + 0x314, &vector.__fpsr, 4);
    if (thread->ss_tsd) {
        words[18] = thread->ss_tsd[g_key18];
        words[0x318 / 8] = thread->ss_tsd[g_keytls];
    }
    pthread_mutex_unlock(&G);
    dm_guest_write_prepare(output, sizeof(context));
    memcpy(output, context, sizeof(context));
    return 0;
}
