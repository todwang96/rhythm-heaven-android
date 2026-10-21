package com.rhport.rhythm;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

/** 启动器设置：画面比例 / 震动强度 / 音频延迟校正。设置存 SharedPreferences，启动游戏前写成 rhdata/env.txt。 */
final class Settings {
    private static final String PREFS = "rhport_settings";

    /** 画面比例：DM_ASPECT_FIT 0=拉伸满屏 1=16:9 黑边（默认） 2=等比裁切满屏 */
    static final int ASPECT_STRETCH = 0;
    static final int ASPECT_16_9 = 1;
    static final int ASPECT_CROP = 2;

    /** 震动增益的几档（DM_HAPTIC_GAIN；0 = 关闭）。 */
    static final float[] HAPTIC_GAINS = {0.0f, 1.5f, 2.25f, 3.0f};
    static final String[] HAPTIC_NAMES = {"关闭", "标准 (+50%)", "强 (+125%，默认)", "最强 (+200%)"};

    /** 画面延迟（声画同步）档位（毫秒，0 = 关闭）。蓝牙耳机可能需要 150~200。 */
    static final int[] DELAY_MS = {0, 30, 50, 70, 100, 150, 200};

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static int aspect(Context c) {
        int v = prefs(c).getInt("aspect", ASPECT_16_9);
        return (v == ASPECT_STRETCH || v == ASPECT_CROP) ? v : ASPECT_16_9;
    }

    /** 返回 HAPTIC_GAINS 的下标。 */
    static int hapticIndex(Context c) {
        int i = prefs(c).getInt("haptic", 2);
        return (i >= 0 && i < HAPTIC_GAINS.length) ? i : 2;
    }

    static float hapticGain(Context c) {
        return HAPTIC_GAINS[hapticIndex(c)];
    }

    /** 返回 DELAY_MS 的下标。 */
    static int delayIndex(Context c) {
        int i = prefs(c).getInt("delay", 0);
        return (i >= 0 && i < DELAY_MS.length) ? i : 0;
    }

    static int videoDelayMs(Context c) {
        return DELAY_MS[delayIndex(c)];
    }

    static void setAspect(Context c, int v) { prefs(c).edit().putInt("aspect", v).apply(); }
    static void setHapticIndex(Context c, int v) { prefs(c).edit().putInt("haptic", v).apply(); }
    static void setDelayIndex(Context c, int v) { prefs(c).edit().putInt("delay", v).apply(); }

    /** 最近一次节拍校准得到的平均偏差（毫秒），0 = 没测过。 */
    static int calibMs(Context c) { return prefs(c).getInt("calib_ms", 0); }

    static void setCalibMs(Context c, int v) { prefs(c).edit().putInt("calib_ms", v).apply(); }

    /** 把设置写成 env.txt（native 在 configure 时读取）。 */
    static void writeEnvFile(Context c, File rhdata) {
        StringBuilder sb = new StringBuilder();
        sb.append("DM_RT_SAMPLE_FLIP=1\n");
        int aspect = aspect(c);
        sb.append("DM_ASPECT_FIT=").append(aspect).append('\n');
        if (aspect == ASPECT_CROP) sb.append("DM_CROP_TOP=50\n");
        sb.append("DM_HAPTIC_GAIN=").append(String.format(java.util.Locale.US, "%.2f", hapticGain(c))).append('\n');
        int delay = videoDelayMs(c);
        if (delay > 0) sb.append("DM_VIDEO_DELAY_MS=").append(delay).append('\n');
        try {
            rhdata.mkdirs();
            File f = new File(rhdata, "env.txt");
            try (FileOutputStream out = new FileOutputStream(f)) {
                out.write(sb.toString().getBytes(StandardCharsets.UTF_8));
            }
        } catch (Exception ignored) {
            /* 写不了就保持原 env.txt，游戏仍可启动 */
        }
    }
}
