package app.stremioorigin;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Display;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;

/* What the native app asks of Android TV (src/platform/android/android.h): installing an update, a page in the TV's
   browser, closing. Called from any thread. */
public final class OriginBridge {
    private static final Handler ui = new Handler(Looper.getMainLooper());
    static Activity act;
    private static volatile String upd = "";

    static void attach(Activity a) { act = a; }

    public static void cmd(String name, String a, String b, int n) {
        Activity x = act; if (x == null) return;
        switch (name) {
            case "open": ui.post(() -> { try { Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(a)); i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); x.startActivity(i); }
                catch (Exception e) { Toast.makeText(x, "No browser on this TV", Toast.LENGTH_SHORT).show(); } }); break;
            case "install": install(x, a, b); break;
            /* Search by voice: Android's speech page (Google's on Google TV), in the app's language; what was said comes back
               through OriginActivity.onActivityResult. b: the words for a TV without one */
            case "voice": ui.post(() -> { try { Intent i = new Intent(android.speech.RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
                    i.putExtra(android.speech.RecognizerIntent.EXTRA_LANGUAGE_MODEL, android.speech.RecognizerIntent.LANGUAGE_MODEL_WEB_SEARCH);
                    i.putExtra(android.speech.RecognizerIntent.EXTRA_LANGUAGE, "ar".equals(a) ? "ar-SA" : "en-US");
                    i.putExtra(android.speech.RecognizerIntent.EXTRA_MAX_RESULTS, 1);
                    x.startActivityForResult(i, OriginActivity.VOICE); }
                catch (Exception e) { Toast.makeText(x, b != null ? b : "Voice search isn't available on this TV", Toast.LENGTH_SHORT).show(); } }); break;
            /* the app's main loop ended (Home's "press again to exit"): the activity goes, and the process with it, so the
               next start is a clean one */
            case "exit": ui.post(() -> { x.finishAndRemoveTask(); ui.postDelayed(() -> System.exit(0), 400); }); break;
        }
    }
    public static String updateStatus() { return upd; }
    /* the panel's lines: 2160 on a 4K TV, whose Android draws its pages at 1080 */
    public static String panelLines() {
        Activity x = act; if (x == null) return "0";
        try { Display d = x.getWindowManager().getDefaultDisplay(); int best = 0;
            for (Display.Mode m : d.getSupportedModes()) best = Math.max(best, Math.min(m.getPhysicalWidth(), m.getPhysicalHeight()));
            return Integer.toString(best); }
        catch (Exception e) { return "0"; }
    }

    /* an update: the APK from the release into the app's cache, its SHA-256 checked, then Android's own installer */
    private static void install(Activity x, String url, String sha) {
        if (url == null) return;
        if (Build.VERSION.SDK_INT >= 26 && !x.getPackageManager().canRequestPackageInstalls()) { /* Android asks once: this app may install */
            upd = "Allow installs from Origin, then press Install again";
            ui.post(() -> { try { Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + x.getPackageName())); i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); x.startActivity(i); }
                catch (Exception e) { try { Intent i = new Intent(Settings.ACTION_SECURITY_SETTINGS); i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); x.startActivity(i); } catch (Exception e2) { } } });
            return;
        }
        upd = "Downloading the update…";
        new Thread(() -> {
            try {
                File f = new File(x.getCacheDir(), "updates"); f.mkdirs(); File apk = new File(f, "update.apk");
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection(); c.setInstanceFollowRedirects(true); c.setConnectTimeout(20000); c.setReadTimeout(30000);
                long total = c.getContentLengthLong(), got = 0; MessageDigest md = MessageDigest.getInstance("SHA-256");
                try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(apk)) {
                    byte[] buf = new byte[65536]; int r; long shown = 0;
                    while ((r = in.read(buf)) > 0) { out.write(buf, 0, r); md.update(buf, 0, r); got += r;
                        if (total > 0 && got - shown > total / 50) { shown = got; upd = "Downloading the update… " + (got * 100 / total) + "%"; } }
                }
                StringBuilder h = new StringBuilder(); for (byte bt : md.digest()) h.append(String.format("%02x", bt));
                if (sha == null || sha.isEmpty() || !h.toString().equalsIgnoreCase(sha)) { upd = "The download was damaged. Try again"; apk.delete(); return; }
                upd = "Opening the installer…";
                Uri u = FileProvider.getUriForFile(x, x.getPackageName() + ".files", apk);
                Intent i = new Intent(Intent.ACTION_VIEW); i.setDataAndType(u, "application/vnd.android.package-archive");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
                ui.post(() -> { try { x.startActivity(i); upd = "Confirm the update in the installer"; } catch (Exception e) { upd = "The installer could not open"; } });
            } catch (Exception e) { upd = "The update failed. Try again"; }
        }, "update").start();
    }
    private OriginBridge() {}
}
