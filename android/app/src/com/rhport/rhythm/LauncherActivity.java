package com.rhport.rhythm;

import android.app.Activity;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTimestamp;
import android.media.AudioTrack;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.SpannableString;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** 启动器：全屏背景 + 右侧文字菜单（一级页入口 / 二级页选项 / 数据包导入）。 */
public final class LauncherActivity extends Activity {
    private static final int REQ_PICK_ZIP = 41;
    private static final String[] DEFAULT_ZIP_PATHS = {
            "/sdcard/Android/media/com.rhport.rhythm/rhdata-pack.zip",
            "/storage/emulated/0/Android/media/com.rhport.rhythm/rhdata-pack.zip",
            "/sdcard/Download/rhdata-pack.zip",
            "/storage/emulated/0/Download/rhdata-pack.zip",
            "/sdcard/Download/rhdata.zip",
    };

    private static final int C_GOLD = 0xffa4640a;
    private static final int C_GOLD_DIM = 0xdd8a5a10;
    private static final int C_TEXT = 0xff1f2b27;
    private static final int C_DIM = 0xff4d6058;
    private static final int C_OK = 0xff2e7d46;
    private static final int C_BAD = 0xffb23a3a;

    private enum Page { MAIN, DATA, ASPECT, HAPTIC, DELAY, CALIB }

    private Page page = Page.MAIN;
    private TextView statusView;
    private TextView importView;
    private ProgressBar importBar;
    private volatile boolean importing;

