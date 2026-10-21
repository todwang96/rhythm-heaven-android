package com.rhport.rhythm;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;

/**
 * iOS �?Touchable 风格的虚拟按键（贴图布局）：
 *   - 左上角常�?B�?
 *   - 右上角常�?+ / −；
 *   - 左下角四个独立方向键（▲ ◀ �?▼，半透明�?+ 三角箭头，多指可同时按出斜方向）�?
 *   - 右下角常�?A�?
 * 所有按键都通过 GameActivity.sendKey() -> nativeKey()，与物理键盘/手柄完全同一条输入路径�?
 */
public final class PadView extends View {
    private static final int K_DPAD_UP = 19, K_DPAD_DOWN = 20, K_DPAD_LEFT = 21, K_DPAD_RIGHT = 22;
    private static final int K_A = 96, K_B = 97, K_PLUS = 108, K_MINUS = 109;

    private static final int B_A = 1, B_B = 2, B_X = 4, B_Y = 8, B_DP_UP = 16, B_DP_DOWN = 32, B_DP_LEFT = 64, B_DP_RIGHT = 128,
            B_PLUS = 4096, B_MINUS = 8192;
    private static final int DPAD_MASK = B_DP_UP | B_DP_DOWN | B_DP_LEFT | B_DP_RIGHT;

    /* 方向键按下的"最小保�?序号：手指抬起后延迟 45ms 再释放，确保游戏按帧轮询时一定能采到
       （触摸点按通常只有 30~60ms，偶尔会跟游戏的采样窗口错开，表现出来的就是"要点两下"）�?*/
    private final int[] dpadSeq = new int[16];

    private final GameActivity activity;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;

    /* 布局（dp�?*/
    private float bCx, bCy, bR;          /* 左上 B */
    private float aCx, aCy, aR;          /* 右下 A */
    private float plusCx, plusCy, minusCx, minusCy, tinyR;   /* 右上 + / �?*/
    private final float[] dpCx = new float[4], dpCy = new float[4];
    private float dpR;
    private float xR, yR, xCx, xCy, yCx, yCy;   /* 可X/Y（按游戏提示显示） */
    private float ybCx, ybCy, ybR;              /* 常驻 Y：放在 B 按钮正下方 */

    /* 场景提示：ss_touch_left_get() 打包值。layout: 0=DPAD 1=ONE 2=TWO 3=MENU；extra bit0=DPAD bit1=X bit2=Y */
    private long hint = 3;
    private boolean showDpad = true, showX = false, showY = false;
    /* 手动 + 呼出方向键的兜底时长 */
    private boolean forceDpad;
    private long forceUntil;
    /* 闲置自动隐藏：关卡里（非菜单）方向键 N 秒没被按过就先淡出，只留很淡的提示点；碰左半屏立刻恢复。 */
    private static final long DPAD_IDLE_HIDE_MS = 6000L;
    private boolean dpadIdleHidden;
    private long lastDpadUse;
    /* ONE/TWO 布局：左半屏按提示按钮分别当�?A/B/方向键；绘制半透明提示�?*/
    private int layoutMode = 3, zoneFirst = -1, zoneSecond = -1;

    /* iOS 风格：A 不常驻，手指按下时在触点显示一个半透明 A 圈�?*/
    private final int[] touchAIds = new int[8];
    private final float[] touchAX = new float[8], touchAY = new float[8];
    private int touchACount;

    private final int[] pointerIds = new int[12];
    private final int[] pointerKeys = new int[12];
    private int pointerCount;

