package org.iosrt;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.MotionEvent;
import android.view.View;

import java.nio.ByteBuffer;

/** Shows the frames rendered by the runtime (scaled to the view) and forwards touches in points. */
public class IosView extends View {
    private final Object lock = new Object();
    private Bitmap frame;
    private float screenW = 375, screenH = 667;
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Rect dst = new Rect();

    public IosView(Context c) { super(c); setBackgroundColor(Color.BLACK); }

    /** logical iOS screen size in points (the view's size divided by its density) */
    public void setScreen(float w, float h) { screenW = w; screenH = h; }

    /** called on the runtime's main thread with premultiplied RGBA pixels */
    public void setFrame(ByteBuffer px, int w, int h) {
        synchronized (lock) {
            if (frame == null || frame.getWidth() != w || frame.getHeight() != h) {
                frame = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            }
            px.rewind();
            frame.copyPixelsFromBuffer(px);
        }
        postInvalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        synchronized (lock) {
            if (frame == null) return;
            dst.set(0, 0, Math.round(screenW * getWidth() / Math.max(1f, screenW)), Math.round(screenH * getWidth() / Math.max(1f, screenW)));
            canvas.drawBitmap(frame, null, dst, paint);
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        float s = getWidth() / Math.max(1f, screenW);
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
