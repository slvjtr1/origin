package app.stremioorigin;

import android.app.Activity;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.view.Surface;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.ColorInfo;
import androidx.media3.common.Format;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.Tracks;
import androidx.media3.common.VideoSize;
import androidx.media3.datasource.DefaultDataSource;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.audio.AudioCapabilities;
import androidx.media3.exoplayer.audio.AudioSink;
import androidx.media3.exoplayer.audio.DefaultAudioSink;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;

import java.util.ArrayList;
import java.util.Locale;

/* The native side's video (src/platform/android/video_android.c). Every call comes from SDL's thread and runs on the
   main looper; what the native side reads back is kept in volatile fields, updated from the player.
   - Sound: the TV's own decoders by default; with Settings › Audio › Dolby and DTS to a sound bar, Dolby Digital (Plus,
     Atmos), DTS and TrueHD go on as they are when the HDMI takes them (back to the decoders if the sound stalls).
   - The picture's frame rate: the display follows the film's (24, 25, 50...) when Android TV's "Match content frame rate"
     allows it, also with a short blank while the TV changes mode; for both players.
   - Subtitles are the app's own: ExoPlayer's text tracks are switched off. */
public final class OriginVideo {
    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static Activity act; private static SurfaceView view; private static Box box; private static ExoPlayer p;
    private static volatile int st, vw, vh, audCur = -1, unsup; private static volatile long pos, dur, buf;
    private static volatile String err = "", tracks = "", hdr = ""; private static volatile float fps;
    private static final ArrayList<Tracks.Group> audGroups = new ArrayList<>(); private static final ArrayList<Integer> audIdx = new ArrayList<>(); /* (main looper only) */

    /* the picture's place: any rectangle, also one larger than the screen (Fill and Zoom) */
    static final class Box extends FrameLayout {
        int x, y, w = -1, h = -1;
        Box(Context c) { super(c); setClipChildren(true); }
        @Override protected void onMeasure(int ws, int hs) {
            super.onMeasure(ws, hs);
            int cw = w < 0 ? getMeasuredWidth() : Math.max(1, w), ch = h < 0 ? getMeasuredHeight() : Math.max(1, h);
            for (int i = 0; i < getChildCount(); i++) getChildAt(i).measure(MeasureSpec.makeMeasureSpec(cw, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(ch, MeasureSpec.EXACTLY));
        }
        @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
            for (int i = 0; i < getChildCount(); i++) { View c = getChildAt(i); int cx = w < 0 ? 0 : x, cy = h < 0 ? 0 : y; c.layout(cx, cy, cx + c.getMeasuredWidth(), cy + c.getMeasuredHeight()); }
        }
    }

