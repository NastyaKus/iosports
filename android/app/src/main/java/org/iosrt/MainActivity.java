package org.iosrt;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Shell: lists bundled demo apps and imports user-supplied .ipa / .app.zip / Mach-O files. */
public class MainActivity extends Activity {
    private static final int PICK = 1;
    private LinearLayout list;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        ScrollView sv = new ScrollView(this);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding(32, 48, 32, 32);
        sv.addView(list);
        setContentView(sv);
        sv.setBackgroundColor(Color.rgb(24, 24, 32));
        refresh();
    }

    @Override protected void onResume() { super.onResume(); refresh(); }

    private void refresh() {
        list.removeAllViews();
        title("iosrt — iOS (arm64 Mach-O) compatibility runtime, stage 1");
        note("Runs iOS app code natively on arm64: own Mach-O loader, Objective-C runtime and "
                + "HLE Foundation / UIKit-lite / AudioToolbox. Real App Store apps need far more APIs "
                + "than stage 1 provides: use \"Compatibility report\" to see what is missing.");
        File last = new File(getFilesDir(), "last_exit.txt");
        if (last.exists()) note("Last run: " + readText(last).trim());
        title("Bundled demo apps (built from source with the iosrt mini-SDK)");
        File samples = new File(getFilesDir(), "samples");
        copyAssets("samples", samples);
        File[] apps = samples.listFiles();
        if (apps != null) {
            java.util.Arrays.sort(apps);
            for (File a : apps) addApp(a);
        }
        title("Imported apps");
        File imported = new File(getFilesDir(), "apps");
        File[] im = imported.listFiles();
        if (im != null) for (File a : im) addApp(a);
        Button pick = new Button(this);
        pick.setText("Import .ipa / zipped .app / Mach-O…");
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, PICK);
        });
        list.addView(pick);
    }

    private void addApp(File app) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        Button run = new Button(this);
        run.setText("▶ " + app.getName().replace(".app", ""));
        run.setAllCaps(false);
        run.setOnClickListener(v -> {
            Intent i = new Intent(this, RunActivity.class);
            i.putExtra(RunActivity.EXTRA_PATH, app.getAbsolutePath());
            startActivity(i);
        });
        Button rep = new Button(this);
        rep.setText("Compatibility report");
        rep.setAllCaps(false);
        rep.setOnClickListener(v -> showReport(app));
        row.addView(run, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.4f));
        row.addView(rep, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        list.addView(row);
    }

    private void showReport(File app) {
        String r = NativeBridge.nativeReport(app.getAbsolutePath());
        TextView tv = new TextView(this);
        tv.setText(r);
        tv.setTypeface(Typeface.MONOSPACE);
        tv.setTextSize(11);
        tv.setPadding(24, 24, 24, 24);
        tv.setTextIsSelectable(true);
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        new AlertDialog.Builder(this).setTitle(app.getName()).setView(sv).setPositiveButton("OK", null).show();
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != PICK || res != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        try {
            File tmp = new File(getCacheDir(), "import.bin");
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(tmp)) { pipe(in, out); }
            File dest = importFile(tmp, guessName(uri));
            note("Imported " + dest.getName());
            refresh();
        } catch (Exception e) {
            new AlertDialog.Builder(this).setTitle("Import failed").setMessage(String.valueOf(e)).setPositiveButton("OK", null).show();
        }
    }

    private String guessName(Uri uri) {
        String s = uri.getLastPathSegment();
        if (s == null) return "app";
        s = s.substring(s.lastIndexOf('/') + 1).replace(':', '_');
        return s.isEmpty() ? "app" : s;
    }

    /** .ipa / zip: extract the first Payload/*.app (or any *.app) ; otherwise treat as a bare Mach-O. */
    private File importFile(File f, String name) throws Exception {
        File apps = new File(getFilesDir(), "apps");
        apps.mkdirs();
        byte[] magic = new byte[4];
        try (InputStream in = new FileInputStream(f)) { if (in.read(magic) != 4) throw new Exception("file too small"); }
        if (magic[0] == 'P' && magic[1] == 'K') {
            File out = null;
            try (ZipInputStream z = new ZipInputStream(new FileInputStream(f))) {
                ZipEntry e;
                while ((e = z.getNextEntry()) != null) {
                    String n = e.getName();
                    int i = n.indexOf(".app/");
                    if (i < 0 || n.contains("..")) continue;
                    String appDir = n.substring(n.lastIndexOf('/', i) + 1, i + 4);
                    String rel = n.substring(i + 5);
                    if (out == null) out = new File(apps, appDir);
                    if (!out.getName().equals(appDir)) continue;   // only the first bundle
                    File dst = new File(out, rel);
                    if (e.isDirectory()) { dst.mkdirs(); continue; }
                    dst.getParentFile().mkdirs();
                    try (OutputStream o = new FileOutputStream(dst)) { pipe(z, o); }
                }
            }
            if (out == null) throw new Exception("no .app bundle inside the archive");
            return out;
        }
        File dst = new File(apps, name);
        try (InputStream in = new FileInputStream(f); OutputStream o = new FileOutputStream(dst)) { pipe(in, o); }
        return dst;
    }

    private void copyAssets(String path, File dst) {
        AssetManager am = getAssets();
        try {
            String[] kids = am.list(path);
            if (kids == null || kids.length == 0) {
                if (dst.exists()) return;
                dst.getParentFile().mkdirs();
                try (InputStream in = am.open(path); OutputStream o = new FileOutputStream(dst)) { pipe(in, o); }
                return;
            }
            dst.mkdirs();
            for (String k : kids) copyAssets(path + "/" + k, new File(dst, k));
        } catch (Exception ignored) { }
    }

    private static void pipe(InputStream in, OutputStream out) throws java.io.IOException {
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    private static String readText(File f) {
        try (InputStream in = new FileInputStream(f)) {
            java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
            pipe(in, b);
            return b.toString(StandardCharsets.UTF_8.name());
        } catch (Exception e) { return ""; }
    }

    private void title(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(Color.WHITE);
        t.setTextSize(17);
        t.setPadding(0, 32, 0, 12);
        list.addView(t);
    }

    private void note(String s) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextColor(Color.rgb(180, 180, 200));
        t.setTextSize(13);
        list.addView(t);
    }
}
