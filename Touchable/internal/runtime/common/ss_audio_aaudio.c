/* Android AAudio output for the nn::audio AudioRenderer (ss_audio_renderer.c).
 *
 * ss_audio_renderer.c mixes PCM16 frames of the console renderer into a lock-free ring; its
 * output unit calls consume() on whichever thread owns the audio clock: CoreAudio on Apple,
 * this AAudio stream on Android. The AAudio data callback below only moves frames out of that
 * ring and never calls into guest code, so the game threads are never blocked.
 *
 * The d2r-mobile AudioOut backend (dm/android/audio_aaudio.inc) is not reused: it owns a guest
 * AudioOut queue and its own nn::os SystemEvent, while the renderer needs a plain device clock
 * that pulls mixed frames.
 *
 * Failure policy mirrors hle_audio.c: never die. When the stream cannot be opened, cannot be
 * started, or reports an error (headset unplugged / device disconnected / internal timeout), the
 * callback just stops arriving; the renderer detects it and falls back to its existing silent
 * sink, which consumes the ring in real time so the game keeps running - only without sound.
 *
 * DM_AUDIO: ss_audio_renderer.c's real_audio() decides whether this file is used at all. Android
 * defaults to real AAudio output; DM_AUDIO=0 stays on the silent sink (regression path).
 */
#ifdef __ANDROID__
#include "hle.h"
#include <aaudio/AAudio.h>
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* AAudioStream_isMMapUsed 是 API 30 的符号；本工程按 API 29 编译，运行时用 dlsym 探测，
 * 这样日志里的 mmap 值才可信（此前一直打印 0 只是因为编译目标没这个函数）。
 * AAudioStream_isMMapUsed is API 30; this build targets API 29, so resolve it at runtime
 * to make the mmap diagnostic trustworthy. */
typedef int32_t (*aaudio_is_mmap_fn)(const AAudioStream *);
static int aaudio_stream_is_mmap(const AAudioStream *s) {
    static aaudio_is_mmap_fn fn = (aaudio_is_mmap_fn)(uintptr_t)0;
    static int tried;
    if (!tried) { tried = 1; fn = (aaudio_is_mmap_fn)(uintptr_t)dlsym(RTLD_DEFAULT, "AAudioStream_isMMapUsed"); }
    if (!fn || !s) return -1;
    return fn(s);
}

typedef void (*ss_audio_aaudio_pull_fn)(void *context, int16_t *stereo, int frames);

typedef struct {
    AAudioStream *stream;
    ss_audio_aaudio_pull_fn pull;
    void *context;
    int rate, channels;
    int format_float;   /* 1 = 流是 FLOAT 格式，回调里需要 int16->float 转换 */
    _Atomic int muted;          /* 1 = keep the pump alive but write silence (lifecycle suspend) */
    _Atomic int error;          /* last aaudio_result_t from the error callback, 0 = none */
    _Atomic uint64_t callbacks; /* data callbacks so far (diagnostics) */
    _Atomic uint64_t frames;    /* frames handed to the device clock (diagnostics) */
} ss_audio_aaudio;

/* Data callback: AAudio's real-time thread. Pulls the mixed frames from the renderer ring and
 * writes them where AAudio asked. No allocation, no guest code, no stream control here. */
