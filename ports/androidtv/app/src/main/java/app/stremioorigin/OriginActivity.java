package app.stremioorigin;

import android.content.Intent;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.RelativeLayout;

import org.libsdl.app.SDLActivity;

/* SDL's activity for Android TV and Google TV: full screen, landscape, the video's SurfaceView under SDL's GL surface
   (made see-through so the picture shows where the app draws nothing). The remote's keys are read here, before SDL,
   and handed to the app as its own key codes (src/core/keys.h): a Google TV remote's D-pad, OK and Back do
   everything; the keys bigger remotes have go straight to what they mean. */
public class OriginActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "SDL2", "origin" }; }

    /* one key, already the app's code, into the app's event queue (src/platform/android/plat_android.c) */
    static native void nativeKey(int k, int down, int repeat);
    /* words for the page on the screen (what was said to voice search) */
    static native void nativeText(String s);
    static final int VOICE = 41;
    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != VOICE || res != RESULT_OK || data == null) return;
        java.util.ArrayList<String> r = data.getStringArrayListExtra(android.speech.RecognizerIntent.EXTRA_RESULTS);
        if (r != null && !r.isEmpty() && r.get(0) != null && !r.get(0).trim().isEmpty()) { try { nativeText(r.get(0).trim()); } catch (UnsatisfiedLinkError e) { } }
    }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        immersive();
        if (mSurface != null) {
            mSurface.getHolder().setFormat(PixelFormat.TRANSLUCENT);
            mSurface.setZOrderMediaOverlay(true);
            /* the app is drawn for 1920x1080: on a TV whose screen Android gives as 4K, it is drawn at 1080p and the TV's
               scaler makes it full size (four times fewer pixels for the GPU each frame; the film itself stays 4K) */
            try {
                Point pt = new Point(); getDisplaySize(pt);
                if (pt.x > 1920 && pt.y > 0) mSurface.getHolder().setFixedSize(1920, Math.round(1920f * pt.y / pt.x));
            } catch (Throwable t) { }
        }
        SurfaceView v = new SurfaceView(this);
        if (mLayout != null) mLayout.addView(v, 0, new RelativeLayout.LayoutParams(RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
        OriginVideo.attach(this, v);
        OriginBridge.attach(this);
    }
    @SuppressWarnings("deprecation")
    private void getDisplaySize(Point pt) { getWindowManager().getDefaultDisplay().getRealSize(pt); }
    @SuppressWarnings("deprecation")
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); }
    @Override protected void onPause() { OriginVideo.backgrounded(); super.onPause(); }
    @Override protected void onDestroy() { OriginVideo.release(); super.onDestroy(); }

    /* the app's key codes (src/core/keys.h, in its order) */
    static final int K_LEFT = 1, K_RIGHT = 2, K_UP = 3, K_DOWN = 4, K_OK = 5, K_BACK = 6, K_PLAY = 7, K_PAUSE = 8, K_PLAYPAUSE = 9,
        K_STOP = 10, K_REW = 11, K_FF = 12, K_RED = 13, K_GREEN = 14, K_YELLOW = 15, K_BLUE = 16, K_CH_UP = 17, K_CH_DOWN = 18,
        K_SUBS = 23, K_AUDIO = 24, K_INFO = 25;
    /* Android TV's keys, each to what it means in the app */
    static int map(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_LEFT: return K_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return K_RIGHT;
            case KeyEvent.KEYCODE_DPAD_UP: return K_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return K_DOWN;
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: case KeyEvent.KEYCODE_BUTTON_A: return K_OK;
            case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_ESCAPE: return K_BACK;
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE: case KeyEvent.KEYCODE_HEADSETHOOK: case KeyEvent.KEYCODE_BUTTON_START: return K_PLAYPAUSE;
            case KeyEvent.KEYCODE_MEDIA_PLAY: return K_PLAY;
            case KeyEvent.KEYCODE_MEDIA_PAUSE: return K_PAUSE;
            case KeyEvent.KEYCODE_MEDIA_STOP: return K_STOP;
            case KeyEvent.KEYCODE_MEDIA_REWIND: case KeyEvent.KEYCODE_MEDIA_PREVIOUS: case KeyEvent.KEYCODE_MEDIA_SKIP_BACKWARD: case KeyEvent.KEYCODE_MEDIA_STEP_BACKWARD: case KeyEvent.KEYCODE_BUTTON_L1: return K_REW;
            case KeyEvent.KEYCODE_MEDIA_FAST_FORWARD: case KeyEvent.KEYCODE_MEDIA_NEXT: case KeyEvent.KEYCODE_MEDIA_SKIP_FORWARD: case KeyEvent.KEYCODE_MEDIA_STEP_FORWARD: case KeyEvent.KEYCODE_BUTTON_R1: return K_FF;
            case KeyEvent.KEYCODE_PROG_RED: case KeyEvent.KEYCODE_MENU: case KeyEvent.KEYCODE_SETTINGS: return K_RED;     /* Settings */
            case KeyEvent.KEYCODE_PROG_GREEN: case KeyEvent.KEYCODE_SEARCH: return K_GREEN;                              /* Search */
            case KeyEvent.KEYCODE_PROG_YELLOW: return K_YELLOW;
            case KeyEvent.KEYCODE_PROG_BLUE: return K_BLUE;
            case KeyEvent.KEYCODE_CHANNEL_UP: case KeyEvent.KEYCODE_PAGE_UP: return K_CH_UP;
            case KeyEvent.KEYCODE_CHANNEL_DOWN: case KeyEvent.KEYCODE_PAGE_DOWN: return K_CH_DOWN;
            case KeyEvent.KEYCODE_CAPTIONS: return K_SUBS;
            case KeyEvent.KEYCODE_MEDIA_AUDIO_TRACK: return K_AUDIO;
            case KeyEvent.KEYCODE_INFO: case KeyEvent.KEYCODE_GUIDE: return K_INFO;
            default: return 0;
        }
    }
    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        int k = map(e.getKeyCode());
        if (k == 0 || mBrokenLibraries) return super.dispatchKeyEvent(e); /* volume, Home, the Assistant... stay Android's */
        int a = e.getAction();
        if (a != KeyEvent.ACTION_DOWN && a != KeyEvent.ACTION_UP) return true;
        try { nativeKey(k, a == KeyEvent.ACTION_DOWN ? 1 : 0, e.getRepeatCount() > 0 ? 1 : 0); } catch (UnsatisfiedLinkError x) { return super.dispatchKeyEvent(e); }
        return true;
    }
}
