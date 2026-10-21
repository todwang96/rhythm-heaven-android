package com.rhport.rhythm;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.RenderEffect;
import android.graphics.RuntimeShader;
import android.os.Build;
import android.util.Log;
import android.view.View;

/**
 * 「液态玻璃」面板：模糊底图（按 CENTER_CROP 对齐）+ 深色 tint + 边缘折射。
 *
 * 关键：整块 view 先铺满原分辨率背景（不透明），再把磨砂玻璃画在圆角路径内。
 * 这样圆角外就是背景本身——不会出现「透明角落被渲染成白色」的驱动问题（手机上四个角发白），
 * 渲染效果也和圆角矩形直接镂空视觉一致。
 *
 * 边缘折射用 AGSL（API 33+）：对 view 自身内容做 RenderEffect，按圆角矩形的距离场
 * 在边缘一条带内做轻微采样位移；路径外的像素原样返回（保留背景），无需透明。
 * 边框由 {@link BorderView} 单独画，避免被折射扭曲。
 */
final class GlassPanel extends View {
    private static final String EDGE_SHADER =
            "uniform shader content;\n" +
            "uniform float2 size;\n" +
            "uniform float radius;\n" +
            "uniform float edge;\n" +
            "uniform float strength;\n" +
            "\n" +
            "float sdRoundBox(float2 p, float2 b, float r) {\n" +
            "    float2 q = abs(p) - b + r;\n" +
            "    return length(max(q, float2(0.0))) + min(max(q.x, q.y), 0.0) - r;\n" +
            "}\n" +
            "\n" +
            "half4 main(float2 coord) {\n" +
            "    float2 halfSize = size * 0.5;\n" +
            "    float2 p = coord - halfSize;\n" +
            "    float d = sdRoundBox(p, halfSize, radius);\n" +
            "    float2 grad = float2(\n" +
            "        sdRoundBox(p + float2(1.0, 0.0), halfSize, radius) - sdRoundBox(p - float2(1.0, 0.0), halfSize, radius),\n" +
            "        sdRoundBox(p + float2(0.0, 1.0), halfSize, radius) - sdRoundBox(p - float2(0.0, 1.0), halfSize, radius));\n" +
            "    float len = length(grad);\n" +
            "    grad = len > 0.0001 ? grad / len : float2(0.0, -1.0);\n" +
            "    float t = clamp(-d / edge, 0.0, 1.0);\n" +
            "    float bend = (1.0 - t) * (1.0 - t);\n" +
            "    /* 四个圆角处淡出位移，避免折射在角上拉出斜向折痕 */\n" +
            "    float2 cornerA = float2(radius, radius);\n" +
            "    float2 cornerB = float2(size.x - radius, radius);\n" +
            "    float2 cornerC = float2(radius, size.y - radius);\n" +
            "    float2 cornerD = float2(size.x - radius, size.y - radius);\n" +
            "    float dCorner = min(min(distance(coord, cornerA), distance(coord, cornerB)),\n" +
            "                        min(distance(coord, cornerC), distance(coord, cornerD)));\n" +
            "    float cornerFade = smoothstep(radius * 0.55, radius * 1.7, dCorner);\n" +
            "    float2 sampleCoord = coord - grad * bend * strength * cornerFade;\n" +
            "    sampleCoord = clamp(sampleCoord, float2(0.5), size - 0.5);\n" +
            "    half4 glassColor = content.eval(sampleCoord);\n" +
            "    glassColor.rgb *= 1.0 + bend * 0.08;\n" +
            "    float spec = pow(clamp(dot(grad, normalize(float2(-0.55, -0.84))), 0.0, 1.0), 3.0);\n" +
            "    glassColor.rgb += spec * bend * (0.10 + 0.18 * cornerFade);\n" +
            "    /* 路径外原样返回背景；路径内用折射后的玻璃，边界 2px 过渡 */\n" +
            "    half4 own = content.eval(coord);\n" +
            "    float inside = smoothstep(0.5, -1.5, d);\n" +
            "    return mix(own, glassColor, inside);\n" +
            "}\n";