    /* ---- 节拍校准（播放 click，用户跟着点，测出听感偏差） ---- */
    private static final int CAL_RATE = 48000;
    private static final int CAL_BEAT_FRAMES = 24000;   /* 0.5s 一拍 */
    private static final int CAL_TARGET_TAPS = 12;
    private AudioTrack calTrack;
    private Thread calThread;
    private volatile boolean calRunning;
    private final List<Long> calBeats = new ArrayList<>();
    private final List<Double> calDeltas = new ArrayList<>();
    private TextView calStatus;
    private LinearLayout calResultBox;
    /** 文字/间距缩放：手机屏幕 dp 高度小，按高度比例缩小字号，平板上为 1.0。 */
    private float uiScale = 1f;

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private float sp(float v) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, getResources().getDisplayMetrics());
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        float hDp = getResources().getDisplayMetrics().heightPixels / getResources().getDisplayMetrics().density;
        uiScale = Math.max(0.72f, Math.min(1f, hDp / 620f));
        hideSystemUi();
        showPage(Page.MAIN);
    }

    /** 全屏沉浸（和游戏页一致）：隐藏状态栏/导航栏，用户滑动后自动收回。 */
    private void hideSystemUi() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override
    protected void onPause() {
        stopCalibration();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) hideSystemUi();
    }

    @Override
    public void onBackPressed() {
        if (page != Page.MAIN) {
            showPage(Page.MAIN);
            return;
        }
        super.onBackPressed();
    }

    private void showPage(Page p) {
        stopCalibration();
        page = p;
        statusView = null;
        importView = null;
        importBar = null;
        calStatus = null;
        calResultBox = null;
        switch (p) {
            case MAIN: buildMain(); break;
            case DATA: buildDataPage(); break;
            case ASPECT: buildSettingPage("画面比例", "选择游戏画面如何适配屏幕", buildAspectOptions()); break;
            case HAPTIC: buildSettingPage("震动强度", "节奏打击的震动力度", buildHapticOptions()); break;
            case DELAY: buildSettingPage("画面延迟（声画同步）",
                    "音乐比画面慢时调大；只影响画面、不影响判定。蓝牙耳机可能需要 100ms 以上", buildDelayOptions()); break;
            case CALIB: buildCalibrationPage(); break;
        }
    }

    /* ================= 页面骨架：全屏背景 + 右侧液态玻璃面板 ================= */

    private LinearLayout makeRoot() {
        FrameLayout root = new FrameLayout(this);

        ImageView bg = new ImageView(this);
        bg.setImageResource(R.drawable.launcher_bg);
        bg.setScaleType(ImageView.ScaleType.CENTER_CROP);
        root.addView(bg, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        ScrollView scroll = new ScrollView(this);
        scroll.setClipToPadding(false);
        root.addView(scroll, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.HORIZONTAL);
        outer.setGravity(Gravity.END | Gravity.TOP);
        outer.setPadding(0, dp(20 * uiScale), dp(24), dp(20 * uiScale));
        scroll.addView(outer);

        /* 玻璃卡片：模糊底图 + 深色 tint + 细亮边（不再用全屏渐变蒙层） */
        FrameLayout card = new FrameLayout(this);
        int width = (int) Math.min(getResources().getDisplayMetrics().widthPixels * 0.36, dp(600));
        card.setLayoutParams(new LinearLayout.LayoutParams(width, ViewGroup.LayoutParams.WRAP_CONTENT));

        GlassPanel glass = new GlassPanel(this, R.drawable.launcher_bg);
        card.addView(glass, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        /* 边框单独一层，避免被边缘折射扭曲 */
        GlassPanel.BorderView border = new GlassPanel.BorderView(this);
        card.addView(border, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.END);
        col.setPadding(dp(22), dp(16), dp(22), dp(18));
        card.addView(col, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        outer.addView(card);

        setContentView(root);
        return col;
    }

    /** 右对齐标题（金色大字 + 灰色副标题）。 */
    private void titleBlock(LinearLayout col, String title, String sub) {
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextColor(C_GOLD);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, 34 * uiScale);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setGravity(Gravity.END);
        t.setLetterSpacing(0.04f);
        col.addView(t, matchWidth());
        TextView s = new TextView(this);
        s.setText(sub);
        s.setTextColor(C_DIM);
        s.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13 * uiScale);
        s.setGravity(Gravity.END);
        LinearLayout.LayoutParams lp = matchWidth();
        lp.topMargin = dp(2);
        s.setLayoutParams(lp);
        col.addView(s);
    }

    private LinearLayout.LayoutParams matchWidth() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    /** 右侧菜单行：右对齐、「标签  值」，整行可点。 */
    private View menuRow(String label, String value, float size, int color, boolean bold, Runnable onClick) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(7 * uiScale), 0, dp(7 * uiScale));

        TextView l = new TextView(this);
        l.setText(label);
        l.setTextColor(color);
        l.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        if (bold) l.setTypeface(Typeface.DEFAULT_BOLD);
        l.setGravity(Gravity.END);
        row.addView(l);

        if (value != null) {
            TextView v = new TextView(this);
            v.setText(value);
            v.setTextColor(bold ? C_GOLD : C_GOLD_DIM);
            v.setTextSize(TypedValue.COMPLEX_UNIT_SP, Math.max(12, size - 3));
            v.setGravity(Gravity.END);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.leftMargin = dp(10);
            v.setLayoutParams(lp);
            row.addView(v);
        }

        row.setClickable(true);
        row.setOnClickListener(v2 -> onClick.run());
        return row;
    }

    private TextView infoLine(String text, int color) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(color);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13 * uiScale);
        tv.setGravity(Gravity.END);
        tv.setLineSpacing(sp(2), 1.1f);
        return tv;
    }

    private void spacer(LinearLayout col, float dpHeight) {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(1, dp(dpHeight)));
        col.addView(v);
    }

    /* ================= 一级页 ================= */

    private void buildMain() {
        LinearLayout col = makeRoot();
        titleBlock(col, "节奏天国", "Android 移植  ·  启动设置");
        spacer(col, 18 * uiScale);

        col.addView(menuRow("▶  启动游戏", null, 26 * uiScale, C_GOLD, true, this::launchGame));
        spacer(col, 10 * uiScale);

        statusView = infoLine("", C_DIM);
        refreshStatus();
        col.addView(statusView);
        spacer(col, 16 * uiScale);

        col.addView(menuRow("画面比例", aspectName(), 17 * uiScale, C_TEXT, false, () -> showPage(Page.ASPECT)));
        col.addView(menuRow("震动强度", Settings.HAPTIC_NAMES[Settings.hapticIndex(this)], 17 * uiScale, C_TEXT, false, () -> showPage(Page.HAPTIC)));
        col.addView(menuRow("画面延迟（声画同步）", delayName(), 17 * uiScale, C_TEXT, false, () -> showPage(Page.DELAY)));
        col.addView(menuRow("节拍校准", calibSummary(), 17 * uiScale, C_TEXT, false, () -> showPage(Page.CALIB)));
        col.addView(menuRow("游戏数据", dataSummary(), 17 * uiScale, C_TEXT, false, () -> showPage(Page.DATA)));

        spacer(col, 26 * uiScale);
        TextView hint = infoLine("数据包由电脑端生成（见 make_data_pack.ps1），导入后即可启动", 0x774a5c54);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11 * uiScale);
        hint.setGravity(Gravity.END);
        col.addView(hint);
    }

    private String dataSummary() {
        File d = rhdataDir();
        File image = new File(d, "romfs.img");
        String[] top = new File(d, "romfs").list();
        boolean ok = (image.isFile() && image.length() > 80) || (top != null && top.length > 0);
        return ok ? "已就绪" : "缺失";
    }

    private String aspectName() {
        switch (Settings.aspect(this)) {
            case Settings.ASPECT_STRETCH: return "拉伸铺满";
            case Settings.ASPECT_CROP: return "等比裁切";
            default: return "16:9 黑边";
        }
    }

    private String delayName() {
        int ms = Settings.videoDelayMs(this);
        return ms == 0 ? "关闭" : ("+" + ms + " ms");
    }

    private String calibSummary() {
        int v = Settings.calibMs(this);
        return v == 0 ? "未测" : ("上次 +" + v + " ms");
    }

    /* ================= 二级页 ================= */

    private void addBackRow(LinearLayout col) {
        col.addView(menuRow("‹  返回", null, 15 * uiScale, C_GOLD, true, () -> showPage(Page.MAIN)));
    }

    private void buildSettingPage(String title, String desc, LinearLayout options) {
        LinearLayout col = makeRoot();
        addBackRow(col);
        spacer(col, 6 * uiScale);
        titleBlock(col, title, desc);
        spacer(col, 14 * uiScale);
        col.addView(options);
    }

    /** 单选项行：右侧「●」标记。 */
    private View optionRow(String text, boolean selected, Runnable onSelect) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(9 * uiScale), 0, dp(9 * uiScale));

        TextView l = new TextView(this);
        l.setText(text);
        l.setTextColor(selected ? C_GOLD : C_TEXT);
        l.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17 * uiScale);
        l.setGravity(Gravity.END);
        l.setTypeface(selected ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        row.addView(l);

        TextView mark = new TextView(this);
        mark.setText(selected ? "  ●" : "  ○");
        mark.setTextColor(selected ? C_GOLD : 0x55303f39);
        mark.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14 * uiScale);
        row.addView(mark);

        row.setClickable(true);
        row.setOnClickListener(v -> onSelect.run());
        return row;
    }

    private LinearLayout buildAspectOptions() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.END);
        int cur = Settings.aspect(this);
        box.addView(optionRow("16:9（保持比例，黑边，推荐）", cur == Settings.ASPECT_16_9, () -> {
            Settings.setAspect(this, Settings.ASPECT_16_9); persist(); showPage(Page.ASPECT);
        }));
        box.addView(optionRow("拉伸铺满（画面变形）", cur == Settings.ASPECT_STRETCH, () -> {
            Settings.setAspect(this, Settings.ASPECT_STRETCH); persist(); showPage(Page.ASPECT);
        }));
        box.addView(optionRow("等比裁切铺满（裁掉上下约 10%）", cur == Settings.ASPECT_CROP, () -> {
            Settings.setAspect(this, Settings.ASPECT_CROP); persist(); showPage(Page.ASPECT);
        }));
        return box;
    }

    private LinearLayout buildHapticOptions() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.END);
        int cur = Settings.hapticIndex(this);
        for (int i = 0; i < Settings.HAPTIC_NAMES.length; i++) {
            final int idx = i;
            box.addView(optionRow(Settings.HAPTIC_NAMES[i], cur == i, () -> {
                Settings.setHapticIndex(this, idx); persist(); showPage(Page.HAPTIC);
            }));
        }
        return box;
    }

    private LinearLayout buildDelayOptions() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.END);
        int cur = Settings.delayIndex(this);
        for (int i = 0; i < Settings.DELAY_MS.length; i++) {
            final int idx = i;
            int ms = Settings.DELAY_MS[i];
            String text = ms == 0 ? "关闭（默认）" : ("+" + ms + " ms");
            box.addView(optionRow(text, cur == i, () -> {
                Settings.setDelayIndex(this, idx); persist(); showPage(Page.DELAY);
            }));
        }
        return box;
    }

    /* ================= 节拍校准 ================= */

    private void buildCalibrationPage() {
        LinearLayout col = makeRoot();
        addBackRow(col);
        spacer(col, 6 * uiScale);
        titleBlock(col, "节拍校准", "戴好平时用的耳机或外放；跟着「哒」声点击下方区域");
        spacer(col, 10 * uiScale);

        calStatus = infoLine("准备中…", C_DIM);
        calStatus.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14 * uiScale);
        col.addView(calStatus);

        TextView pad = new TextView(this);
        pad.setText("跟着节拍点这里");
        pad.setTextColor(0xdd1f2b27);
        pad.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17 * uiScale);
        pad.setGravity(Gravity.CENTER);
        GradientDrawable padBg = new GradientDrawable();
        padBg.setShape(GradientDrawable.RECTANGLE);
        padBg.setColor(0x26000000);
        padBg.setCornerRadius(dp(18));
        padBg.setStroke(Math.max(1, dp(1)), 0x33000000);
        pad.setBackground(padBg);
        LinearLayout.LayoutParams padLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, (int) dp(180 * uiScale));
        padLp.topMargin = dp(12 * uiScale);
        pad.setLayoutParams(padLp);
        pad.setOnTouchListener((v, ev) -> {
            if (ev.getActionMasked() == android.view.MotionEvent.ACTION_DOWN) onCalibrationTap(ev.getEventTime());
            return true;
        });
        col.addView(pad);

        calResultBox = new LinearLayout(this);
        calResultBox.setOrientation(LinearLayout.VERTICAL);
        calResultBox.setGravity(Gravity.END);
        LinearLayout.LayoutParams boxLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        boxLp.topMargin = dp(10 * uiScale);
        calResultBox.setLayoutParams(boxLp);
        col.addView(calResultBox);

        TextView hint = infoLine("跟着声音点、别看屏幕估节奏；多测几轮取中间值更稳。", 0x774a5c54);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11 * uiScale);
        col.addView(hint);

        startCalibration();
    }

    private void startCalibration() {
        stopCalibration();
        synchronized (calBeats) { calBeats.clear(); }
        synchronized (calDeltas) { calDeltas.clear(); }
        if (calStatus != null) calStatus.setText("正在播放节拍… 跟着「哒」声点击");
        int minBuf = AudioTrack.getMinBufferSize(CAL_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (minBuf <= 0) minBuf = CAL_RATE / 5;
        AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
        AudioFormat fmt = new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(CAL_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build();
        try {
            calTrack = new AudioTrack.Builder()
                    .setAudioAttributes(attrs).setAudioFormat(fmt)
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(Math.max(minBuf, CAL_RATE / 5) * 2)
                    .build();
            calTrack.play();
        } catch (Throwable t) {
            if (calStatus != null) calStatus.setTextColor(C_BAD);
            if (calStatus != null) calStatus.setText("无法播放校准音：" + t);
            calTrack = null;
            return;
        }
        calRunning = true;
        final AudioTrack track = calTrack;
        calThread = new Thread(() -> {
            short[] click = new short[900];
            for (int i = 0; i < click.length; i++) {
                double env = Math.min(1.0, Math.min(i, click.length - 1 - i) / 80.0);
                click[i] = (short) (Math.sin(2 * Math.PI * 1400.0 * i / CAL_RATE) * 0.55 * env * 32767);
            }
            long frame = 0;
            while (calRunning) {
                synchronized (calBeats) {
                    calBeats.add(frame);
                    if (calBeats.size() > 40) calBeats.remove(0);
                }
                int w = track.write(click, 0, click.length);
                if (w <= 0) break;
                frame += w;
                int rest = CAL_BEAT_FRAMES - click.length;
                short[] silence = new short[rest];
                w = track.write(silence, 0, rest);
                if (w <= 0) break;
                frame += w;
            }
        }, "rh-calib");
        calThread.start();
    }

    private void stopCalibration() {
        calRunning = false;
        Thread th = calThread;
        calThread = null;
        if (th != null) {
            try { th.join(300); } catch (InterruptedException ignored) {}
        }
        AudioTrack track = calTrack;
        calTrack = null;
        if (track != null) {
            try { track.pause(); } catch (Throwable ignored) {}
            try { track.flush(); } catch (Throwable ignored) {}
            try { track.release(); } catch (Throwable ignored) {}
        }
    }

    private void onCalibrationTap(long eventMs) {
        if (!calRunning || calTrack == null) return;
        AudioTimestamp ts = new AudioTimestamp();
        if (!calTrack.getTimestamp(ts)) return;
        long tapNs = System.nanoTime() - (SystemClock.uptimeMillis() - eventMs) * 1000000L;
        long[] beats;
        synchronized (calBeats) { beats = new long[calBeats.size()]; for (int i = 0; i < beats.length; i++) beats[i] = calBeats.get(i); }
        if (beats.length == 0) return;
        double best = 1e9;
        for (long beat : beats) {
            double presentSec = ts.nanoTime * 1e-9 + (beat - ts.framePosition) / (double) CAL_RATE;
            double deltaMs = (tapNs * 1e-9 - presentSec) * 1000.0;
            if (Math.abs(deltaMs) < Math.abs(best)) best = deltaMs;
        }
        if (Math.abs(best) > 250) return;   /* 明显点错拍，丢弃 */
        int count;
        synchronized (calDeltas) {
            calDeltas.add(best);
            count = calDeltas.size();
        }
        if (calStatus != null) {
            calStatus.setText("已采集 " + count + " / " + CAL_TARGET_TAPS + "    本次偏差 " + Math.round(best) + " ms");
        }
        if (count >= CAL_TARGET_TAPS) finishCalibration();
    }

    private void finishCalibration() {
        stopCalibration();
        double median;
        synchronized (calDeltas) {
            if (calDeltas.isEmpty()) return;
            List<Double> copy = new ArrayList<>(calDeltas);
            Collections.sort(copy);
            median = copy.get(copy.size() / 2);
        }
        int suggested = suggestPreset(median);
        Settings.setCalibMs(this, (int) Math.round(median));
        if (calStatus != null) {
            calStatus.setTextColor(C_OK);
            calStatus.setText("平均偏差 " + Math.round(median) + " ms   →   建议 " + (suggested == 0 ? "关闭" : "+" + suggested + " ms"));
        }
        if (calResultBox != null) {
            calResultBox.removeAllViews();
            if (suggested > 0) {
                calResultBox.addView(menuRow("应用建议值  +" + suggested + " ms", null, 17 * uiScale, C_GOLD, true, () -> {
                    int idx = 0;
                    for (int i = 0; i < Settings.DELAY_MS.length; i++) if (Settings.DELAY_MS[i] == suggested) idx = i;
                    Settings.setDelayIndex(this, idx);
                    persist();
                    Toast.makeText(this, "已应用 +" + suggested + " ms 画面延迟", Toast.LENGTH_SHORT).show();
                    showPage(Page.DELAY);
                }));
            }
            calResultBox.addView(menuRow("重新测一次", null, 16 * uiScale, C_TEXT, false, () -> {
                if (calResultBox != null) calResultBox.removeAllViews();
                if (calStatus != null) calStatus.setTextColor(C_DIM);
                startCalibration();
            }));
        }
    }

    /** 把测得偏差取最近档位；0 也是候选（偏差很小时建议关闭）。 */
    private static int suggestPreset(double median) {
        if (median < 8) return 0;
        int best = 0;
        double bestDiff = 1e9;
        for (int ms : Settings.DELAY_MS) {
            double d = Math.abs(ms - median);
            if (d < bestDiff) { bestDiff = d; best = ms; }
        }
        return best;
    }

    /* ================= 数据页 ================= */

    private void buildDataPage() {
        LinearLayout col = makeRoot();
        addBackRow(col);
        spacer(col, 6 * uiScale);
        titleBlock(col, "游戏数据", "数据包导入 / 状态");

        statusView = infoLine("", C_DIM);
        LinearLayout.LayoutParams slp = matchWidth();
        slp.topMargin = dp(14);
        statusView.setLayoutParams(slp);
        refreshStatus();
        col.addView(statusView);

        spacer(col, 12 * uiScale);
        importBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        importBar.setMax(1000);
        importBar.setProgressTintList(ColorStateList.valueOf(C_GOLD));
        importBar.setVisibility(View.GONE);
        LinearLayout.LayoutParams blp = matchWidth();
        blp.height = dp(6);
        importBar.setLayoutParams(blp);
        col.addView(importBar);

        importView = infoLine("", C_DIM);
        importView.setVisibility(View.GONE);
        col.addView(importView);

        spacer(col, 14 * uiScale);
        col.addView(menuRow("导入数据包", "rhdata-pack.zip", 18 * uiScale, C_TEXT, false, this::importFromDefaultPath));
        col.addView(menuRow("选择 zip 文件…", null, 18 * uiScale, C_TEXT, false, () -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_PICK_ZIP);
        }));

        spacer(col, 14 * uiScale);
        TextView hint = infoLine("zip 内需包含 romfs/、modules/、shaders/ 三个目录（可放根目录或任意一层）。\n"
                + "可放到 Android/media/com.rhport.rhythm/（USB 直接考入）。\n需要约 6GB 可用空间。", 0x774a5c54);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11 * uiScale);
        col.addView(hint);
    }

    /* ================= 逻辑 ================= */

    private File rhdataDir() {
        return new File(getExternalMediaDirs()[0], "rhdata");
    }

    private void refreshStatus() {
        refreshStatus(statusView);
    }

    private void refreshStatus(TextView tv) {
        if (tv == null) return;
        File d = rhdataDir();
        File image = new File(d, "romfs.img");
        boolean hasImage = image.isFile() && image.length() > 80;
        String[] top = new File(d, "romfs").list();
        int romfsTop = top == null ? 0 : top.length;
        boolean modules = new File(d, "modules").isDirectory();
        boolean shaders = new File(d, "shaders").isDirectory();
        boolean ready = (hasImage || romfsTop > 0) && modules && shaders;
        String romfsText = hasImage ? ("romfs.img " + (image.length() / (1 << 20)) + " MB")
                : ("romfs " + (romfsTop > 0 ? (romfsTop + " 项") : "缺失"));
        String text = (ready ? "数据：已就绪" : "数据：不完整")
                + "\n" + romfsText
                + "   ·   modules " + (modules ? "OK" : "缺失")
                + "   ·   shaders " + (shaders ? "OK" : "缺失");
        tv.setText(text);
        tv.setTextColor(ready ? C_OK : C_BAD);
    }

    private void persist() {
        Settings.writeEnvFile(this, rhdataDir());
    }

    private void launchGame() {
        Settings.writeEnvFile(this, rhdataDir());
        Intent i = new Intent(this, GameActivity.class);
        i.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);
        startActivity(i);
    }

    @Override
    protected void onActivityResult(int req, int result, Intent data) {
        super.onActivityResult(req, result, data);
        if (req == REQ_PICK_ZIP && result == RESULT_OK && data != null && data.getData() != null) {
            importZip(data.getData(), "已选文件");
        }
    }

    private void importFromDefaultPath() {
        if (importing) return;
        for (String path : DEFAULT_ZIP_PATHS) {
            File f = new File(path);
            if (f.isFile() && f.length() > 0) {
                importZip(Uri.fromFile(f), f.getName());
                return;
            }
        }
        Toast.makeText(this, "没找到数据包：把 rhdata-pack.zip 放到 Android/media/com.rhport.rhythm/ 或 Download/，或用「选择 zip 文件」", Toast.LENGTH_LONG).show();
    }

    /** 后台解压：zip 里找 romfs/modules/shaders（允许外面套一层目录）→ 写到 rhdata/。 */
    private void importZip(Uri uri, String label) {
        if (importing) { Toast.makeText(this, "正在导入中…", Toast.LENGTH_SHORT).show(); return; }
        importing = true;
        if (importBar != null) { importBar.setVisibility(View.VISIBLE); importBar.setProgress(0); }
        if (importView != null) {
            importView.setVisibility(View.VISIBLE);
            importView.setTextColor(C_DIM);
            importView.setText("正在导入 " + label + " …");
        }
        final File base = rhdataDir();
        final long[] lastUi = {0};
        new Thread(() -> {
            long bytes = 0;
            int files = 0;
            Throwable error = null;
            try (InputStream raw = openStream(uri);
                 ZipInputStream zin = new ZipInputStream(new BufferedInputStream(raw, 1 << 20))) {
                if (raw == null) throw new IllegalStateException("打不开文件");
                byte[] buf = new byte[1 << 20];
                ZipEntry e;
                String baseCanon = base.getCanonicalPath();
                while ((e = zin.getNextEntry()) != null) {
                    String name = e.getName().replace('\\', '/');
                    while (name.startsWith("/")) name = name.substring(1);
                    /* 单文件 RomFS 镜像：直接写到 rhdata/romfs.img（native 用 romfs_image 挂载）。 */
                    if (name.equals("romfs.img") || name.endsWith("/romfs.img")) {
                        File out = new File(base, "romfs.img");
                        File parent = out.getParentFile();
                        if (parent != null) parent.mkdirs();
                        try (FileOutputStream fos = new FileOutputStream(out)) {
                            int n;
                            while ((n = zin.read(buf)) > 0) { fos.write(buf, 0, n); bytes += n; }
                        }
                        files++;
                        final long bb = bytes;
                        runOnUiThread(() -> {
                            if (importView != null) {
                                importView.setVisibility(View.VISIBLE);
                                importView.setText("正在导入… romfs.img / " + (bb / (1 << 20)) + " MB");
                            }
                        });
                        continue;
                    }
                    String[] parts = name.split("/");
                    int idx = -1;
                    for (int i = 0; i < parts.length; i++) {
                        if (parts[i].equals("romfs") || parts[i].equals("modules") || parts[i].equals("shaders")) {
                            idx = i;
                            break;
                        }
                    }
                    if (idx < 0) continue;
                    StringBuilder rel = new StringBuilder();
                    for (int i = idx; i < parts.length; i++) {
                        if (parts[i].isEmpty()) continue;
                        if (rel.length() > 0) rel.append('/');
                        rel.append(parts[i]);
                    }
                    if (rel.length() == 0) continue;
                    File out = new File(base, rel.toString());
                    String canon = out.getCanonicalPath();
                    if (!canon.equals(baseCanon) && !canon.startsWith(baseCanon + File.separator)) continue;
                    if (e.isDirectory()) { out.mkdirs(); continue; }
                    File parent = out.getParentFile();
                    if (parent != null) parent.mkdirs();
                    try (FileOutputStream fos = new FileOutputStream(out)) {
                        int n;
                        while ((n = zin.read(buf)) > 0) {
                            fos.write(buf, 0, n);
                            bytes += n;
                        }
                    }
                    files++;
                    long now = SystemClock.uptimeMillis();
                    if (now - lastUi[0] > 250) {
                        lastUi[0] = now;
                        final int ff = files;
                        final long bb = bytes;
                        final String cur = rel.toString();
                        runOnUiThread(() -> {
                            if (importView != null) {
                                importView.setVisibility(View.VISIBLE);
                                importView.setText("正在导入… " + ff + " 个文件 / " + (bb / (1 << 20)) + " MB\n" + cur);
                            }
                            if (importBar != null) importBar.setProgress((int) (now % 1000));
                        });
                    }
                }
            } catch (Throwable t) {
                error = t;
            }
            final Throwable err = error;
            final int filesDone = files;
            final long bytesDone = bytes;
            runOnUiThread(() -> {
                importing = false;
                if (importView != null) {
                    importView.setVisibility(View.VISIBLE);
                    importView.setTextColor(err == null ? C_OK : C_BAD);
                    importView.setText(err == null
                            ? ("导入完成：" + filesDone + " 个文件 / " + (bytesDone / (1 << 20)) + " MB")
                            : ("导入失败：" + err));
                }
                if (importBar != null) importBar.setVisibility(View.GONE);
                Toast.makeText(this, err == null ? "数据导入完成" : ("导入失败：" + err),
                        err == null ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
                refreshStatus();
            });
        }).start();
    }

    private InputStream openStream(Uri uri) throws Exception {
        if ("file".equals(uri.getScheme())) return new FileInputStream(new File(uri.getPath()));
        return getContentResolver().openInputStream(uri);
    }
}
