package app.stremioorigin;

import android.content.Context;
import android.net.Uri;
import android.view.SurfaceView;
import android.view.View;

import org.videolan.libvlc.LibVLC;
import org.videolan.libvlc.Media;
import org.videolan.libvlc.MediaPlayer;
import org.videolan.libvlc.interfaces.IMedia;
import org.videolan.libvlc.interfaces.IVLCVout;

import java.util.ArrayList;
import java.util.Locale;

/* The second player: LibVLC, for what ExoPlayer can't play on this TV (a picture its decoders refuse, an old XviD or
   AVI, 10-bit H.264, a sound the TV has no decoder for: DTS, TrueHD...). OriginVideo hands a film over to it from the
   point it reached; it plays on the same SurfaceView, the TV's hardware decoders first, else FFmpeg in software, and
   the sound always decoded here (any speakers). Everything runs on the main looper (OriginVideo's); the native side
   reads the volatile fields, as ExoPlayer's. Subtitles stay the app's own (VLC's are off). */
final class OriginVlc {
    private static LibVLC lib; private static MediaPlayer mp; private static SurfaceView view; private static View.OnLayoutChangeListener sizer;
    static volatile int st, vw, vh, audCur = -1; static volatile long pos, dur; static volatile float fps; static volatile String err = "", tracks = "";
    private static final ArrayList<Integer> audIds = new ArrayList<>(); /* the app's audio index -> VLC's track id */
    private static String wantLang = ""; private static boolean langDone; private static float bufPct = 100;

    static boolean on() { return mp != null; }