static aaudio_data_callback_result_t aaudio_render(AAudioStream *stream, void *userdata, void *audio_data, int32_t frames) {
    (void)stream;
    ss_audio_aaudio *out = userdata;
    if (!out || frames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    int16_t converted[2048 * 2];
    int16_t *pcm = (int16_t *)audio_data;
    if (out->format_float) {
        if (frames > 2048) frames = 2048;
        pcm = converted;
    }
    if (out->pull) out->pull(out->context, pcm, (int)frames);
    if (out->format_float) {
        float *dst = (float *)audio_data;
        int samples = frames * out->channels;
        for (int i = 0; i < samples; i++) dst[i] = (float)pcm[i] * (1.0f / 32768.0f);
    }
#ifdef __ANDROID__
    if (getenv("DM_AUDIOPEAK")) {
        static _Atomic int dbg_count;
        int n = atomic_fetch_add(&dbg_count, 1) + 1;
        if (n % 50 == 0) {
            const int16_t *pcm = (const int16_t *)audio_data;
            int peak = 0, samples = frames * (int)out->channels;
            for (int i = 0; i < samples; i++) { int v = pcm[i] < 0 ? -pcm[i] : pcm[i]; if (v > peak) peak = v; }
            LOG("AUDIOPEAK callbacks=%d frames=%d peak=%d", n, frames, peak);
        }
    }
#endif
    if (getenv("DM_AVSYNC")) {
        static _Atomic int dbg;
        int n = atomic_fetch_add(&dbg, 1) + 1;
        if (n % 20 == 0 && stream) {
            int64_t position = 0, time_ns = 0;
            aaudio_result_t tr = AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &position, &time_ns);
            int64_t written = AAudioStream_getFramesWritten(stream);
            if (tr == AAUDIO_OK)
                LOG("AVSYNC_AUDIO cb=%d written=%lld presented=%lld ts_ns=%lld now_ns=%lld xruns=%d",
                    n, (long long)written, (long long)position, (long long)time_ns, (long long)now_ns(),
                    (int)AAudioStream_getXRunCount(stream));
        }
    }
    if (atomic_load_explicit(&out->muted, memory_order_relaxed))
        memset(audio_data, 0, sizeof(int16_t) * (size_t)out->channels * (size_t)frames);
    if (atomic_load_explicit(&out->error, memory_order_relaxed) == 0) {
        atomic_fetch_add_explicit(&out->callbacks, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&out->frames, (uint64_t)(uint32_t)frames, memory_order_relaxed);
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* Error callback: also an AAudio thread. Only record the error; the renderer's server thread
 * reports it and decides what to do (closing/reopening from here is forbidden by AAudio). */
static void aaudio_error(AAudioStream *stream, void *userdata, aaudio_result_t error) {
    (void)stream;
    ss_audio_aaudio *out = userdata;
    if (out) atomic_store_explicit(&out->error, (int)error, memory_order_release);
}

static int wait_state(ss_audio_aaudio *out, aaudio_stream_state_t wanted, int64_t timeout_ns) {
    int64_t deadline = (int64_t)now_ns() + timeout_ns;
    for (;;) {
        aaudio_stream_state_t state = AAudioStream_getState(out->stream), next;
        if (state == wanted) return AAUDIO_OK;
        if (state == AAUDIO_STREAM_STATE_DISCONNECTED) return AAUDIO_ERROR_DISCONNECTED;
        if (state == AAUDIO_STREAM_STATE_CLOSED || state == AAUDIO_STREAM_STATE_CLOSING) return AAUDIO_ERROR_INVALID_STATE;
        int64_t left = deadline - (int64_t)now_ns();
        if (left <= 0) return AAUDIO_ERROR_TIMEOUT;
        int result = AAudioStream_waitForStateChange(out->stream, state, &next, left);
        if (result != AAUDIO_OK && result != AAUDIO_ERROR_TIMEOUT) return result;
    }
}

/* One openStream attempt with the renderer format: PCM16, stereo, renderer rate.
 * 先尝试 EXCLUSIVE+LOW_LATENCY 拿 fast track（MMAP），拿不到再降级；每步都记录服务端实际
 * 返回的 performance mode，方便判断是否真的低延迟。 */
typedef struct { int sharing, usage, perf, format; } aaudio_try;
static const aaudio_try aaudio_tries[] = {
    {AAUDIO_SHARING_MODE_EXCLUSIVE, AAUDIO_USAGE_GAME,  AAUDIO_PERFORMANCE_MODE_LOW_LATENCY, AAUDIO_FORMAT_PCM_FLOAT},
    {AAUDIO_SHARING_MODE_EXCLUSIVE, AAUDIO_USAGE_GAME,  AAUDIO_PERFORMANCE_MODE_LOW_LATENCY, AAUDIO_FORMAT_PCM_I16},
    {AAUDIO_SHARING_MODE_SHARED,    AAUDIO_USAGE_MEDIA, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY, AAUDIO_FORMAT_PCM_FLOAT},
    {AAUDIO_SHARING_MODE_SHARED,    AAUDIO_USAGE_GAME,  AAUDIO_PERFORMANCE_MODE_LOW_LATENCY, AAUDIO_FORMAT_PCM_I16},
    {AAUDIO_SHARING_MODE_SHARED,    AAUDIO_USAGE_GAME,  AAUDIO_PERFORMANCE_MODE_NONE,       AAUDIO_FORMAT_PCM_I16},
};
/* DM_AAUDIO_PROBE=1：启动时把一组参数组合各开一次流，打印 burst/perf/buffer/mmap。
 * fast track 的准入判断发生在创建 track 时，open 后再 setBufferSize 不会改变是否 fast，
 * 所以必须用不同的 open 参数实测。只在诊断时打开。| Probe matrix for the fast-track
 * admission: the decision happens when the track is created, so different open parameters
 * must be measured. Diagnostic only. */
static int probe_one(int rate, int usage, int format, int cb, int cap, const char *tag) {
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) return -1;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setSampleRate(b, rate);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setFormat(b, (aaudio_format_t)format);
    AAudioStreamBuilder_setUsage(b, (aaudio_usage_t)usage);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    if (cb > 0) AAudioStreamBuilder_setFramesPerDataCallback(b, cb);
    if (cap > 0) AAudioStreamBuilder_setBufferCapacityInFrames(b, cap);
    AAudioStream *s = NULL;
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s) { LOG("AUDIO_PROBE %s open failed: %d (%s)", tag, (int)r, AAudio_convertResultToText(r)); return -1; }
    LOG("AUDIO_PROBE %s burst=%d perf=%d buffer=%d cap=%d mmap=%d cb=%d",
        tag, (int)AAudioStream_getFramesPerBurst(s), (int)AAudioStream_getPerformanceMode(s),
        (int)AAudioStream_getBufferSizeInFrames(s), (int)AAudioStream_getBufferCapacityInFrames(s),
        aaudio_stream_is_mmap(s), (int)AAudioStream_getFramesPerDataCallback(s));
    AAudioStream_close(s);
    return 0;
}
static void probe_streams(int rate) {
    static const struct { int usage, format, cb, cap; const char *tag; } combos[] = {
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_I16,   0,   0, "game/i16/cb0/cap0"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_I16, 192, 192, "game/i16/cb192/cap192"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_I16, 192, 384, "game/i16/cb192/cap384"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_I16, 480, 192, "game/i16/cb480/cap192"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_I16,  96, 192, "game/i16/cb96/cap192"},
        {AAUDIO_USAGE_MEDIA, AAUDIO_FORMAT_PCM_I16, 192, 192, "media/i16/cb192/cap192"},
        {AAUDIO_USAGE_MEDIA, AAUDIO_FORMAT_PCM_I16, 480,   0, "media/i16/cb480/cap0"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_FLOAT,192, 192, "game/f32/cb192/cap192"},
        {AAUDIO_USAGE_GAME,  AAUDIO_FORMAT_PCM_FLOAT,480,   0, "game/f32/cb480/cap0"},
    };
    for (unsigned i = 0; i < sizeof combos / sizeof combos[0]; ++i)
        probe_one(rate, combos[i].usage, combos[i].format, combos[i].cb, combos[i].cap, combos[i].tag);
}