    private final Paint bitmapPaint = new Paint(Paint.FILTER_BITMAP_FLAG | Paint.ANTI_ALIAS_FLAG);
    private final Paint tintPaint = new Paint();
    private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();
    private final Rect src = new Rect();
    private final Rect dst = new Rect();
    private final int[] location = new int[2];
    private final Bitmap blurred;
    private final Bitmap sharp;
    private final int imageWidth, imageHeight;
    private final float radius;
    private final float edgeBand;
    private final float bendStrength;
    private RuntimeShader shader;

    GlassPanel(Context context, int backgroundRes) {
        super(context);
        setWillNotDraw(false);
        float density = getResources().getDisplayMetrics().density;
        radius = 32f * density;
        edgeBand = 36f * density;
        bendStrength = 24f * density;
        tintPaint.setColor(0x66FFFFFF);   /* 乳白玻璃 tint（约 40% 白） */
        glowPaint.setColor(0x40FFFFFF);

        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        bounds.inScaled = false;
        BitmapFactory.decodeResource(context.getResources(), backgroundRes, bounds);
        imageWidth = Math.max(1, bounds.outWidth);
        imageHeight = Math.max(1, bounds.outHeight);
        blurred = createBlurred(context, backgroundRes);
        sharp = createSharp(context, backgroundRes);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            try {
                shader = new RuntimeShader(EDGE_SHADER);
            } catch (Throwable ignored) {
                shader = null;
            }
        }
    }

    /** 原分辨率取样：既用来铺满整块 view 的边缘（不透明），也作为玻璃里的一层淡反射。 */
    private static Bitmap createSharp(Context context, int res) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        return BitmapFactory.decodeResource(context.getResources(), res, options);
    }

    /** 缩小到 1/2 + 对称盒式模糊，放大后就是柔和的磨砂效果。 */
    private static Bitmap createBlurred(Context context, int res) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inScaled = false;
        options.inPreferredConfig = Bitmap.Config.ARGB_8888;
        options.inSampleSize = 2;
        Bitmap small = BitmapFactory.decodeResource(context.getResources(), res, options);
        if (small == null) return null;
        return boxBlur(small, 5);
    }

    /** 可分离盒式模糊（对称窗口，水平 + 垂直两遍）。 */
    private static Bitmap boxBlur(Bitmap source, int radius) {
        int w = source.getWidth(), h = source.getHeight();
        int[] pixels = new int[w * h];
        source.getPixels(pixels, 0, w, 0, 0, w, h);
        int[] temp = new int[w * h];
        blurPass(pixels, temp, w, h, radius, true);
        blurPass(temp, pixels, w, h, radius, false);
        Bitmap result = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        result.setPixels(pixels, 0, w, 0, 0, w, h);
        source.recycle();
        return result;
    }

    private static void blurPass(int[] in, int[] out, int w, int h, int radius, boolean horizontal) {
        int outer = horizontal ? h : w;
        int inner = horizontal ? w : h;
        for (int o = 0; o < outer; o++) {
            int base = horizontal ? o * w : o;
            int rSum = 0, gSum = 0, bSum = 0, aSum = 0, count = 0;
            for (int i = -radius; i <= radius; i++) {
                int k = Math.max(0, Math.min(inner - 1, i));
                int p = in[horizontal ? base + k : base + k * w];
                rSum += (p >> 16) & 0xFF; gSum += (p >> 8) & 0xFF; bSum += p & 0xFF; aSum += p >>> 24;
                count++;
            }
            for (int i = 0; i < inner; i++) {
                out[horizontal ? base + i : base + i * w] =
                        (rSum / count) << 16 | (gSum / count) << 8 | (bSum / count) | (aSum / count) << 24;
                int add = Math.max(0, Math.min(inner - 1, i + radius + 1));
                int remove = Math.max(0, Math.min(inner - 1, i - radius));
                int pAdd = in[horizontal ? base + add : base + add * w];
                int pRemove = in[horizontal ? base + remove : base + remove * w];
                rSum += ((pAdd >> 16) & 0xFF) - ((pRemove >> 16) & 0xFF);
                gSum += ((pAdd >> 8) & 0xFF) - ((pRemove >> 8) & 0xFF);
                bSum += (pAdd & 0xFF) - (pRemove & 0xFF);
                aSum += (pAdd >>> 24) - (pRemove >>> 24);
            }
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        rect.set(0, 0, w, h);
        path.reset();
        path.addRoundRect(rect, radius, radius, Path.Direction.CW);
        if (shader != null && w > 0 && h > 0) {
            try {
                shader.setFloatUniform("size", (float) w, (float) h);
                shader.setFloatUniform("radius", radius);
                shader.setFloatUniform("edge", edgeBand);
                shader.setFloatUniform("strength", bendStrength);
                setRenderEffect(RenderEffect.createRuntimeShaderEffect(shader, "content"));
                Log.i("GlassPanel", "AGSL edge refraction on (" + w + "x" + h + ", strength=" + bendStrength + ")");
            } catch (Throwable t) {
                Log.w("GlassPanel", "AGSL edge refraction failed, fallback to plain frost", t);
            }
        }
    }

    /** 按 ImageView.CENTER_CROP 的缩放/偏移，把屏幕坐标映射成位图 src 区域。 */
    private static void mapSource(Rect out, Bitmap bitmap, int screenW, int screenH,
                                  int imageWidth, int imageHeight, int[] location, int w, int h) {
        float scale = Math.max((float) screenW / imageWidth, (float) screenH / imageHeight);
        float displayW = imageWidth * scale, displayH = imageHeight * scale;
        float offsetX = (screenW - displayW) / 2f, offsetY = (screenH - displayH) / 2f;
        float bx = (float) bitmap.getWidth() / imageWidth;
        float by = (float) bitmap.getHeight() / imageHeight;
        out.set(
                Math.round((location[0] - offsetX) / scale * bx),
                Math.round((location[1] - offsetY) / scale * by),
                Math.round((location[0] + w - offsetX) / scale * bx),
                Math.round((location[1] + h - offsetY) / scale * by));
        out.left = Math.max(0, Math.min(out.left, bitmap.getWidth() - 1));
        out.top = Math.max(0, Math.min(out.top, bitmap.getHeight() - 1));
        out.right = Math.max(out.left + 1, Math.min(out.right, bitmap.getWidth()));
        out.bottom = Math.max(out.top + 1, Math.min(out.bottom, bitmap.getHeight()));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        View root = getRootView();
        int screenW = root.getWidth() > 0 ? root.getWidth() : w;
        int screenH = root.getHeight() > 0 ? root.getHeight() : h;
        getLocationOnScreen(location);
        dst.set(0, 0, w, h);

        /* 1) 整块先铺满原分辨率背景（不透明）——圆角外就是背景，避免透明角发白 */
        if (sharp != null) {
            mapSource(src, sharp, screenW, screenH, imageWidth, imageHeight, location, w, h);
            bitmapPaint.setAlpha(0xFF);
            canvas.drawBitmap(sharp, src, dst, bitmapPaint);
        }

        /* 2) 圆角路径内再画磨砂玻璃（模糊底图 + tint + 淡反射 + 顶部高光） */
        canvas.save();
        canvas.clipPath(path);
        if (blurred != null) {
            mapSource(src, blurred, screenW, screenH, imageWidth, imageHeight, location, w, h);
            bitmapPaint.setAlpha(0xFF);
            canvas.drawBitmap(blurred, src, dst, bitmapPaint);
        }
        canvas.drawRect(0, 0, w, h, tintPaint);
        if (sharp != null) {
            mapSource(src, sharp, screenW, screenH, imageWidth, imageHeight, location, w, h);
            bitmapPaint.setAlpha(0x22);
            canvas.drawBitmap(sharp, src, dst, bitmapPaint);
            bitmapPaint.setAlpha(0xFF);
        }
        canvas.drawRect(0, 0, w, 1.4f * getResources().getDisplayMetrics().density, glowPaint);
        canvas.restore();
    }

    /** 单独画边框，避免被边缘折射扭曲。 */
    static final class BorderView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF rect = new RectF();
        private final float radius;

        BorderView(Context context) {
            super(context);
            setWillNotDraw(false);
            float density = getResources().getDisplayMetrics().density;
            radius = 32f * density;
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(1.4f * density);
            paint.setColor(0x26000000);
        }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            super.onSizeChanged(w, h, oldw, oldh);
            rect.set(0, 0, w, h);
            path.reset();
            path.addRoundRect(rect, radius, radius, Path.Direction.CW);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            canvas.drawPath(path, paint);
        }
    }
}
