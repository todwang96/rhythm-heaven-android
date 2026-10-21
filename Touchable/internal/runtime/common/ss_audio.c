/* AudioOut and the AudioRenderer (ss_audio_renderer.c) both follow the app lifecycle. */
#define dm_audio_suspend ss_audio_out_suspend
#include <hle_audio.c>
#undef dm_audio_suspend
void (*g_ss_audio_suspend_hook)(int on);
void dm_audio_suspend(int on) {
    ss_audio_out_suspend(on);
    if (g_ss_audio_suspend_hook) g_ss_audio_suspend_hook(on);
}

HLE_FORCE("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutEPNS_2os11SystemEventERKNS0_17AudioOutParameterE",
          "Open the host default audio output", Result, ss_audio_open_default, void *output, void *event, const uint8_t *parameter) {
    return h_au_Open(output, event, NULL, parameter);
}