static int stream_open(ss_audio_aaudio *out) {
    if (getenv("DM_AAUDIO_PROBE")) probe_streams(out->rate);
    int result = AAUDIO_ERROR_UNAVAILABLE;
    int ntry = (int)(sizeof aaudio_tries / sizeof aaudio_tries[0]);
    for (int attempt = 0; attempt < ntry; ++attempt) {
        const aaudio_try *tr = &aaudio_tries[attempt];
        AAudioStreamBuilder *builder = NULL;
        result = AAudio_createStreamBuilder(&builder);
        if (result != AAUDIO_OK || !builder) return result != AAUDIO_OK ? result : AAUDIO_ERROR_NO_MEMORY;
        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder, (aaudio_sharing_mode_t)tr->sharing);
        AAudioStreamBuilder_setSampleRate(builder, out->rate);
        AAudioStreamBuilder_setChannelCount(builder, out->channels);
        AAudioStreamBuilder_setFormat(builder, (aaudio_format_t)tr->format);
        /* DM_AAUDIO_CB 可调数据回调帧数（默认 480=10ms@48k）。更小的回调能降低 AAudio
         * 侧排队深度，但也更容易被 HAL 的深缓冲路径放大 jitter。| DM_AAUDIO_CB overrides
         * the data-callback frame count (default 480 = 10 ms at 48 kHz). */
        int cb_frames = 480;
        { const char *e = getenv("DM_AAUDIO_CB"); if (e) { int v = atoi(e); if (v >= 64 && v <= 4096) cb_frames = v; } }
        AAudioStreamBuilder_setFramesPerDataCallback(builder, cb_frames);
        AAudioStreamBuilder_setUsage(builder, (aaudio_usage_t)tr->usage);
        AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setPerformanceMode(builder, (aaudio_performance_mode_t)tr->perf);
        /* DM_AAUDIO_CAPACITY：open 前设置的 buffer capacity（帧）。fast track 准入在创建 track
         * 时判定，open 后再 setBufferSize 改不了；先压 capacity 试能否拿到 FAST。 */
        int cap_frames = 0;
        { const char *e = getenv("DM_AAUDIO_CAPACITY"); if (e) { int v = atoi(e); if (v > 0 && v <= 65536) cap_frames = v; } }
        if (cap_frames > 0) AAudioStreamBuilder_setBufferCapacityInFrames(builder, cap_frames);
        AAudioStreamBuilder_setDataCallback(builder, aaudio_render, out);
        AAudioStreamBuilder_setErrorCallback(builder, aaudio_error, out);
        result = AAudioStreamBuilder_openStream(builder, &out->stream);
        AAudioStreamBuilder_delete(builder);
        if (result != AAUDIO_OK) {
            LOG("audio::AudioRenderer AAudio attempt %d (sharing=%d usage=%d) failed: %d (%s)",
                attempt, tr->sharing, tr->usage, (int)result, AAudio_convertResultToText(result));
            out->stream = NULL; continue;
        }
        int got_format = AAudioStream_getFormat(out->stream);
        int want_float = tr->format == AAUDIO_FORMAT_PCM_FLOAT;
        if (AAudioStream_getSampleRate(out->stream) != out->rate ||
            AAudioStream_getChannelCount(out->stream) != out->channels ||
            (got_format != AAUDIO_FORMAT_PCM_I16 && got_format != AAUDIO_FORMAT_PCM_FLOAT)) {
            LOG("audio::AudioRenderer AAudio attempt %d returned %d Hz %d ch format %d; retry",
                attempt, AAudioStream_getSampleRate(out->stream), AAudioStream_getChannelCount(out->stream),
                (int)AAudioStream_getFormat(out->stream));
            AAudioStream_close(out->stream); out->stream = NULL; continue;
        }
        int32_t got_perf = AAudioStream_getPerformanceMode(out->stream);
        int32_t burst = AAudioStream_getFramesPerBurst(out->stream);
        int mmap_used = aaudio_stream_is_mmap(out->stream);
        out->format_float = got_format == AAUDIO_FORMAT_PCM_FLOAT;
        /* DM_AAUDIO_BUFSIZE：显式指定 AAudio 缓冲帧数（0=沿用启发式：本机拿不到 fast track
         * 时 2×burst）。这台 vivo 的 HAL 深缓冲路径会在流内队列上再叠 ~3000 帧，缓冲区调小
         * 是否真降延迟必须用 AUDIO_STATS 的 inflight/latency_ms 实测。| Explicit buffer size
         * in frames; 0 keeps the heuristic. The HAL deep-buffer path adds its own queue, so
         * verify with AUDIO_STATS inflight/latency before trusting a smaller value. */
        int buf_override = 0;
        { const char *e = getenv("DM_AAUDIO_BUFSIZE"); if (e) { int v = atoi(e); if (v > 0 && v <= 65536) buf_override = v; } }
        if (burst > 0 && burst <= INT32_MAX / 2) {
            /* 默认 1×burst：实测（vivo 10AG5R380R002J8，深缓冲路径 inflight≈4944 帧）把缓冲区
               从 2×burst 降到 1×burst 后总延迟 123ms → 103ms，AAudio 侧 xrun 仍为 0；2×burst 可用
               DM_AAUDIO_BUFSIZE=1924 恢复。| Default 1x burst: on this deep-buffer path the total
               latency dropped 123 -> 103 ms with xruns still 0; DM_AAUDIO_BUFSIZE=1924 restores 2x. */
            int want_buf = buf_override > 0 ? buf_override : burst;
            AAudioStream_setBufferSizeInFrames(out->stream, want_buf);
        }
        LOG("audio::AudioRenderer AAudio opened %s %d Hz %d ch, device=%d burst=%d requested_perf=%d got_perf=%d buffer=%d mmap=%d attempt=%d callbacks=%d",
            out->format_float ? "FLOAT" : "PCM16",
            AAudioStream_getSampleRate(out->stream), AAudioStream_getChannelCount(out->stream),
            AAudioStream_getDeviceId(out->stream), burst, tr->perf, got_perf,
            AAudioStream_getBufferSizeInFrames(out->stream), mmap_used, attempt, (int)AAudioStream_getFramesPerDataCallback(out->stream));
        /* 服务端降级时默认不保留：实测 vivo 游戏魔盒的 384-burst 降级路径缓冲被强制 1924 帧，
           总延迟 123ms，比普通路径的 103ms 更差。仅 DM_AAUDIO_ACCEPT_DOWNGRADED=1 时接受，
           用于继续排查厂商侧路径。| Do not keep downgraded streams by default: the vivo game-mode
           384-burst path measured 123 ms (buffer forced to 1924), worse than 103 ms. */
        int accept_downgraded = 0;
        { const char *e = getenv("DM_AAUDIO_ACCEPT_DOWNGRADED"); if (e) accept_downgraded = atoi(e) != 0; }
        int downgraded = tr->perf == AAUDIO_PERFORMANCE_MODE_LOW_LATENCY && got_perf != AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
        if (downgraded && !accept_downgraded && attempt + 1 < ntry) {
            AAudioStream_close(out->stream); out->stream = NULL; continue;   /* 服务端降级，试下一组参数 */
        }
        return AAUDIO_OK;
    }
    return result;
}

