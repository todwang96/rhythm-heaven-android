package com.rhport.rhythm;

import android.app.Activity;
import android.os.Build;
import android.os.Bundle;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.TypedValue;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.util.Log;

/** 最小宿主：一个全屏 SurfaceView，把 Surface 交给 native，然后起游戏线程。 */
public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "RHPORT";

    static {
        System.loadLibrary("dmandroid");
    }

    private static native int nativeConfigure(String dataDir, String filesDir, float refreshHz);
    private static native int nativeSurface(Surface surface);
    private static native int nativeStart();
    private static native String nativeVersion();
    private static native void nativeTouch(int id, float x, float y, int action);
    private static native void nativeKey(int code, boolean down);
    private static native void nativeSetVibrator(Object vibrator);
    private static native long nativeTouchHint();

    static void sendKey(int code, boolean down) { nativeKey(code, down); }

    /** ss_touch_left_get() 的打包值：layout | first<<8 | second<<16 | extra<<24。 */
    static long touchHint() {
        try { return nativeTouchHint(); } catch (Throwable t) { return 3; }   /* 默认 MENU */
    }

    private SurfaceView view;
    private PadView pad;
    private boolean configured;
    private boolean started;
    private boolean destroyed;

    /** PadView 未命中的触摸继续走原来的「点哪都按 A / 指针目标」逻辑。 */
    void forwardTouch(int id, float x, float y, int action) {
        nativeTouch(id, x, y, action);
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemUi();

        /* 启动器设置（画面比例/震动/音频延迟）→ 写 env.txt，必须在 nativeConfigure 之前。 */
        Settings.writeEnvFile(this, new java.io.File(dataDir()));

        /* 先加载 env.txt / 目录配置，再建 Surface：nativeSurface() 会触发 Vulkan 后端初始化，
         * 而后端在 init 时读取的一批图形调试开关（DM_RT_SAMPLE_FLIP 等）必须在 init 前就绪。 */
        if (!configured) {
            int rc = nativeConfigure(dataDir(), getFilesDir().getAbsolutePath(), refreshHz());
            if (rc != 0) Log.e(TAG, "nativeConfigure(early) rc=" + rc);
            else configured = true;
        }

        /* 震动：把系统 Vibrator 交给 native，游戏通过 nn::hid 发来的马达信号会映射成短脉冲。 */
        Vibrator vibrator = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            VibratorManager manager = getSystemService(VibratorManager.class);
            if (manager != null) vibrator = manager.getDefaultVibrator();
        } else {
            vibrator = getSystemService(Vibrator.class);
        }
        if (vibrator != null && vibrator.hasVibrator()) nativeSetVibrator(vibrator);

        /* 数据包检查：正式包不携带 3.4GB 游戏资源，缺数据时给出明确指引而不是黑屏/崩溃。 */
        String missing = dataCheckError();
        if (missing != null) {
            Log.e(TAG, "game data missing: " + missing + " (root=" + dataDir() + ")");
            setContentView(buildDataMissingView(missing));
            return;
        }

        view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        pad = new PadView(this);
        FrameLayout root = new FrameLayout(this);
        root.addView(view, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        root.addView(pad, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        Log.i(TAG, "activity created, " + nativeVersion());
    }

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
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) hideSystemUi();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        /* 音量键留给系统：否则玩家没法调音量，调试时的系统提示音也发不出来。 */
        if (code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN) {
            return super.dispatchKeyEvent(event);
        }
        int action = event.getAction();
        if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
            if (action == KeyEvent.ACTION_DOWN && event.getKeyCode() == KeyEvent.KEYCODE_PLUS && pad != null) pad.toggleForceDpad();
            nativeKey(event.getKeyCode(), action == KeyEvent.ACTION_DOWN);
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    private float refreshHz() {
        return getDisplay() != null ? getDisplay().getRefreshRate() : 60.0f;
    }

    /** 数据根目录：Android/media/<包名>/rhdata（adb 与 app 都能访问）。 */
    private String dataDir() {
        return new java.io.File(getExternalMediaDirs()[0], "rhdata").getAbsolutePath();
    }

    /** 缺哪个数据目录就返回名字（null = 齐了）。romfs 允许「单文件镜像 romfs.img」或「解包目录」二者其一。 */
    private String dataCheckError() {
        java.io.File root = new java.io.File(dataDir());
        java.io.File image = new java.io.File(root, "romfs.img");
        boolean imageOk = image.isFile() && image.length() > 80;
        for (String name : new String[]{"romfs", "modules", "shaders"}) {
            java.io.File dir = new java.io.File(root, name);
            String[] names = dir.isDirectory() ? dir.list() : null;
            boolean nonEmpty = names != null && names.length > 0;
            if (name.equals("romfs")) {
                if (!imageOk && !nonEmpty) return "romfs";
                continue;
            }
            if (!nonEmpty) return name;
        }
        return null;
    }

    /** 缺数据时的全屏说明页（含"重新检测"按钮，放好数据后点一下即可继续）。 */
    private View buildDataMissingView(String missing) {
        String dir = dataDir();
        TextView tv = new TextView(this);
        tv.setTextColor(0xffe8e8e8);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17f);
        tv.setPadding(56, 48, 56, 32);
        tv.setLineSpacing(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 5, getResources().getDisplayMetrics()), 1.15f);
        tv.setText("缺少游戏数据：" + missing + "\n\n"
                + "数据目录：\n" + dir + "\n\n"
                + "需要把下面这些放进去（三个目录，或者 romfs.img 单文件镜像 + modules/ + shaders/）：\n"
                + "  romfs/ 或 romfs.img   游戏资源（约 3.2 GB）\n"
                + "  modules/              改写后的模块表\n"
                + "  shaders/              Vulkan shader 缓存\n\n"
                + "安装方法：\n"
                + "1. 电脑 USB 连接后运行 rh-port/android/install_release.ps1\n"
                + "2. 或用启动器的「游戏数据 → 导入数据包」\n"
                + "3. 或手动把这些复制到上面的数据目录，然后点「重新检测」\n\n"
                + "Missing game data. Put romfs/ (or romfs.img), modules/ and shaders/\n"
                + "into the folder above, then tap Retry.");
        Button retry = new Button(this);
        retry.setText("重新检测 / Retry");
        retry.setOnClickListener(v -> recreate());
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackgroundColor(0xff1c1c1c);
        box.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.MATCH_PARENT));
        /* 先放按钮再放文字：按钮始终第一屏可见，放好数据后直接点。 */
        box.addView(retry, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        box.addView(tv, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(0xff1c1c1c);
        sv.addView(box);
        return sv;
    }

    private void configureAndStart() {
        if (destroyed) return;
        if (!configured) {
            int rc = nativeConfigure(dataDir(), getFilesDir().getAbsolutePath(), refreshHz());
            if (rc != 0) { Log.e(TAG, "nativeConfigure rc=" + rc); return; }
            configured = true;
        }
        if (!started) {
            int rc = nativeStart();
            if (rc != 0) { Log.e(TAG, "nativeStart rc=" + rc); return; }
            started = true;
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "surfaceCreated");
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (destroyed) return;
        Log.i(TAG, "surfaceChanged " + width + "x" + height);
        int rc = nativeSurface(holder.getSurface());
        if (rc != 0) { Log.e(TAG, "nativeSurface rc=" + rc); return; }
        configureAndStart();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (destroyed) return;
        nativeSurface(null);
    }

    @Override
    protected void onDestroy() {
        destroyed = true;
        super.onDestroy();
    }
}
