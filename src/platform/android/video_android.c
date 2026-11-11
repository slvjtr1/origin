/* Android TV: the picture is Media3 ExoPlayer's (OriginVideo.java), on a SurfaceView under SDL's GL surface,
   which is drawn with alpha where the video shows through. Calls go to Java through JNI; Java runs them on the main
   looper and keeps the state in fields read back here every frame. Audio tracks are ExoPlayer's (chosen by the app,
   as on webOS); subtitles inside Matroska files are read by the app itself (mkvsubs), so the platform lists none.
   Fill and Zoom: the SurfaceView is made larger than the screen, the screen shows the part asked for. */
#include "core/video.h"
#include "ui/gfx.h"
#include "core/store.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jclass cls; static char err[256];
static jmethodID mUp, mPass, mLoad, mUnload, mPlay, mPause, mSeek, mVolume, mWindow, mPos, mDur, mBuf, mState, mW, mH, mErr, mTracks, mAudCur, mSelAud, mFps, mHdr, mUnsup;

static JNIEnv *env(void) { return (JNIEnv *)SDL_AndroidGetJNIEnv(); }
static int ex(JNIEnv *e) { if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionDescribe(e); (*e)->ExceptionClear(e); return 1; } return 0; }

int video_init(const char *appId) {
  (void)appId; JNIEnv *e = env(); if (!e) return 0;
  jobject act = (jobject)SDL_AndroidGetActivity(); if (!act) return 0;
  jclass ac = (*e)->GetObjectClass(e, act); jmethodID gl = (*e)->GetMethodID(e, ac, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject ld = (*e)->CallObjectMethod(e, act, gl);
  jclass lc = (*e)->FindClass(e, "java/lang/ClassLoader"); jmethodID lm = (*e)->GetMethodID(e, lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring nm = (*e)->NewStringUTF(e, "app.stremioorigin.OriginVideo");
  jclass c = (jclass)(*e)->CallObjectMethod(e, ld, lm, nm);
  (*e)->DeleteLocalRef(e, nm); (*e)->DeleteLocalRef(e, act); (*e)->DeleteLocalRef(e, ac); (*e)->DeleteLocalRef(e, ld); (*e)->DeleteLocalRef(e, lc);
  if (ex(e) || !c) { fprintf(stderr, "[video] OriginVideo not found\n"); return 0; }
  cls = (jclass)(*e)->NewGlobalRef(e, c); (*e)->DeleteLocalRef(e, c);
#define M(v, n, s) v = (*e)->GetStaticMethodID(e, cls, n, s); if (ex(e) || !v) { fprintf(stderr, "[video] no OriginVideo.%s\n", n); return 0; }
  M(mLoad, "load", "(Ljava/lang/String;)V"); M(mUnload, "unload", "()V"); M(mPlay, "play", "()V"); M(mPause, "pause", "()V");
  M(mSeek, "seek", "(J)V"); M(mVolume, "volume", "(F)V"); M(mWindow, "window", "(FFFF)V");
  M(mPos, "position", "()J"); M(mDur, "duration", "()J"); M(mBuf, "buffered", "()J"); M(mState, "state", "()I");
  M(mW, "width", "()I"); M(mH, "height", "()I"); M(mErr, "error", "()Ljava/lang/String;");
  M(mTracks, "audioTracks", "()Ljava/lang/String;"); M(mAudCur, "audioCurrent", "()I"); M(mSelAud, "selectAudio", "(I)V");
  M(mFps, "fps", "()F"); M(mHdr, "hdr", "()Ljava/lang/String;"); M(mUnsup, "audioUnsupported", "()I");
  M(mPass, "passthrough", "(Z)V"); M(mUp, "upscale", "(I)V");
#undef M
  return 1;
}
static void callv(jmethodID m) { JNIEnv *e = env(); if (cls && m) { (*e)->CallStaticVoidMethod(e, cls, m); ex(e); } }
static jlong callj(jmethodID m) { JNIEnv *e = env(); if (!cls || !m) return 0; jlong r = (*e)->CallStaticLongMethod(e, cls, m); return ex(e) ? 0 : r; }
static jint calli(jmethodID m) { JNIEnv *e = env(); if (!cls || !m) return 0; jint r = (*e)->CallStaticIntMethod(e, cls, m); return ex(e) ? 0 : r; }
static int calls(jmethodID m, char *out, size_t cap) { /* a String method, copied out */
  out[0] = 0; JNIEnv *e = env(); if (!cls || !m) return 0;
  jstring s = (jstring)(*e)->CallStaticObjectMethod(e, cls, m); if (ex(e) || !s) return 0;
  const char *c = (*e)->GetStringUTFChars(e, s, NULL); if (c) { snprintf(out, cap, "%s", c); (*e)->ReleaseStringUTFChars(e, s, c); }
  (*e)->DeleteLocalRef(e, s); return out[0] != 0;
}

void video_load(const char *url) {
  JNIEnv *e = env(); if (!cls) return; err[0] = 0;
  { const char *u = store_get("gpuUpscale", "off"); (*e)->CallStaticVoidMethod(e, cls, mUp, (jint)(!strcmp(u, "high") ? 3 : !strcmp(u, "medium") ? 2 : !strcmp(u, "low") ? 1 : 0)); ex(e); } /* Settings › Playback › Upscale */
  (*e)->CallStaticVoidMethod(e, cls, mPass, (jboolean)!strcmp(store_get("audioPassthrough", "false"), "true")); ex(e); /* Dolby and DTS: decoded by the TV, or sent as they are (Settings › Audio) */
  jstring s = (*e)->NewStringUTF(e, url); (*e)->CallStaticVoidMethod(e, cls, mLoad, s); (*e)->DeleteLocalRef(e, s); ex(e);
}
void video_load_timed(const char *base, double durSec) { (void)durSec; video_load(base); } /* (ExoPlayer seeks inside the stream itself) */
void video_unload(void) { callv(mUnload); }
void video_play(void) { callv(mPlay); }
void video_pause(void) { callv(mPause); }
void video_seek(double sec) { JNIEnv *e = env(); if (cls) { (*e)->CallStaticVoidMethod(e, cls, mSeek, (jlong)(sec * 1000)); ex(e); } }
double video_time(void) { return callj(mPos) / 1000.0; }
double video_duration(void) { jlong d = callj(mDur); return d > 0 ? d / 1000.0 : 0; }
double video_buffered_end(void) { return callj(mBuf) / 1000.0; }
/* OriginVideo.state(): 0 idle, 1 loading, 2 playing, 3 paused, 4 buffering, 5 ended, 6 error (VidState's order) */
VidState video_state(void) { int s = calli(mState); return s >= VID_IDLE && s <= VID_ERROR ? (VidState)s : VID_IDLE; }
const char *video_error(void) { char b[256]; if (calls(mErr, b, sizeof b) || cls) snprintf(err, sizeof err, "%s", b); return err; }
int video_live(void) { VidState s = video_state(); return (s == VID_PLAYING || s == VID_PAUSED || s == VID_BUFFERING) && video_width() > 0; }
int video_has_media(void) { return video_state() != VID_IDLE; }
void video_set_volume(int pct, int easeMs) { (void)easeMs; JNIEnv *e = env(); if (cls) { (*e)->CallStaticVoidMethod(e, cls, mVolume, (jfloat)(pct / 100.0f)); ex(e); } }
int video_settling(void) { return 0; }
int video_width(void) { return calli(mW); }
int video_height(void) { return calli(mH); }
double video_fps(void) { JNIEnv *e = env(); if (!cls) return 0; jfloat f = (*e)->CallStaticFloatMethod(e, cls, mFps); return ex(e) || f <= 0 ? 0 : f; }
int video_hdr(char *out, int cap) { return calls(mHdr, out, (size_t)cap); }
/* "idx\tlang\tname\tcodec\tchannels\tforced" a line for each audio track ExoPlayer has */
int video_audio_tracks(VidTrack *out, int max) {
  static char b[8192]; if (!calls(mTracks, b, sizeof b)) return 0;
  int n = 0; for (char *p = b; *p && n < max; ) {
    char *e = strchr(p, '\n'); if (e) *e = 0;
    char *f[6] = { 0 }; int nf = 0; for (char *q = p; nf < 6; ) { f[nf++] = q; char *t = strchr(q, '\t'); if (!t) break; *t = 0; q = t + 1; }
    if (nf >= 4) { VidTrack *t = &out[n++]; memset(t, 0, sizeof *t); t->idx = atoi(f[0]);
      snprintf(t->lang, sizeof t->lang, "%s", f[1]); snprintf(t->name, sizeof t->name, "%s", f[2]); snprintf(t->codec, sizeof t->codec, "%s", f[3]);
      t->channels = nf > 4 ? atoi(f[4]) : 0; t->forced = nf > 5 ? atoi(f[5]) : 0; }
    if (!e) break; p = e + 1;
  }
  return n;
}
int video_audio_current(void) { return calli(mAudCur); }
void video_select_audio(int idx) { JNIEnv *e = env(); if (cls) { (*e)->CallStaticVoidMethod(e, cls, mSelAud, (jint)idx); ex(e); } }
int video_text_tracks(VidTrack *out, int max) { (void)out; (void)max; return 0; }
void video_select_text(int idx) { (void)idx; }
/* layout units (1920x1080) to the view's fractions: Java places the SurfaceView */
void video_set_window(float x, float y, float w, float h) {
  float f[4]; gfx_layout_to_window_frac(x, y, w, h, f); JNIEnv *e = env(); if (cls) { (*e)->CallStaticVoidMethod(e, cls, mWindow, f[0], f[1], f[2], f[3]); ex(e); }
}
int video_partial_plane(void) { return 1; }
/* the part (sx, sy, sw, sh) of the frame shown in (dx, dy, dw, dh): the whole frame laid out at that scale, larger
   than the screen, which shows only its own part of it */
int video_set_crop(int sx, int sy, int sw, int sh, float dx, float dy, float dw, float dh) {
  int vw = video_width(), vh = video_height(); if (vw <= 0 || vh <= 0 || sw <= 0 || sh <= 0) return 0;
  float kx = dw / sw, ky = dh / sh;
  video_set_window(dx - sx * kx, dy - sy * ky, vw * kx, vh * ky);
  return 1;
}
void video_set_surface(int w, int h) { (void)w; (void)h; }
int video_audio_unsupported(void) { return calli(mUnsup); }
void video_pump(void) {}