/* Open the Android device clock. NULL = no AAudio output; the caller keeps its silent sink.
 * The returned handle must be released with ss_audio_aaudio_close(). */
void *ss_audio_aaudio_open(int rate, ss_audio_aaudio_pull_fn pull, void *context) {
    if (!pull || (rate != 32000 && rate != 48000)) {
        LOG("audio::AudioRenderer AAudio needs a known pull callback and a 32/48 kHz rate (got %d)", rate);
        return NULL;
    }
    ss_audio_aaudio *out = calloc(1, sizeof(*out));
    if (!out) { LOG("audio::AudioRenderer AAudio cannot allocate its output object"); return NULL; }
    out->pull = pull;
    out->context = context;
    out->rate = rate;
    out->channels = 2;   /* ss_ar_render() always produces interleaved stereo PCM16 */
    atomic_init(&out->muted, 0);
    atomic_init(&out->error, 0);
    atomic_init(&out->callbacks, 0);
    atomic_init(&out->frames, 0);
    int result = stream_open(out);
    if (result != AAUDIO_OK) {
        LOG("audio::AudioRenderer AAudio open failed: %d (%s); the silent sink takes over (no sound)",
            result, AAudio_convertResultToText(result));
        free(out);
        return NULL;
    }
    return out;
}

/* 0 = the AAudio callback thread is running and will pull the ring; negative = aaudio error.
 * The stream object stays usable for a later start attempt. */
