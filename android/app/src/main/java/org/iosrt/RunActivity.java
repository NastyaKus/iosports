package org.iosrt;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;

/** Hosts one guest iOS app (runs in the separate ":guest" process). */
public class RunActivity extends Activity implements NativeBridge.Listener {
    public static final String EXTRA_PATH = "path";
    private IosView iosView;
    private TextView log;
    private ScrollView logScroll;
    private boolean started;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        iosView = new IosView(this);
        root.addView(iosView, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 3f));
        log = new TextView(this);
        log.setTextColor(Color.rgb(200, 230, 200));
        log.setBackgroundColor(Color.rgb(16, 16, 16));
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextSize(11);
        log.setPadding(16, 8, 16, 8);
        logScroll = new ScrollView(this);
        logScroll.addView(log);
        root.addView(logScroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.2f));
        setContentView(root);
        NativeBridge.setListener(this);
        // start once the view has its size: it becomes the iOS screen
        iosView.post(this::startGuest);
    }

    private void startGuest() {
        if (started) return;
        started = true;
        final String path = getIntent().getStringExtra(EXTRA_PATH);
        float density = getResources().getDisplayMetrics().density;
        final float w = iosView.getWidth() / density, h = iosView.getHeight() / density;
        /* like an iPhone: integer scale, rendered at most at 2x (the software renderer's fill rate) */
        final float scale = Math.min(2f, Math.max(1f, Math.round(density)));
        iosView.setScreen(w, h);
        final File home = new File(getFilesDir(), "home/" + new File(path).getName());
        home.mkdirs();
        append("[shell] launching " + path + "\n[shell] screen " + Math.round(w) + "x" + Math.round(h) + " pt, rendered @" + Math.min(2, Math.max(1, Math.round(density))) + "x\n");
        Thread t = new Thread(() -> {
            int rc = NativeBridge.nativeRun(path, home.getAbsolutePath(), w, h, scale);
            append("[shell] runtime returned " + rc + "\n");
        }, "iosrt-launcher");
        t.start();
    }

    private void append(String s) {
        runOnUiThread(() -> {
            log.append(s);
            logScroll.post(() -> logScroll.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }

    @Override public void onLog(int fd, String text) { append(text); }
    @Override public void onFrame(java.nio.ByteBuffer px, int w, int h) { iosView.setFrame(px, w, h); }
    @Override public void onExit(int code, String reason) {
        String msg = "[shell] guest exited with code " + code + (reason != null ? ": " + reason : "") + "\n";
        append(msg);
        try (FileOutputStream f = new FileOutputStream(new File(getFilesDir(), "last_exit.txt"))) {
            f.write(msg.getBytes());
        } catch (Exception ignored) { }
    }

    @Override protected void onDestroy() {
        NativeBridge.nativeQuit();
        super.onDestroy();
        // the runtime keeps process-global state: always start the next guest in a fresh process
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
