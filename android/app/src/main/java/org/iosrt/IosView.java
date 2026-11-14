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
 * Shows the frames rendered by the runtime, aspect-fitted and centered in the view,
 * and forwards every finger (pointer id, phase, position in frame pixels).
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
        float w = fw * k, h = fh * k, x = (getWidth() - w) / 2, y = (getHeight() - h) / 2;
        dst.set(x, y, x + w, y + h);
    }

    @Override protected void onDraw(Canvas canvas) {
        synchronized (lock) {
            if (frame == null) return;
            fitRect(frame.getWidth(), frame.getHeight());
            canvas.drawBitmap(frame, null, dst, paint);
        }
    }

    private void send(MotionEvent e, int index, int phase) {
        float x, y;
        synchronized (lock) {
            if (frame == null) return;
            fitRect(frame.getWidth(), frame.getHeight());
            x = (e.getX(index) - dst.left) * frame.getWidth() / dst.width();
            y = (e.getY(index) - dst.top) * frame.getHeight() / dst.height();
        }
        NativeBridge.nativeTouch(e.getPointerId(index), phase, x, y);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                send(e, e.getActionIndex(), 0);
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) send(e, i, 1);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                send(e, e.getActionIndex(), 2);
                break;
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < e.getPointerCount(); i++) send(e, i, 2);
                break;
            default:
                break;
        }
        return true;
    }
}