    /* v is placed by the activity; it goes inside a Box in its place */
    static void attach(Activity a, SurfaceView v) {
        act = a; View par = (View) v.getParent();
        if (par instanceof android.view.ViewGroup) {
            android.view.ViewGroup g = (android.view.ViewGroup) par; int at = g.indexOfChild(v); android.view.ViewGroup.LayoutParams lp = v.getLayoutParams();
            g.removeView(v); box = new Box(a); box.addView(v, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)); g.addView(box, at, lp);
        }
        view = v; v.setVisibility(View.INVISIBLE);
    }
    static void release() { ui.post(() -> { vlcOff(); if (p != null) { p.release(); p = null; } st = 0; }); }
    static void backgrounded() { if (vlc) OriginVlc.pause(); if (p != null && p.getPlayWhenReady()) p.setPlayWhenReady(false); } /* Home: the film waits (the app saves where it was) */

    private static void ensure() {
        if (p != null || act == null) return;
        DefaultHttpDataSource.Factory http = new DefaultHttpDataSource.Factory().setAllowCrossProtocolRedirects(true)
            .setConnectTimeoutMs(20000).setReadTimeoutMs(30000).setUserAgent("Origin/" + BuildConfig.VERSION_NAME + " (Android TV)");
        builtPass = wantPass; final boolean pass = builtPass; builtUp = lowRam() ? 0 : wantUp; builtSoft = wantSoft;
        /* Dolby and DTS: by default the TV's own decoders make them sound (many Google TV sets say they take them as they
           are and then play nothing from their speakers, the film's clock held with them); passed on as they are only
           when Settings › Audio says a sound bar or receiver is there */
        DefaultRenderersFactory rf = new DefaultRenderersFactory(act) {
            @Override protected AudioSink buildAudioSink(Context c, boolean flt, boolean params) {
                DefaultAudioSink.Builder b = new DefaultAudioSink.Builder(c).setEnableFloatOutput(flt).setEnableAudioTrackPlaybackParams(params);
                if (!pass) b.setAudioCapabilities(AudioCapabilities.DEFAULT_AUDIO_CAPABILITIES);
                return b.build();
            }
        }.setEnableDecoderFallback(true)
         /* The app's own sound decoders (FFmpeg, in the app): Dolby Digital, Dolby Digital Plus, DTS, TrueHD, and the rest,
            made into plain sound on any device. An older box with no Dolby or DTS decoder of its own plays them through
            these; a device that has its own keeps using them, unless they failed on this film (builtSoft: the app's first) */
         .setExtensionRendererMode(builtSoft ? DefaultRenderersFactory.EXTENSION_RENDERER_MODE_PREFER : DefaultRenderersFactory.EXTENSION_RENDERER_MODE_ON);
        /* a box with little memory keeps a shorter stretch of the film ahead and none behind: room for the picture */
        androidx.media3.exoplayer.DefaultLoadControl.Builder lc = new androidx.media3.exoplayer.DefaultLoadControl.Builder();
        if (lowRam()) lc.setBufferDurationsMs(12000, 24000, 1500, 3000).setBackBuffer(0, false);
        p = new ExoPlayer.Builder(act, rf).setMediaSourceFactory(new DefaultMediaSourceFactory(new DefaultDataSource.Factory(act, http)))
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(C.USAGE_MEDIA).setContentType(C.AUDIO_CONTENT_TYPE_MOVIE).build(), true)
            .setHandleAudioBecomingNoisy(true).setLoadControl(lc.build()).build();
        p.setVideoChangeFrameRateStrategy(C.VIDEO_CHANGE_FRAME_RATE_STRATEGY_OFF); /* (asked here, see rate(): ExoPlayer's own asks only for a seamless change, which most TVs never make) */
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true).build());
        /* Upscale: the frames through the graphics chip (OriginSharp). Asked before the film is prepared; a player made
           without it never has the extra step in its way */
        if (builtUp > 0) { try { p.setVideoEffects(java.util.Collections.singletonList(new OriginSharp(builtUp >= 3 ? 1f : builtUp == 2 ? 0.6f : 0.25f))); } catch (Throwable t) { builtUp = 0; } }
        p.setVideoSurfaceView(view);
        p.addListener(new Player.Listener() {
            @Override public void onPlaybackStateChanged(int s) { sync(); }
            @Override public void onIsPlayingChanged(boolean b) { sync(); }
            @Override public void onVideoSizeChanged(VideoSize s) { vw = s.width; vh = s.height; video(); }
            @Override public void onTracksChanged(Tracks t) { listTracks(t); video(); }
            @Override public void onPlayerError(PlaybackException e) {
                int c = e.errorCode;
                if (builtUp > 0) { plainHere(); return; } /* whatever went wrong with Upscale on: the same film again without it, before anything else is tried */
                /* the sound refused as it is: again at once, decoded by the TV */
                /* the device's own sound decoder failed on this film: the same film again at once, its sound decoded by the app */
                if (!builtPass && !builtSoft && audioFault(e)) { softHere(); return; }
                if (builtPass && (c == PlaybackException.ERROR_CODE_AUDIO_TRACK_INIT_FAILED || c == PlaybackException.ERROR_CODE_AUDIO_TRACK_WRITE_FAILED)) { decodeHere(); return; }
                /* a file or a picture or sound ExoPlayer can't read or decode here: VLC plays it, from the same point */
                if ((c >= 3001 && c <= 3004) || (c >= 4001 && c <= 4005) || (c >= 5001 && c <= 5003) || c == PlaybackException.ERROR_CODE_UNSPECIFIED) { toVlc(e.getErrorCodeName()); return; }
                err = e.getErrorCodeName(); st = 6; }
        });
        if (!ticking) { ticking = true; ui.post(OriginVideo::tick); } /* (one clock, also for a player made again) */
    }
    private static void sync() {
        if (p == null) { st = 0; return; }
        if (st == 6) return;
        switch (p.getPlaybackState()) {
            case Player.STATE_IDLE: st = 0; break;
            case Player.STATE_BUFFERING: st = vw > 0 ? 4 : 1; break;
            case Player.STATE_READY: st = p.getPlayWhenReady() ? 2 : 3; break;
            case Player.STATE_ENDED: st = 5; break;
        }
    }
    /* the position and the buffer, 4 times a second, as long as a player exists */
    private static void tick() {
        if (p == null) { ticking = false; return; }
        if (vlc) { OriginVlc.tick(); rate(OriginVlc.fps); ui.postDelayed(OriginVideo::tick, 250); return; }
        pos = p.getCurrentPosition(); long d = p.getDuration(); dur = d > 0 ? d : 0; buf = p.getBufferedPosition(); sync(); rate(fps); /* (again once the surface is there) */
        watch();
        ui.postDelayed(OriginVideo::tick, 250);
    }
    /* a film that plays but never shows: its clock still (the sound sent on as it is, and nothing taking it: decoded by
       the TV instead), or a picture this TV can't decode (said, so the app offers another stream, not a wait for ever) */
    private static long movedAt, lastPos = -1, readyAt;
    private static void watch() {
        long now = android.os.SystemClock.uptimeMillis();
        if (st != 2 || !p.getPlayWhenReady()) { movedAt = now; readyAt = 0; lastPos = pos; return; }
        if (readyAt == 0) readyAt = now;
        if (pos != lastPos) { lastPos = pos; movedAt = now; }
        else if (now - movedAt > 6000 && builtPass) { decodeHere(); return; }
        else if (now - movedAt > 8000 && !builtSoft && p.getAudioFormat() != null) { softHere(); return; } /* the film's clock stands still with the device's decoder: the app's own */
        if (vw == 0) {
            Format f = p.getVideoFormat();
            if (f != null && f.width > 0 && f.height > 0) { vw = f.width; vh = f.height; video(); }
            else if (now - readyAt > 4000 && !playableVideo()) { toVlc("VIDEO_FORMAT_UNSUPPORTED"); return; }
        }
        if (unsup == 1 && now - readyAt > 1500) toVlc("AUDIO_FORMAT_UNSUPPORTED"); /* no sound this TV decodes: VLC's own decoders */
    }
    private static boolean playableVideo() {
        boolean any = false;
        for (Tracks.Group g : p.getCurrentTracks().getGroups()) if (g.getType() == C.TRACK_TYPE_VIDEO) { any = true; for (int i = 0; i < g.length; i++) if (g.isTrackSupported(i)) return true; }
        return !any; /* (no picture at all: a sound-only stream plays as it is) */
    }
    /* the same film again where it was, its sound decoded by the TV */
    private static void decodeHere() {
        if (p == null || lastUrl == null) return;
        long at = p.getCurrentPosition(); boolean play = p.getPlayWhenReady(); wantPass = false;
        p.release(); p = null; ensure(); if (p == null) return;
        p.setMediaItem(item(lastUrl)); p.prepare(); if (at > 0) p.seekTo(at); p.setPlayWhenReady(play);
        st = 1; movedAt = android.os.SystemClock.uptimeMillis(); readyAt = 0;
    }
    /* the same film again where it was, without Upscale (an HDR or 4K picture, which it leaves alone; or an error with it on) */
    private static void plainHere() {
        if (p == null || lastUrl == null || builtUp == 0) return;
        long at = p.getCurrentPosition(); boolean play = p.getPlayWhenReady(); wantUp = 0;
        p.release(); p = null; ensure(); if (p == null) return;
        p.setMediaItem(item(lastUrl)); p.prepare(); if (at > 0) p.seekTo(at); p.setPlayWhenReady(play);
        st = 1; movedAt = android.os.SystemClock.uptimeMillis(); readyAt = 0;
    }
    private static volatile int wantUp; private static int builtUp;
    public static void upscale(int level) { wantUp = level < 0 ? 0 : level > 3 ? 3 : level; }
    /* the same film again where it was, its sound decoded by the app's own decoders */
    private static void softHere() {
        if (p == null || lastUrl == null) return;
        long at = p.getCurrentPosition(); boolean play = p.getPlayWhenReady(); wantSoft = true; wantPass = false;
        p.release(); p = null; ensure(); if (p == null) return;
        p.setMediaItem(item(lastUrl)); p.prepare(); if (at > 0) p.seekTo(at); p.setPlayWhenReady(play);
        st = 1; movedAt = android.os.SystemClock.uptimeMillis(); readyAt = 0;
    }
    private static boolean wantSoft, builtSoft;
    private static boolean audioFault(PlaybackException e) {
        int c = e.errorCode;
        if (c == PlaybackException.ERROR_CODE_AUDIO_TRACK_INIT_FAILED || c == PlaybackException.ERROR_CODE_AUDIO_TRACK_WRITE_FAILED) return true;
        if (e instanceof androidx.media3.exoplayer.ExoPlaybackException) { androidx.media3.exoplayer.ExoPlaybackException x = (androidx.media3.exoplayer.ExoPlaybackException) e;
            if (x.type == androidx.media3.exoplayer.ExoPlaybackException.TYPE_RENDERER && x.rendererFormat != null && MimeTypes.isAudio(x.rendererFormat.sampleMimeType)) return true; }
        return false;
    }
    private static int lowRamIs = -1;
    private static boolean lowRam() {
        if (lowRamIs < 0) { try { android.app.ActivityManager am = (android.app.ActivityManager) act.getSystemService(Context.ACTIVITY_SERVICE); android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo(); am.getMemoryInfo(mi);
            lowRamIs = am.isLowRamDevice() || mi.totalMem < 1600L * 1024 * 1024 ? 1 : 0; } catch (Throwable t) { lowRamIs = 0; } }
        return lowRamIs == 1;
    }
    private static MediaItem item(String url) {
        MediaItem.Builder mi = new MediaItem.Builder().setUri(url); String u = url.toLowerCase(Locale.ROOT);
        if (u.contains(".m3u8")) mi.setMimeType(MimeTypes.APPLICATION_M3U8); else if (u.contains(".mpd")) mi.setMimeType(MimeTypes.APPLICATION_MPD);
        return mi.build();
    }
    private static volatile boolean wantPass, vlc; private static boolean builtPass, ticking, vlcTried; private static String lastUrl;
    /* the film handed to VLC where it was (once per film; if VLC can't either, the error is said) */
    private static void toVlc(String why) {
        if (lastUrl == null || vlcTried || act == null || view == null) { err = why; st = 6; return; }
        vlcTried = true; long at = p != null ? Math.max(0, p.getCurrentPosition()) : 0; boolean play = p == null || p.getPlayWhenReady();
        String lang = ""; int ci = audCur; if (ci >= 0) for (String l : tracks.split("\n")) { String[] f = l.split("\t", -1); if (f.length > 1 && f[0].equals(String.valueOf(ci))) { lang = f[1]; break; } }
        if (p != null) { p.stop(); p.clearVideoSurfaceView(view); }
        android.util.Log.i("Origin", "ExoPlayer: " + why + ", the film goes on in VLC");
        vlc = OriginVlc.start(act, view, lastUrl, at, play, lang);
        if (!vlc) { err = why; st = 6; if (p != null) p.setVideoSurfaceView(view); }
    }
    private static void vlcOff() { boolean was = OriginVlc.on(); vlc = false; if (was) { OriginVlc.stop(); if (p != null) p.setVideoSurfaceView(view); } }
    public static void passthrough(boolean on) { wantPass = on; }
    private static String codec(String m) {
        if (m == null) return "";
        switch (m) {
            case MimeTypes.AUDIO_AC3: return "ac3";
            case MimeTypes.AUDIO_E_AC3: case MimeTypes.AUDIO_E_AC3_JOC: return "eac3";
            case MimeTypes.AUDIO_AC4: return "ac4";
            case MimeTypes.AUDIO_DTS: case MimeTypes.AUDIO_DTS_EXPRESS: return "dts";
            case MimeTypes.AUDIO_DTS_HD: return "dts-hd";
            case MimeTypes.AUDIO_DTS_X: return "dts-x";
            case MimeTypes.AUDIO_TRUEHD: return "truehd";
            case MimeTypes.AUDIO_AAC: return "aac";
            case MimeTypes.AUDIO_MPEG: return "mp3";
            case MimeTypes.AUDIO_OPUS: return "opus";
            case MimeTypes.AUDIO_VORBIS: return "vorbis";
            case MimeTypes.AUDIO_FLAC: return "flac";
            default: return m.startsWith("audio/") ? m.substring(6) : m;
        }
    }
    private static String lang3(String l) {
        if (l == null || l.isEmpty() || "und".equals(l)) return "";
        try { String s = Locale.forLanguageTag(l).getISO3Language(); return s.isEmpty() ? l : s; } catch (Exception e) { return l; }
    }
    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' '); }
    /* the audio tracks as lines for the native side: "idx\tlang\tname\tcodec\tchannels\tforced" */
    private static void listTracks(Tracks t) {
        audGroups.clear(); audIdx.clear(); StringBuilder sb = new StringBuilder(); int cur = -1, n = 0, playable = 0, any = 0;
        for (Tracks.Group g : t.getGroups()) {
            if (g.getType() != C.TRACK_TYPE_AUDIO) continue;
            for (int i = 0; i < g.length; i++) {
                Format f = g.getTrackFormat(i); any++; if (g.isTrackSupported(i)) playable++;
                if (g.isTrackSelected(i)) cur = n;
                sb.append(n).append('\t').append(lang3(f.language)).append('\t').append(clean(f.label)).append('\t').append(codec(f.sampleMimeType)).append('\t')
                  .append(f.channelCount > 0 ? f.channelCount : 0).append('\t').append((f.selectionFlags & C.SELECTION_FLAG_FORCED) != 0 ? 1 : 0).append('\n');
                audGroups.add(g); audIdx.add(i); n++;
            }
        }
        tracks = sb.toString(); audCur = cur; unsup = any > 0 && (playable == 0 || cur < 0) ? 1 : 0;
    }
    /* the frame rate and the HDR kind of the picture playing */
    private static void video() {
        if (p == null) return; Format f = p.getVideoFormat(); if (f == null) return;
        fps = f.frameRate > 0 ? f.frameRate : 0; rate(fps);
        String h = "none"; ColorInfo c = f.colorInfo;
        if (MimeTypes.VIDEO_DOLBY_VISION.equals(f.sampleMimeType) || (f.codecs != null && (f.codecs.startsWith("dvh") || f.codecs.startsWith("dva")))) h = "DolbyVision";
        else if (c != null && c.colorTransfer == C.COLOR_TRANSFER_ST2084) h = "HDR10";
        else if (c != null && c.colorTransfer == C.COLOR_TRANSFER_HLG) h = "HLG";
        hdr = h;
        if (builtUp > 0 && (!"none".equals(h) || f.height > 1088 || f.width > 1920)) ui.post(OriginVideo::plainHere); /* HDR, Dolby Vision and 4K stay exactly as they are */
    }
    /* the display's refresh rate asked for the film's own (0: the TV's usual one again). Android 12 and later: also a
       change that isn't seamless, as the TV's "Match content frame rate" setting allows; Android 11: a seamless one */
    private static float rateSet;
    private static void rate(float f) {
        if (Build.VERSION.SDK_INT < 30 || view == null || f == rateSet || (f > 0 && (f < 10 || f > 121))) return;
        Surface s = view.getHolder().getSurface(); if (s == null || !s.isValid()) return;
        try {
            if (Build.VERSION.SDK_INT >= 31) s.setFrameRate(f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE, f > 0 ? Surface.CHANGE_FRAME_RATE_ALWAYS : Surface.CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS);
            else s.setFrameRate(f, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
            rateSet = f;
        } catch (Throwable t) { rateSet = f; }
    }

    /* load and unload come from SDL's thread and run later on the main looper: until then the native side reads a new film
       opening (or none), never the last film's end or error (gate; seq: the latest call, so an older one doesn't open it) */
    private static volatile int gate, seq;
    public static void load(String url) {
        final boolean wp = wantPass; final int my = ++seq, up = wantUp;
        vlc = false; err = ""; st = 1; vw = vh = 0; pos = dur = buf = 0; fps = 0; hdr = ""; tracks = ""; audCur = -1; unsup = 0; gate = 1;
        ui.post(() -> {
            vlcOff(); vlcTried = false;
            if (p != null && (builtPass != wp || builtUp != (lowRam() ? 0 : up) || builtSoft)) { p.release(); p = null; } /* (a setting changed, or the last film needed the app's decoders: a player made afresh) */
            wantPass = wp; wantUp = up; wantSoft = false; ensure();
            if (p == null) { err = "PLAYER_INIT_FAILED"; st = 6; if (seq == my) gate = 0; return; }
            err = ""; st = 1; vw = vh = 0; fps = 0; hdr = ""; tracks = ""; audCur = -1; unsup = 0; view.setVisibility(View.VISIBLE);
            lastUrl = url; movedAt = android.os.SystemClock.uptimeMillis(); readyAt = 0; lastPos = -1;
            p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().clearOverridesOfType(C.TRACK_TYPE_AUDIO).build());
            p.setVideoSurfaceView(view); p.setMediaItem(item(url)); p.prepare(); p.setPlayWhenReady(true);
            if (seq == my) gate = 0;
        });
    }
    public static void unload() {
        ++seq; vlc = false; st = 0; vw = vh = 0; pos = dur = buf = 0; tracks = ""; audCur = -1; unsup = 0; gate = 2;
        ui.post(() -> { vlcOff(); if (p != null) { p.stop(); p.clearMediaItems(); } st = 0; vw = vh = 0; pos = dur = buf = 0; fps = 0; tracks = ""; audCur = -1; unsup = 0; lastUrl = null; rate(0); if (view != null) view.setVisibility(View.INVISIBLE); });
    }
    public static void play() { ui.post(() -> { if (vlc) OriginVlc.play(); else if (p != null) p.setPlayWhenReady(true); }); }
    public static void pause() { ui.post(() -> { if (vlc) OriginVlc.pause(); else if (p != null) p.setPlayWhenReady(false); }); }
    public static void seek(long ms) { if (vlc) OriginVlc.pos = ms; else pos = ms; ui.post(() -> { if (vlc) OriginVlc.seek(ms); else if (p != null) p.seekTo(ms); }); }
    public static void volume(float v) { ui.post(() -> { if (vlc) OriginVlc.volume(v); else if (p != null) p.setVolume(v); }); }
    public static void selectAudio(int idx) { if (vlc) { ui.post(() -> OriginVlc.selectAudio(idx)); return; } audCur = idx; ui.post(() -> { if (p == null || idx < 0 || idx >= audGroups.size()) return; Tracks.Group g = audGroups.get(idx);
        p.setTrackSelectionParameters(p.getTrackSelectionParameters().buildUpon().setOverrideForType(new TrackSelectionOverride(g.getMediaTrackGroup(), audIdx.get(idx))).build()); }); }
    /* fractions of the screen: the picture's rectangle (larger than the screen for Fill and Zoom) */
    public static void window(float x, float y, float w, float h) {
        ui.post(() -> { if (box == null) return; View par = (View) box.getParent(); if (par == null) return;
            int W = par.getWidth(), H = par.getHeight(); if (W <= 0 || H <= 0) return;
            box.x = Math.round(x * W); box.y = Math.round(y * H); box.w = Math.max(1, Math.round(w * W)); box.h = Math.max(1, Math.round(h * H)); box.requestLayout(); });
    }
    /* what the native side reads: the player playing the film (ExoPlayer, or VLC once it took over) */
    public static long position() { return vlc ? OriginVlc.pos : pos; }
    public static long duration() { return vlc ? OriginVlc.dur : dur; }
    public static long buffered() { return vlc ? OriginVlc.pos : buf; }
    public static int state() { int g = gate; return g == 1 ? 1 : g == 2 ? 0 : vlc ? OriginVlc.st : st; }
    public static int width() { return vlc ? OriginVlc.vw : vw; }
    public static int height() { return vlc ? OriginVlc.vh : vh; }
    public static String error() { return gate != 0 ? "" : vlc ? OriginVlc.err : err; }
    public static String audioTracks() { return vlc ? OriginVlc.tracks : tracks; }
    public static int audioCurrent() { return vlc ? OriginVlc.audCur : audCur; }
    public static int audioUnsupported() { return vlc ? 0 : unsup; }
    public static float fps() { return vlc ? OriginVlc.fps : fps; }
    public static String hdr() { return vlc ? "" : hdr; }
    private OriginVideo() {}
}
