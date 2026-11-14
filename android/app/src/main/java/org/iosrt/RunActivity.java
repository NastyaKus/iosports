package org.iosrt;

import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;

/**
 * Hosts one guest iOS app (runs in the separate ":guest" process), full screen:
 * the activity takes the app's orientation, system bars are hidden, and the log
 * is an overlay toggled with the volume-up key (it is also written to
 * files/last_log.txt).
 */
public class RunActivity extends Activity implements NativeBridge.Listener {
    public static final String EXTRA_PATH = "path";
    /** stretch apps made for a fixed iPhone size to the whole display */
    public static final String EXTRA_FILL = "fill";
    private IosView iosView;
    private TextView log;
    private ScrollView logScroll;
    private boolean started;
    private boolean landscape;
    private FileWriter logFile;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        final String path = getIntent().getStringExtra(EXTRA_PATH);
        landscape = path != null && NativeBridge.nativeIsLandscape(path);
        setRequestedOrientation(landscape ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                                          : ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            w.setAttributes(lp);
        }

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        iosView = new IosView(this);
        root.addView(iosView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        log = new TextView(this);
        log.setTextColor(Color.rgb(200, 230, 200));
        log.setTypeface(Typeface.MONOSPACE);
        log.setTextSize(10);
        log.setPadding(16, 8, 16, 8);
        logScroll = new ScrollView(this);
        logScroll.setBackgroundColor(Color.argb(210, 16, 16, 16));
        logScroll.addView(log);
        logScroll.setVisibility(View.GONE);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, Gravity.BOTTOM);
        root.addView(logScroll, lp);
        setContentView(root);
        hideSystemBars();
        try { logFile = new FileWriter(new File(getFilesDir(), "last_log.txt"), false); } catch (Exception ignored) { }
        NativeBridge.setListener(this);
        // start once the view has its final, full-screen size in the app's orientation: it becomes the iOS screen
        iosView.addOnLayoutChangeListener((v, l, t, r, bt, ol, ot, or, ob) -> maybeStart());
        Toast.makeText(this, "Volume up: show/hide log", Toast.LENGTH_SHORT).show();
    }

    private void hideSystemBars() {
        Window w = getWindow();
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void maybeStart() {
        int w = iosView.getWidth(), h = iosView.getHeight();
        if (started || w == 0 || h == 0) return;
        if (landscape != (w > h)) return;          // still rotating
        started = true;
        // let the bars finish hiding before the size is taken
        iosView.postDelayed(this::startGuest, 250);
    }

    private void startGuest() {
        final String path = getIntent().getStringExtra(EXTRA_PATH);
        float density = getResources().getDisplayMetrics().density;
        final float w = iosView.getWidth() / density, h = iosView.getHeight() / density;
        /* like an iPhone: integer scale, rendered at most at 2x (the software renderer's fill rate) */
        final float scale = Math.min(2f, Math.max(1f, Math.round(density)));
        iosView.setScreen(w, h);
        ViewGroup.LayoutParams lp = logScroll.getLayoutParams();
        lp.height = iosView.getHeight() / 2;
        logScroll.setLayoutParams(lp);
        NativeBridge.nativeSetEnv("IOSRT_FILL", getIntent().getBooleanExtra(EXTRA_FILL, false) ? "1" : null);
        final File home = new File(getFilesDir(), "home/" + new File(path).getName());
        home.mkdirs();
        append("[shell] launching " + path + "\n[shell] display " + Math.round(w) + "x" + Math.round(h) + " pt, rendered @"
                + Math.min(2, Math.max(1, Math.round(density))) + "x" + (landscape ? ", landscape app" : "") + "\n");
        Thread t = new Thread(() -> {
            int rc = NativeBridge.nativeRun(path, home.getAbsolutePath(), w, h, scale);
            append("[shell] runtime returned " + rc + "\n");
        }, "iosrt-launcher");
        t.start();
    }

    private void append(String s) {
        synchronized (this) {
            if (logFile != null) { try { logFile.write(s); logFile.flush(); } catch (Exception ignored) { } }
        }
        runOnUiThread(() -> {
            log.append(s);
            CharSequence all = log.getText();   // keep the on-screen log bounded; the file has everything
            if (all.length() > 400000) log.setText(all.subSequence(all.length() - 200000, all.length()));
            if (logScroll.getVisibility() == View.VISIBLE) logScroll.post(() -> logScroll.fullScroll(ScrollView.FOCUS_DOWN));
        });
    }

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_VOLUME_UP) {
            boolean show = logScroll.getVisibility() != View.VISIBLE;
            logScroll.setVisibility(show ? View.VISIBLE : View.GONE);
            if (show) logScroll.post(() -> logScroll.fullScroll(ScrollView.FOCUS_DOWN));
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override public void onLog(int fd, String text) { append(text); }
    @Override public void onFrame(java.nio.ByteBuffer px, int w, int h) { iosView.setFrame(px, w, h); }
    @Override public void onExit(int code, String reason) {
        String msg = "[shell] guest exited with code " + code + (reason != null ? ": " + reason : "") + "\n";
        append(msg);
        runOnUiThread(() -> logScroll.setVisibility(View.VISIBLE));
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