    public PadView(GameActivity host) {
        super(host);
        activity = host;
        density = getResources().getDisplayMetrics().density;
        text.setColor(0xffffffff);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.DEFAULT_BOLD);
        setFocusable(false);
        hintTick.run();
    }

    private final Runnable hintTick = new Runnable() {
        @Override public void run() {
            long h = GameActivity.touchHint();
            boolean changed = h != hint;
            if (changed) hint = h;
            long now = android.os.SystemClock.uptimeMillis();
            if (forceDpad && now > forceUntil) { forceDpad = false; changed = true; }
            /* 闲置自动隐藏：关卡里方向键 N 秒没用过就淡出（菜单/暂停、手动呼出的不隐藏）。 */
            if (!dpadIdleHidden && showDpad && !forceDpad && layoutMode != 3 && now - lastDpadUse > DPAD_IDLE_HIDE_MS) {
                dpadIdleHidden = true; changed = true;
            }
            if (changed) { applyHint(); invalidate(); }
            postDelayed(this, 200);
        }
    };

    private void applyHint() {
        int layout = (int) (hint & 0xff);
        int extra = (int) ((hint >> 24) & 0xff);
        layoutMode = layout;
        zoneFirst = layout == 1 ? (int) ((hint >> 8) & 0xff) : layout == 2 ? (int) ((hint >> 8) & 0xff) : -1;
        zoneSecond = layout == 2 ? (int) ((hint >> 16) & 0xff) : -1;
        /* 菜单/暂停（MENU/DPAD）显示方向键；关卡里隐藏。按 + 时强制显示一段时间�?*/
        long now = android.os.SystemClock.uptimeMillis();
        if (forceDpad && now > forceUntil) forceDpad = false;
        /* 只要场景用到方向（四�?/ 两区 / 单方向），就把十字键显示出来：单方向场景以前只算
           一个区域，却没画方向按钮，导致"这关要方向键但看不到"。| Show the D-pad whenever the
           scene uses any direction; single-direction scenes used to draw nothing. */
        boolean dirLayout = layout == 0 || layout == 2 ||
                (layout == 1 && (zoneFirst == 12 || zoneFirst == 13 || zoneFirst == 14 || zoneFirst == 15));
        showDpad = forceDpad || dirLayout || layout == 3 || (extra & 1) != 0;
        showX = (extra & 2) != 0;
        showY = (extra & 4) != 0;
        lastDpadUse = now;          /* 场景切换时重新显示方向键 */
        dpadIdleHidden = false;
    }

    /** 虚拟/物理 + 键按下时调用：暂停菜单需要方向键，先强制显示；再按一次撤回�?*/
    public void toggleForceDpad() {
        long now = android.os.SystemClock.uptimeMillis();
        if (forceDpad && now < forceUntil) { forceDpad = false; }
        else { forceDpad = true; forceUntil = now + 120000L; }
        applyHint();
        invalidate();
    }

    /** NpadButton 序号 -> 内部位掩码（0=A 1=B 12=Left 13=Up 14=Right 15=Down）�?*/
    private static int npadBit(int button) {
        switch (button) {
            case 0: return B_A;
            case 1: return B_B;
            case 12: return B_DP_LEFT;
            case 13: return B_DP_UP;
            case 14: return B_DP_RIGHT;
            case 15: return B_DP_DOWN;
            default: return 0;
        }
    }

    private static String npadLabel(int button) {
        switch (button) {
            case 0: return "A";
            case 1: return "B";
            case 12: return "◀";
            case 13: return "\u25B2";
            case 14: return "\u25B6";
            case 15: return "\u25BC";
            default: return "";
        }
    }

    /** 左半屏触摸由"触点 A"处理，不再使用固定触摸区�?*/
    private int zoneKeys(float px, float py) { return -1; }

    /** UI 缩放：平板屏幕的 dp 空间比手机大得多（手机 792x360dp，平板 ~1164x777dp），
        固定 dp 尺寸会显得又小又远；按 dp 面积比开方缩放，钳在 1.0~1.5。 */
    private float uiScale = 1f;

    private float dp(float v) { return v * density * uiScale; }

    /** 十字键整体上移量（dp）：原位置下箭头贴到底部/与关卡底�?UI 重叠，用户要求上移一点�?*/
    private static final float DPAD_UP_SHIFT = 36f;

    /** 方向键触摸区半径（dp）：整块圆形区域按扇区判定方向，比四个小圆好按（参�?iOS �?D-pad）�?*/
    private float padR;

    /** 十字键中心（隐藏方向键时这里�?点我呼出"的提示）�?*/
    private float crossCx, crossCy;

    /** 手动呼出方向键 120 秒（关卡里某些关需要方向键，而场景提示可能只给了 A）。 */
    private void revealDpad() {
        forceDpad = true;
        forceUntil = android.os.SystemClock.uptimeMillis() + 120000L;
        showDpad = true;
        lastDpadUse = android.os.SystemClock.uptimeMillis();
        dpadIdleHidden = false;
        invalidate();
    }

    private void layout() {
        float rawW = getWidth() / density, rawH = getHeight() / density;
        uiScale = Math.min(1.7f, Math.max(1.0f, (float)Math.sqrt((rawW * rawH) / (792f * 360f))));
        float w = rawW / uiScale, h = rawH / uiScale;   /* 逻辑 dp 空间：乘 dp() 后正好是屏幕尺寸 */
        float margin = 24;
        bR = 24; bCx = dp(margin + bR); bCy = dp(margin + bR);
        aR = 32; aCx = dp(w - margin - aR); aCy = dp(h - margin - aR);
        tinyR = 21;
        /* + / − 竖向排列（用户要求）：+ 在上、− 在下，贴右上角一列。 */
        plusCx = dp(w - margin - tinyR); plusCy = dp(margin + tinyR);
        minusCx = plusCx; minusCy = plusCy + dp(tinyR * 2 + 14);
        dpR = 25;
        padR = dp(74);   /* 扇区式方向键的触摸半径：整块圆内按角度判定方向 */
        android.util.Log.i("RHPORT", "pad layout " + getWidth() + "x" + getHeight() + " density=" + density +
                " dp=" + w + "x" + h + " uiScale=" + uiScale);
        float cx = margin + dpR + 44, cy = h - margin - dpR - 44 - DPAD_UP_SHIFT;   /* 十字中心（整体上移） */
        crossCx = dp(cx); crossCy = dp(cy);
        dpCx[0] = dp(cx); dpCy[0] = dp(cy - dpR * 2.45f);           /* up */
        dpCx[1] = dp(cx - dpR * 2.45f); dpCy[1] = dp(cy);           /* left */
        dpCx[2] = dp(cx); dpCy[2] = dp(cy + dpR * 2.45f);           /* down */
        dpCx[3] = dp(cx + dpR * 2.45f); dpCy[3] = dp(cy);           /* right */
        xR = 27; yR = 27;
        xCx = dp(w - margin - aR); xCy = dp(h - margin - aR - aR - xR - 14);
        yCx = dp(w - margin - aR - aR - yR - 14); yCy = dp(h - margin - aR);
        /* 常驻 Y 键：B 按钮正下方，尺寸与 B 相同，间距 16dp。 */
        ybR = bR;
        ybCx = bCx;
        ybCy = bCy + dp(bR * 2 + 16);
    }

    private static boolean inside(float px, float py, float cx, float cy, float r, float margin) {
        float dx = px - cx, dy = py - cy;
        float rr = r + margin;
        return dx * dx + dy * dy <= rr * rr;
    }

    /** 返回该位置应发的键位位掩码；-1 = 不是按键区域（交给游戏自�?= A 触摸）�?*/
    private int hitKeys(float px, float py) {
        float m = dp(10);
        if (inside(px, py, bCx, bCy, dp(bR), m)) return B_B;
        if (inside(px, py, plusCx, plusCy, dp(tinyR), m)) return B_PLUS;
        if (inside(px, py, minusCx, minusCy, dp(tinyR), m)) return B_MINUS;
        if (showX && inside(px, py, xCx, xCy, dp(xR), m)) return B_X;
        if (inside(px, py, ybCx, ybCy, dp(ybR), m)) return B_Y;   /* 常驻 Y（B 下方） */
        {
            /* 左半屏整块都是方向区（扇区判定，参�?iOS 版）：不再要求落在小圆里，手指随便按�?
               左下区域都会立刻出方向，按不到的问题就没了；右半屏留�?A。中心只留极小死区�?
               The whole left half is the D-pad: sector by dominant axis so any thumb position
               triggers immediately; the right half is reserved for A. */
            if (px < getWidth() * 0.5f) {
                if (!showDpad) revealDpad();
                /* 碰左半屏就恢复显示，并重新计时（闲置自动隐藏用）。 */
                lastDpadUse = android.os.SystemClock.uptimeMillis();
                if (dpadIdleHidden) { dpadIdleHidden = false; invalidate(); }
                float dx = px - crossCx, dy = py - crossCy;
                if (Math.abs(dx) < dp(12) && Math.abs(dy) < dp(12)) return 0;   /* 十字中心的极小死�?*/
                if (Math.abs(dx) > Math.abs(dy)) return dx < 0 ? B_DP_LEFT : B_DP_RIGHT;
                return dy < 0 ? B_DP_UP : B_DP_DOWN;
            }
        }
        return -1;
    }

    private void sendKey(int bit, boolean down) {
        int code;
        switch (bit) {
            case B_A: code = K_A; break;
            case B_B: code = K_B; break;
            case B_X: code = 99; break;
            case B_Y: code = 100; break;
            case B_DP_UP: code = K_DPAD_UP; break;
            case B_DP_DOWN: code = K_DPAD_DOWN; break;
            case B_DP_LEFT: code = K_DPAD_LEFT; break;
            case B_DP_RIGHT: code = K_DPAD_RIGHT; break;
            case B_PLUS: code = K_PLUS; break;
            case B_MINUS: code = K_MINUS; break;
            default: return;
        }
        if (down && (bit == B_PLUS || bit == B_MINUS)) {
            /* 游戏�?+/�?是按帧轮询的，触屏点击太短会被漏掉：这里保持 140ms 再松开�?*/
            if (bit == B_PLUS) toggleForceDpad();
            GameActivity.sendKey(code, true);
            final int releaseCode = code;
            postDelayed(() -> GameActivity.sendKey(releaseCode, false), 140);
            return;
        }
        if (!down && (bit == B_PLUS || bit == B_MINUS)) return;   /* 松开由上面的定时器完�?*/
        if ((bit & DPAD_MASK) != 0) {
            int bi = Integer.numberOfTrailingZeros(bit);
            if (down) {
                dpadSeq[bi]++;
                GameActivity.sendKey(code, true);
            } else {
                final int seq = ++dpadSeq[bi];
                postDelayed(() -> { if (dpadSeq[bi] == seq) GameActivity.sendKey(code, false); }, 30);
            }
            return;
        }
        GameActivity.sendKey(code, down);
    }

    private void updatePointer(int index, int newKeys) {
        int changed = pointerKeys[index] ^ newKeys;
        for (int bit = 1; bit <= 8192; bit <<= 1) {
            if ((changed & bit) == 0) continue;
            sendKey(bit, (newKeys & bit) != 0);
        }
        pointerKeys[index] = newKeys;
        if (changed != 0) invalidate();   /* 按下/松开立即重绘，显示按键反馈 */
    }

    private void remember(int id, int keys) {
        if (pointerCount >= pointerIds.length) pointerCount = 0;
        pointerIds[pointerCount] = id;
        pointerKeys[pointerCount] = keys;
        pointerCount++;
    }

    private int indexOf(int id) {
        for (int i = 0; i < pointerCount; i++) if (pointerIds[i] == id) return i;
        return -1;
    }

    private void forget(int id) {
        int pi = indexOf(id);
        if (pi < 0) return;
        pointerCount--;
        pointerIds[pi] = pointerIds[pointerCount];
        pointerKeys[pi] = pointerKeys[pointerCount];
    }

    /** A 键只在右半屏触发（左半屏留给方向键，避免够方向键时误�?A）�?*/
    private static final boolean A_RIGHT_HALF_ONLY = true;
    private boolean aAllowed(float x) { return !A_RIGHT_HALF_ONLY || x >= getWidth() * 0.5f; }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked(), idx = e.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int keys = hitKeys(e.getX(idx), e.getY(idx));
                if (keys < 0) keys = zoneKeys(e.getX(idx), e.getY(idx));
                remember(e.getPointerId(idx), 0);
                int pi = indexOf(e.getPointerId(idx));
                if (keys < 0) {
                    if (aAllowed(e.getX(idx))) {
                        activity.forwardTouch(e.getPointerId(idx), e.getX(idx), e.getY(idx), 0);
                        pointerKeys[pi] = -3;
                        addFloatingA(e.getPointerId(idx), e.getX(idx), e.getY(idx));
                    } else {
                        pointerKeys[pi] = -4;   /* 左半屏空白处：A 不触�?*/
                    }
                } else {
                    updatePointer(pi, keys);
                }
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int h = 0; h < e.getPointerCount(); h++) {
                    int pi = indexOf(e.getPointerId(h));
                    if (pi < 0 || pointerKeys[pi] == -3) {
                        activity.forwardTouch(e.getPointerId(h), e.getX(h), e.getY(h), 1);
                        moveFloatingA(e.getPointerId(h), e.getX(h), e.getY(h));
                        continue;
                    }
                    if (pointerKeys[pi] == -4) continue;   /* 左半屏被忽略的触�?*/
                    int keys = hitKeys(e.getX(h), e.getY(h));
                    if (keys < 0) keys = zoneKeys(e.getX(h), e.getY(h));
                    if (keys >= 0) updatePointer(pi, keys);
                    else updatePointer(pi, 0);   /* 滑出按键区就松开，避免方向键卡在按下状�?*/
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int pi = indexOf(e.getPointerId(idx));
                if (pi < 0 || pointerKeys[pi] == -3) {
                    activity.forwardTouch(e.getPointerId(idx), e.getX(idx), e.getY(idx), 2);
                    removeFloatingA(e.getPointerId(idx));
                } else if (pointerKeys[pi] == -4) {
                    forget(e.getPointerId(idx));   /* 左半屏被忽略的触点，抬起时什么都不发 */
                } else { updatePointer(pi, 0); forget(e.getPointerId(idx)); }
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                for (int i = 0; i < pointerCount; i++) {
                    if (pointerKeys[i] >= 0) updatePointer(i, 0);
                    else if (pointerKeys[i] == -3) activity.forwardTouch(pointerIds[i], 0, 0, 3);
                    /* -4：左半屏被忽略的触点，取消时无需通知 */
                }
                pointerCount = 0;
                touchACount = 0;
                return true;
            }
            default:
                return super.onTouchEvent(e);
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        layout();
    }

    @Override
    protected void onDraw(Canvas c) {
        int active = 0;
        for (int i = 0; i < pointerCount; i++) if (pointerKeys[i] >= 0) active |= pointerKeys[i];
        drawButton(c, bCx, bCy, dp(bR), "B", (active & B_B) != 0);
        for (int i = 0; i < touchACount; i++) drawFloatingA(c, touchAX[i], touchAY[i]);
        drawButton(c, plusCx, plusCy, dp(tinyR), "+", (active & B_PLUS) != 0);
        drawButton(c, minusCx, minusCy, dp(tinyR), "\u2212", (active & B_MINUS) != 0);
        if (showX) drawButton(c, xCx, xCy, dp(xR), "X", (active & B_X) != 0);
        drawButton(c, ybCx, ybCy, dp(ybR), "Y", (active & B_Y) != 0);   /* 常驻 Y：B 按钮下方 */
        if (showDpad) {
            if (dpadIdleHidden) drawDpadIdleHint(c);
            else drawDpad(c, active);
        } else {
            /* 隐藏方向键时留一个很淡的提示标记：点它（或方向键原来的位置）会呼出十字键并生效�?*/
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(dp(1.4f));
            paint.setColor(0x33ffffff);
            drawDpadIdleHint(c);
        }
    }

    /** 很淡的"方向键在这里"提示：隐藏状态和闲置淡出状态共用；碰左半屏就恢复大圆。 */
    private void drawDpadIdleHint(Canvas c) {
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.4f));
        paint.setColor(0x33ffffff);
        c.drawCircle(crossCx, crossCy, dp(dpR * 0.62f), paint);
        c.drawLine(crossCx - dp(dpR * 0.34f), crossCy, crossCx + dp(dpR * 0.34f), crossCy, paint);
        c.drawLine(crossCx, crossCy - dp(dpR * 0.34f), crossCx, crossCy + dp(dpR * 0.34f), paint);
    }

    private void drawButton(Canvas c, float cx, float cy, float r, String label, boolean pressed) {
        /* 浅色背景（菜单、白底关卡）上纯白半透明按钮几乎看不见：先垫一层暗阴影，再画白圈。 */
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(pressed ? 0x55000000 : 0x3a000000);
        c.drawCircle(cx, cy, r + dp(3), paint);
        if (pressed) {
            /* 按下反馈：外圈轻微光晕 + 填充提亮 + 描边加粗，文字反色保证可读。 */
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x24ffffff);
            c.drawCircle(cx, cy, r + dp(5), paint);
        }
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(pressed ? 0x77ffffff : 0x2bffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(pressed ? 2.6f : 1.6f));
        paint.setColor(pressed ? 0xffffffff : 0xb4ffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.FILL);
        text.setShadowLayer(dp(2.2f), 0, dp(1.2f), 0x99000000);
        text.setColor(pressed ? 0xff101010 : 0xf0ffffff);
        text.setTextSize(r * 0.95f);
        c.drawText(label, cx, cy + r * 0.34f, text);
        text.clearShadowLayer();
    }

    private void drawFloatingA(Canvas c, float cx, float cy) {
        float r = dp(34);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0x30ffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.6f));
        paint.setColor(0x99ffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.FILL);
        text.setColor(0xffffffff);
        text.setShadowLayer(dp(2.2f), 0, dp(1.2f), 0x99000000);
        text.setTextSize(r * 0.95f);
        c.drawText("A", cx, cy + r * 0.34f, text);
        text.clearShadowLayer();
    }

    private void addFloatingA(int id, float x, float y) {
        if (touchACount >= touchAIds.length) touchACount = 0;
        touchAIds[touchACount] = id; touchAX[touchACount] = x; touchAY[touchACount] = y; touchACount++;
        invalidate();
    }

    private void moveFloatingA(int id, float x, float y) {
        for (int i = 0; i < touchACount; i++) if (touchAIds[i] == id) { touchAX[i] = x; touchAY[i] = y; invalidate(); return; }
    }

    private void removeFloatingA(int id) {
        for (int i = 0; i < touchACount; i++) if (touchAIds[i] == id) {
            touchACount--; touchAIds[i] = touchAIds[touchACount]; touchAX[i] = touchAX[touchACount]; touchAY[i] = touchAY[touchACount];
            invalidate(); return;
        }
    }

    private void drawZone(Canvas c, float cx, float cy, String label) {
        float r = dp(46);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0x18ffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.2f));
        paint.setColor(0x55ffffff);
        c.drawCircle(cx, cy, r, paint);
        text.setTextSize(r * 0.9f);
        paint.setStyle(Paint.Style.FILL);
        c.drawText(label, cx, cy + r * 0.3f, text);
    }

    private void drawArrow(Canvas c, float cx, float cy, float degrees) {
        float r = dp(dpR);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0x2bffffff);
        c.drawCircle(cx, cy, r, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.6f));
        paint.setColor(0xb4ffffff);
        c.drawCircle(cx, cy, r, paint);
        Path p = new Path();
        float s = r * 0.52f;
        p.moveTo(0, -s);
        p.lineTo(s * 0.9f, s * 0.6f);
        p.lineTo(-s * 0.9f, s * 0.6f);
        p.close();
        c.save();
        c.translate(cx, cy);
        c.rotate(degrees);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(0xf0ffffff);
        c.drawPath(p, paint);
        c.restore();
    }

    /** 只画箭头三角形（不画外圈），带颜色/大小参数，按下反馈用；带深色描边保证浅色背景可见。 */
    private void drawArrowGlyphColor(Canvas c, float cx, float cy, float degrees, float size, int color) {
        Path p = new Path();
        float s = size;
        p.moveTo(0, -s);
        p.lineTo(s * 0.9f, s * 0.6f);
        p.lineTo(-s * 0.9f, s * 0.6f);
        p.close();
        c.save();
        c.translate(cx, cy);
        c.rotate(degrees);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(2.4f));
        paint.setColor(0x70000000);
        c.drawPath(p, paint);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(color);
        c.drawPath(p, paint);
        c.restore();
    }

    /** 只画箭头三角形（不画外圈），给扇区式 D-pad 用。 */
    private void drawArrowGlyph(Canvas c, float cx, float cy, float degrees, float size) {
        drawArrowGlyphColor(c, cx, cy, degrees, size, 0xf0ffffff);
    }

    /** 扇区式 D-pad：一个大圆（整块可触）+ 四个方向的箭头图标 + 中心小圆提示。
     *  按下时高亮对应方向（亮圈 + 更亮的箭头）。 */
    private void drawDpad(Canvas c, int active) {
        int dirs = active & DPAD_MASK;
        float R = padR;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(dirs != 0 ? 0x30000000 : 0x22000000);
        c.drawCircle(crossCx, crossCy, R, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(dirs != 0 ? 2.6f : 1.8f));
        paint.setColor(dirs != 0 ? 0xccffffff : 0x99ffffff);
        c.drawCircle(crossCx, crossCy, R, paint);
        float g = R * 0.55f, s = dp(16);
        drawDpadDir(c, crossCx, crossCy - g, 0, s, (dirs & B_DP_UP) != 0);
        drawDpadDir(c, crossCx - g, crossCy, 270, s, (dirs & B_DP_LEFT) != 0);
        drawDpadDir(c, crossCx + g, crossCy, 90, s, (dirs & B_DP_RIGHT) != 0);
        drawDpadDir(c, crossCx, crossCy + g, 180, s, (dirs & B_DP_DOWN) != 0);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(dp(1.2f));
        paint.setColor(0x33ffffff);
        c.drawCircle(crossCx, crossCy, dp(7), paint);
    }

    private void drawDpadDir(Canvas c, float cx, float cy, float degrees, float size, boolean pressed) {
        if (pressed) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x40ffffff);
            c.drawCircle(cx, cy, size * 1.9f, paint);
        }
        drawArrowGlyphColor(c, cx, cy, degrees, pressed ? size * 1.15f : size,
                            pressed ? 0xffffffff : 0xddffffff);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(MeasureSpec.getSize(widthMeasureSpec), MeasureSpec.getSize(heightMeasureSpec));
    }
}
