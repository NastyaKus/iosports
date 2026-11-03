package org.iosrt;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/** Renders the runtime's UIKit display list (points -> pixels) and forwards touches. */
public class IosView extends View {
    private float[] cmds = new float[0];
    private String[] texts = new String[0];
    private float screenW = 375, screenH = 667;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF r = new RectF();

    public IosView(Context c) { super(c); setBackgroundColor(Color.BLACK); }

    public void setFrame(float[] c, String[] t) {
        if (c.length >= 2) { screenW = c[0]; screenH = c[1]; }
        cmds = c; texts = t;
        postInvalidate();
    }

    private float scale() { return getWidth() / Math.max(1f, screenW); }

    private static int argb(float r, float g, float b, float a) {
        return Color.argb(Math.round(a * 255), Math.round(r * 255), Math.round(g * 255), Math.round(b * 255));
    }

    @Override protected void onDraw(Canvas canvas) {
        float s = scale();
        final int stride = 12;
        for (int i = 0; (2 + i * stride + stride) <= cmds.length; i++) {
            int o = 2 + i * stride;
            int op = (int) cmds[o];
            float x = cmds[o + 1] * s, y = cmds[o + 2] * s, w = cmds[o + 3] * s, h = cmds[o + 4] * s;
            paint.setColor(argb(cmds[o + 5], cmds[o + 6], cmds[o + 7], cmds[o + 8]));
            paint.setStrokeWidth(Math.max(1f, cmds[o + 11] * s));
            r.set(x, y, x + w, y + h);
            switch (op) {
                case 1: paint.setStyle(Paint.Style.FILL); canvas.drawRect(r, paint); break;
                case 6: paint.setStyle(Paint.Style.STROKE); canvas.drawRect(r, paint); break;
                case 3: paint.setStyle(Paint.Style.FILL); canvas.drawOval(r, paint); break;
                case 4: paint.setStyle(Paint.Style.STROKE); canvas.drawOval(r, paint); break;
                case 5: paint.setStyle(Paint.Style.STROKE); canvas.drawLine(x, y, x + w, y + h, paint); break;
                case 2: {
                    String t = i < texts.length ? texts[i] : null;
                    if (t == null) break;
                    paint.setStyle(Paint.Style.FILL);
                    paint.setTextSize(cmds[o + 9] * s);
                    int align = (int) cmds[o + 10];
                    float tw = paint.measureText(t);
                    float tx = align == 1 ? x + (w - tw) / 2 : align == 2 ? x + w - tw : x;
                    Paint.FontMetrics fm = paint.getFontMetrics();
                    float ty = y + h / 2 - (fm.ascent + fm.descent) / 2;
                    canvas.save(); canvas.clipRect(r); canvas.drawText(t, tx, ty, paint); canvas.restore();
                    break;
                }
                default: break;
            }
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        float s = scale();
        int phase;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: phase = 0; break;
            case MotionEvent.ACTION_MOVE: phase = 1; break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: phase = 2; break;
            default: return true;
        }
        NativeBridge.nativeTouch(phase, e.getX() / s, e.getY() / s);
        return true;
    }
}