int ss_audio_aaudio_start(void *handle) {
    ss_audio_aaudio *out = handle;
    if (!out || !out->stream) return AAUDIO_ERROR_INVALID_STATE;
    atomic_store_explicit(&out->error, 0, memory_order_release);
    int result = AAudioStream_requestStart(out->stream);
    if (result == AAUDIO_OK) result = wait_state(out, AAUDIO_STREAM_STATE_STARTED, 1000000000ll);
    if (result != AAUDIO_OK) {
        LOG("audio::AudioRenderer AAudio start failed: %d (%s)", result, AAudio_convertResultToText(result));
        atomic_store_explicit(&out->error, result, memory_order_release);
    } else {
        LOG("audio::AudioRenderer AAudio started (%d Hz stereo PCM16)", out->rate);
    }
    return result;
}

/* Stop the callbacks but keep the stream for a later start. Guest thread, bounded wait. */
void ss_audio_aaudio_stop(void *handle) {
    ss_audio_aaudio *out = handle;
    if (!out || !out->stream) return;
    aaudio_stream_state_t state = AAudioStream_getState(out->stream);
    if (state != AAUDIO_STREAM_STATE_STARTED && state != AAUDIO_STREAM_STATE_STARTING &&
        state != AAUDIO_STREAM_STATE_PAUSING) return;
    int result = AAudioStream_requestPause(out->stream);
    if (result == AAUDIO_OK) result = wait_state(out, AAUDIO_STREAM_STATE_PAUSED, 500000000ll);
    if (result != AAUDIO_OK && result != AAUDIO_ERROR_DISCONNECTED && result != AAUDIO_ERROR_INVALID_STATE)
        LOG("audio::AudioRenderer AAudio pause failed: %d (%s)", result, AAudio_convertResultToText(result));
}

