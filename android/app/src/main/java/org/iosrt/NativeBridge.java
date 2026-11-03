package org.iosrt;

import java.nio.charset.StandardCharsets;

/** JNI bridge to libiosrt.so (the iOS compatibility runtime). */
public final class NativeBridge {
    static { System.loadLibrary("iosrt"); }

    public interface Listener {
        void onLog(int fd, String text);
        void onFrame(float[] cmds, String[] texts);
        void onExit(int code, String reason);
    }
    private static volatile Listener listener;
    public static void setListener(Listener l) { listener = l; }

    /** Static compatibility analysis (no guest code is executed). */
    public static native String nativeReport(String path);
    /** Runs the guest until it exits; blocks the calling thread. */
    public static native int nativeRun(String path, String homeDir, float widthPt, float heightPt, float scale);
    public static native void nativeTouch(int phase, float xPt, float yPt);
    public static native void nativeQuit();

    // called from native code (any thread)
    static void onLog(int fd, byte[] utf8) {
        Listener l = listener;
        if (l != null) l.onLog(fd, new String(utf8, StandardCharsets.UTF_8));
    }
    static void onFrame(float[] cmds, String[] texts) {
        Listener l = listener;
        if (l != null) l.onFrame(cmds, texts);
    }
    static void onExit(int code, String reason) {
        Listener l = listener;
        if (l != null) l.onExit(code, reason);
    }
}
