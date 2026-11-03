package org.iosrt;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

import java.nio.ByteBuffer;

/**
 * Shows the frames rendered by the runtime, aspect-fitted to the view (apps in
 * iOS compatibility mode use a smaller logical screen, like the "zoomed" mode
 * on an iPhone), and forwards touches in frame pixels.
 */
public class IosView extends View {
    private final Object lock = new Object();
    private Bitmap frame;
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF dst = new RectF();

    public IosView(Context c) { super(c); setBackgroundColor(Color.BLACK); }

    /** kept for callers that know the logical size; the frame itself defines the mapping */
    public void setScreen(float w, float h) { }

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

    private void fitRect(int fw, int fh) {
        float k = Math.min(getWidth() / (float) fw, getHeight() / (float) fh);
        float w = fw * k, h = fh * k, x = (getWidth() - w) / 2;
        dst.set(x, 0, x + w, h);   /* top-aligned, centered horizontally */
    }

    @Override protected void onDraw(Canvas canvas) {
        synchronized (lock) {
            if (frame == null) return;
            fitRect(frame.getWidth(), frame.getHeight());
            canvas.drawBitmap(frame, null, dst, paint);
        }
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int phase;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN: phase = 0; break;
            case MotionEvent.ACTION_MOVE: phase = 1; break;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: phase = 2; break;
            default: return true;
        }
        float x, y;
        synchronized (lock) {
            if (frame == null) return true;
            fitRect(frame.getWidth(), frame.getHeight());
            x = (e.getX() - dst.left) * frame.getWidth() / dst.width();
            y = (e.getY() - dst.top) * frame.getHeight() / dst.height();
        }
        NativeBridge.nativeTouch(phase, x, y);
        return true;
    }
}