    /* the film from at ms, playing or paused; lang: the audio language the app had chosen ("" none) */
    static boolean start(Context c, SurfaceView v, String url, long at, boolean play, String lang) {
        stop();
        try {
            ArrayList<String> o = new ArrayList<>();
            o.add("--network-caching=4000"); o.add("--file-caching=2000"); o.add("--http-reconnect");
            o.add("--no-spu"); o.add("--no-sub-autodetect-file");        /* (the app draws the subtitles) */
            o.add("--audio-time-stretch"); o.add("--avcodec-skiploopfilter=1"); /* (software H.264/HEVC: a little lighter, invisible) */
            o.add("--avcodec-threads=0");
            lib = new LibVLC(c, o); mp = new MediaPlayer(lib);
        } catch (Throwable t) { err = "VLC_INIT_FAILED"; st = 6; stop(); return false; }
        view = v; wantLang = lang == null ? "" : lang; langDone = wantLang.isEmpty(); err = ""; st = 1; vw = vh = 0; fps = 0; pos = at; dur = 0; tracks = ""; audCur = -1; audIds.clear(); bufPct = 100;
        IVLCVout vo = mp.getVLCVout(); vo.setVideoView(v);
        vo.attachViews((out, w, h, visW, visH, sarN, sarD) -> { int ww = visW > 0 ? visW : w, hh = visH > 0 ? visH : h; if (sarN > 0 && sarD > 0 && sarN != sarD) ww = ww * sarN / sarD; vw = ww; vh = hh; });
        if (v.getWidth() > 0 && v.getHeight() > 0) vo.setWindowSize(v.getWidth(), v.getHeight());
        sizer = (x, l, t, r, b, ol, ot, or, ob) -> { if (mp != null && r - l > 0 && b - t > 0) mp.getVLCVout().setWindowSize(r - l, b - t); }; /* Fill and Zoom resize the view */
        v.addOnLayoutChangeListener(sizer);
        mp.setEventListener(e -> {
            switch (e.type) {
                case MediaPlayer.Event.Buffering: bufPct = e.getBuffering(); break;
                case MediaPlayer.Event.EncounteredError: err = "VLC_PLAYBACK_FAILED"; st = 6; break;
                case MediaPlayer.Event.ESAdded: case MediaPlayer.Event.ESSelected: listTracks(); break;
                default: break;
            }
        });
        Media m = new Media(lib, Uri.parse(url)); m.setHWDecoderEnabled(true, false);
        if (at > 1000) m.addOption(":start-time=" + (at / 1000.0));
        if (!play) m.addOption(":start-paused");
        mp.setMedia(m); m.release(); mp.play();
        return true;
    }
    static void stop() {
        if (mp != null) {
            try { mp.setEventListener(null); mp.stop(); IVLCVout vo = mp.getVLCVout(); if (vo.areViewsAttached()) vo.detachViews(); } catch (Throwable t) { }
            try { mp.release(); } catch (Throwable t) { } mp = null;
        }
        if (view != null && sizer != null) view.removeOnLayoutChangeListener(sizer); sizer = null; view = null;
        if (lib != null) { try { lib.release(); } catch (Throwable t) { } lib = null; }
        st = 0; vw = vh = 0; fps = 0; pos = dur = 0; tracks = ""; audCur = -1; audIds.clear();
    }
    /* 4 times a second, from OriginVideo's clock */
    static void tick() {
        if (mp == null || st == 6) return;
        long t = mp.getTime(), d = mp.getLength(); if (t >= 0) pos = t; dur = d > 0 ? d : 0;
        switch (mp.getPlayerState()) {
            case IMedia.State.Opening: st = 1; break;
            case IMedia.State.Playing: st = vw > 0 && bufPct < 100 ? 4 : vw > 0 || mp.isPlaying() ? 2 : 1; break;
            case IMedia.State.Paused: st = 3; break;
            case IMedia.State.Ended: st = 5; break;
            case IMedia.State.Error: err = "VLC_PLAYBACK_FAILED"; st = 6; break;
            default: break;
        }
    }
    private static String codec(String c) {
        if (c == null) return ""; String s = c.trim().toLowerCase(Locale.ROOT);
        switch (s) {
            case "a52": case "ac-3": case "ac3": return "ac3";
            case "eac3": case "e-ac3": return "eac3";
            case "dts": case "dtsc": case "dtsh": case "dtsl": return "dts";
            case "mlp": case "trhd": case "truehd": return "truehd";
            case "mp4a": case "aac": return "aac";
            case "mpga": case "mp3": case "mp2": return "mp3";
            case "opus": return "opus"; case "vorb": case "vorbis": return "vorbis"; case "flac": return "flac";
            default: return s;
        }
    }
    private static String lang3(String l) {
        if (l == null || l.isEmpty() || "und".equals(l)) return "";
        try { String s = Locale.forLanguageTag(l).getISO3Language(); return s.isEmpty() ? l : s; } catch (Exception e) { return l; }
    }
    private static String clean(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' '); }
    /* the audio tracks, as OriginVideo's lines: "idx\tlang\tname\tcodec\tchannels\tforced" */
    private static void listTracks() {
        if (mp == null) return;
        MediaPlayer.TrackDescription[] td = mp.getAudioTracks(); IMedia m = mp.getMedia();
        StringBuilder sb = new StringBuilder(); audIds.clear(); int cur = mp.getAudioTrack(), sel = -1, n = 0, pick = -1;
        if (td != null) for (MediaPlayer.TrackDescription d : td) {
            if (d.id < 0) continue; /* ("Disable") */
            String lang = "", cod = ""; int ch = 0;
            if (m != null) for (int i = 0; i < m.getTrackCount(); i++) { IMedia.Track t = m.getTrack(i); if (t != null && t.type == IMedia.Track.Type.Audio && t.id == d.id) { lang = lang3(t.language); cod = codec(t.codec); ch = t instanceof IMedia.AudioTrack ? ((IMedia.AudioTrack) t).channels : 0; break; } }
            if (d.id == cur) sel = n;
            if (pick < 0 && !langDone && !lang.isEmpty() && lang.equals(wantLang)) pick = d.id;
            sb.append(n).append('\t').append(lang).append('\t').append(clean(d.name)).append('\t').append(cod).append('\t').append(ch).append('\t').append(0).append('\n');
            audIds.add(d.id); n++;
        }
        if (m != null) { for (int i = 0; i < m.getTrackCount(); i++) { IMedia.Track t = m.getTrack(i); /* the picture's frame rate (the display follows it) */
                if (t instanceof IMedia.VideoTrack) { IMedia.VideoTrack v = (IMedia.VideoTrack) t; if (v.frameRateDen > 0 && v.frameRateNum > 0) { fps = (float) v.frameRateNum / v.frameRateDen; break; } } }
            m.release(); }
        tracks = sb.toString(); audCur = sel;
        if (pick >= 0) { langDone = true; if (pick != cur) mp.setAudioTrack(pick); } /* the language the app had chosen, kept */
    }
    static void play() { if (mp != null && !mp.isPlaying()) mp.play(); }
    static void pause() { if (mp != null && mp.isPlaying()) mp.pause(); }
    static void seek(long ms) { if (mp != null) { mp.setTime(ms, false); pos = ms; } }
    static void volume(float v) { if (mp != null) mp.setVolume(Math.max(0, Math.min(100, Math.round(v * 100)))); }
    static void selectAudio(int idx) { if (mp != null && idx >= 0 && idx < audIds.size()) { mp.setAudioTrack(audIds.get(idx)); audCur = idx; langDone = true; } }
    private OriginVlc() {}
}