/* Lifecycle suspend (background / audio focus lost): keep the device pump running but hand it
 * silence, exactly like the Android AudioOut backend keeps its stream alive while muted. The
 * renderer ring keeps being consumed, so the guest renderer event never stalls. */
void ss_audio_aaudio_set_muted(void *handle, int muted) {
    ss_audio_aaudio *out = handle;
    if (out) atomic_store_explicit(&out->muted, muted != 0, memory_order_release);
}

int ss_audio_aaudio_error(void *handle) {
    ss_audio_aaudio *out = handle;
    return out ? atomic_load_explicit(&out->error, memory_order_acquire) : 0;
}

uint64_t ss_audio_aaudio_frames(void *handle) {
    ss_audio_aaudio *out = handle;
    return out ? atomic_load_explicit(&out->frames, memory_order_relaxed) : 0;
}

/* 常驻遥测（每 ~6s 调用一次，不在热路径）：AAudio 队列里还没播出去的帧数、缓冲区大小、
 * 硬件 xrun 计数。用来把「破音/音频拖慢」拆成三件事：环形缓冲预缓冲不足（underrun）、
 * AAudio 队列积压（in_flight 变大 = 延迟漂移）、或设备侧 xrun。
 * Always-on telemetry helpers (called ~every 6 s, not in the hot path): frames still queued in
 * AAudio, the stream buffer size, and the device xrun count. */
long long ss_audio_aaudio_inflight(void *handle) {
    ss_audio_aaudio *out = handle;
    if (!out || !out->stream) return -1;
    int64_t position = 0, time_ns = 0;
    if (AAudioStream_getTimestamp(out->stream, CLOCK_MONOTONIC, &position, &time_ns) != AAUDIO_OK) return -1;
    int64_t written = AAudioStream_getFramesWritten(out->stream);
    long long diff = (long long)(written - position);
    return diff < 0 ? 0 : diff;
}
int ss_audio_aaudio_buffersize(void *handle) {
    ss_audio_aaudio *out = handle;
    return out && out->stream ? (int)AAudioStream_getBufferSizeInFrames(out->stream) : -1;
}
int ss_audio_aaudio_xruns(void *handle) {
    ss_audio_aaudio *out = handle;
    return out && out->stream ? (int)AAudioStream_getXRunCount(out->stream) : -1;
}

/* Close the stream and free the object. Called from ordinary threads only (never the AAudio
 * data/error callback): renderer close or its server thread's error failover. */
void ss_audio_aaudio_close(void *handle) {
    ss_audio_aaudio *out = handle;
    if (!out) return;
    uint64_t callbacks = atomic_load(&out->callbacks), frames = atomic_load(&out->frames);
    if (out->stream) {
        aaudio_stream_state_t state = AAudioStream_getState(out->stream);
        if (state == AAUDIO_STREAM_STATE_STARTED || state == AAUDIO_STREAM_STATE_STARTING) {
            /* Pause first so the callback thread stops before the stream is deleted; a failed
             * pause (disconnected stream) is fine, close below tears it down anyway. */
            if (AAudioStream_requestPause(out->stream) == AAUDIO_OK)
                wait_state(out, AAUDIO_STREAM_STATE_PAUSED, 500000000ll);
        }
        AAudioStream_close(out->stream);
        out->stream = NULL;
    }
    LOG("audio::AudioRenderer AAudio closed after %llu callbacks, %llu frames",
        (unsigned long long)callbacks, (unsigned long long)frames);
    free(out);
}

#else  /* !__ANDROID__: the Apple build keeps ss_audio_renderer.c's own CoreAudio unit. */
typedef int ss_audio_aaudio_android_only_translation_unit;
#endif
