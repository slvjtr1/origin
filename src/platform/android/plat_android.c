/* Android TV and Google TV (Android 9, API 28, and later): SDL2's Java activity hosts the app, GLES2 draws it, in
   landscape, full screen, as on a webOS TV. Assets ship inside the APK, where fopen cannot reach them: the first start
   of each version copies them (the list is assets.lst, written by the build) to the app's private storage, and the
   app reads them there as it does on webOS. The remote's keys are read by the activity itself (OriginActivity.java),
   before SDL sees them, and come here already as the app's own key codes: every Android TV remote's key has its
   place, from the D-pad, OK and Back of a Google TV remote to the media, colour, channel, Info and Captions keys of
   bigger ones. */
#include "platform/plat.h"
#include "core/keys.h"
#include "core/applog.h"
#include <SDL2/SDL_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/system_properties.h>
#include <unistd.h>

static char assetDir[1024], dataDir[1024], devName[160], locale[24];

static void mkdirs(const char *d) {
  char p[1100]; snprintf(p, sizeof p, "%s", d);
  for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0700); *s = '/'; } mkdir(p, 0700);
}
/* one file from the APK's assets to dst; 0 when it is not there */
static int copy_asset(const char *name, const char *dst) {
  SDL_RWops *in = SDL_RWFromFile(name, "rb"); if (!in) return 0;
  char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.part", dst);
  FILE *o = fopen(tmp, "wb"); if (!o) { SDL_RWclose(in); return 0; }
  char buf[65536]; size_t n; int ok = 1;
  while ((n = SDL_RWread(in, buf, 1, sizeof buf)) > 0) if (fwrite(buf, 1, n, o) != n) { ok = 0; break; }
  SDL_RWclose(in); if (fclose(o)) ok = 0;
  if (ok) ok = rename(tmp, dst) == 0; else unlink(tmp);
  return ok;
}
/* the assets once per version: assets.lst names every file (one path a line), its first line is the version */
static void unpack_assets(void) {
  SDL_RWops *l = SDL_RWFromFile("assets.lst", "rb"); if (!l) { fprintf(stderr, "[start] no assets.lst in the package\n"); return; }
  Sint64 n = SDL_RWsize(l); if (n <= 0 || n > (8 << 20)) { SDL_RWclose(l); return; }
  char *lst = malloc((size_t)n + 1); if (!lst) { SDL_RWclose(l); return; }
  size_t got = SDL_RWread(l, lst, 1, (size_t)n); SDL_RWclose(l); lst[got] = 0;
  char *nl = strchr(lst, '\n'); if (!nl) { free(lst); return; } *nl = 0; const char *ver = lst;
  char stamp[1100], have[64] = ""; snprintf(stamp, sizeof stamp, "%s/.version", assetDir);
  FILE *s = fopen(stamp, "r"); if (s) { if (!fgets(have, sizeof have, s)) have[0] = 0; fclose(s); have[strcspn(have, "\r\n")] = 0; }
  if (!strcmp(have, ver)) { free(lst); return; }
  int ok = 0, bad = 0;
  for (char *p = nl + 1; *p; ) {
    char *e = strchr(p, '\n'); if (e) *e = 0; p[strcspn(p, "\r")] = 0;
    if (*p && !strstr(p, "..")) {
      char dst[1200]; snprintf(dst, sizeof dst, "%s/%s", assetDir, p);
      char *sl = strrchr(dst, '/'); if (sl) { *sl = 0; mkdirs(dst); *sl = '/'; }
      if (copy_asset(p, dst)) ok++; else bad++;
    }
    if (!e) break; p = e + 1;
  }
  if (!bad && (s = fopen(stamp, "w"))) { fputs(ver, s); fclose(s); }
  fprintf(stderr, "[start] assets %s: %d copied%s\n", ver, ok, bad ? ", some failed (tried again next start)" : "");
  free(lst);
}

void plat_pre_init(const char *appId) {
  (void)appId;
  const char *in = SDL_AndroidGetInternalStoragePath(); /* /data/user/0/<package>/files */
  snprintf(dataDir, sizeof dataDir, "%s/data", in ? in : "/data/local/tmp");
  snprintf(assetDir, sizeof assetDir, "%s/assets", in ? in : "/data/local/tmp");
  mkdirs(dataDir); mkdirs(assetDir);
  applog_start(dataDir);
  unpack_assets();
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* Back is the app's (K_BACK): Home's "press again to exit" ends it */
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
}
/* the drawing thread at Android's display priority (its own UI and render threads run there): a frame never waits
   behind the pictures, the torrent engine or the network */
void plat_post_window(SDL_Window *w) { (void)w; if (setpriority(PRIO_PROCESS, (id_t)gettid(), -4) != 0) setpriority(PRIO_PROCESS, (id_t)gettid(), -2); }
const char *plat_asset_dir(void) { return assetDir; }
const char *plat_data_dir(void) { return dataDir; }
void plat_window_size(int *w, int *h, Uint32 *flags) {
  SDL_DisplayMode m; if (SDL_GetDesktopDisplayMode(0, &m) == 0 && m.w > 0 && m.h > 0) { *w = m.w; *h = m.h; } else { *w = 1920; *h = 1080; }
  *flags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI;
}
/* OriginActivity sends the remote's keys already mapped (see nativeKey below): they arrive with the app's code in
   keysym.unused, marked. A keyboard plugged into the TV comes through SDL as it is. */
#define KEY_MARK 0x4b000000u
int plat_map_key(const SDL_KeyboardEvent *e) {
  if ((e->keysym.unused & 0xff000000u) == KEY_MARK) return (int)(e->keysym.unused & 0xffff);
  switch (e->keysym.sym) {
    case SDLK_AC_BACK: case SDLK_ESCAPE: return K_BACK;
    case SDLK_LEFT: return K_LEFT; case SDLK_RIGHT: return K_RIGHT; case SDLK_UP: return K_UP; case SDLK_DOWN: return K_DOWN;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SELECT: return K_OK;
    case SDLK_AUDIOPLAY: return K_PLAYPAUSE; case SDLK_AUDIOSTOP: return K_STOP;
    case SDLK_AUDIOREWIND: return K_REW; case SDLK_AUDIOFASTFORWARD: return K_FF;
    case SDLK_PAUSE: return K_PAUSE; case SDLK_SPACE: return K_PLAYPAUSE;
    default: return K_NONE;
  }
}
#include <jni.h>
/* OriginActivity.nativeKey: one key from the remote, already the app's code, as an SDL key event (any thread) */
JNIEXPORT void JNICALL Java_app_stremioorigin_OriginActivity_nativeKey(JNIEnv *env, jclass cls, jint k, jint down, jint repeat) {
  (void)env; (void)cls; if (k <= 0 || k > 0xffff) return;
  SDL_Event ev; memset(&ev, 0, sizeof ev);
  ev.type = down ? SDL_KEYDOWN : SDL_KEYUP; ev.key.state = down ? SDL_PRESSED : SDL_RELEASED; ev.key.repeat = repeat ? 1 : 0;
  ev.key.timestamp = SDL_GetTicks(); ev.key.keysym.unused = KEY_MARK | (Uint32)k;
  SDL_PushEvent(&ev);
}
/* OriginActivity.nativeText: words for the page on the screen (voice search), to the main loop as SB_EV_TEXT with its own copy */
JNIEXPORT void JNICALL Java_app_stremioorigin_OriginActivity_nativeText(JNIEnv *env, jclass cls, jstring s) {
  (void)cls; if (!s) return; const char *t = (*env)->GetStringUTFChars(env, s, NULL); if (!t) return;
  char *c = strdup(t); (*env)->ReleaseStringUTFChars(env, s, t); if (!c) return;
  SDL_Event ev; memset(&ev, 0, sizeof ev); ev.type = SDL_USEREVENT; ev.user.code = SB_EV_TEXT; ev.user.data1 = c;
  if (SDL_PushEvent(&ev) != 1) free(c);
}
int plat_text_input(void) { return 0; } /* the app's own keyboard: made for the D-pad (and the phone, through Your phone as a keyboard) */
const char *plat_device_name(void) {
  if (!devName[0]) {
    char man[PROP_VALUE_MAX] = "", model[PROP_VALUE_MAX] = "", rel[PROP_VALUE_MAX] = "";
    __system_property_get("ro.product.manufacturer", man); __system_property_get("ro.product.model", model); __system_property_get("ro.build.version.release", rel);
    snprintf(devName, sizeof devName, "Android TV %s %s %s", rel, man, model);
  }
  return devName;
}
int plat_mem_total_mb(void) {
  FILE *f = fopen("/proc/meminfo", "r"); if (!f) return 0;
  char line[128]; long kb = 0; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemTotal: %ld kB", &kb) == 1) break;
  fclose(f); return (int)(kb / 1024);
}
/* per thread, as on webOS: the helpers a few steps under the drawing thread (SDL's "low" was nice 19, where pictures
   waited behind every other app's work) */
void plat_thread_background(int nice) { setpriority(PRIO_PROCESS, (id_t)gettid(), nice); }
/* SDL pauses the activity's drawing in the background: the app hears it as a TV's "sent to the back" */
int plat_lifecycle(const SDL_Event *e) {
  if (e->type == SDL_APP_DIDENTERFOREGROUND) return 1;
  if (e->type == SDL_APP_WILLENTERBACKGROUND) return -1;
  return 0;
}
const char *plat_ui_locale(void) {
  if (!locale[0]) {
    SDL_Locale *l = SDL_GetPreferredLocales();
    if (l && l[0].language) snprintf(locale, sizeof locale, "%s%s%s", l[0].language, l[0].country ? "-" : "", l[0].country ? l[0].country : "");
    SDL_free(l);
  }
  return locale[0] ? locale : NULL;
}
char *plat_luna_call(const char *uri, const char *payload, int timeoutMs) { (void)uri; (void)payload; (void)timeoutMs; return NULL; }
unsigned long plat_luna_subscribe(const char *uri, const char *payload, PlatLunaCb cb, void *ud) { (void)uri; (void)payload; (void)cb; (void)ud; return 0; }
void plat_luna_cancel(unsigned long handle) { (void)handle; }
void plat_luna_pump(void) {}
int plat_luna_send(const char *uri, const char *payload) { (void)uri; (void)payload; return 0; }
void plat_cursor_hide(void) {}

/* ---- the Java side (OriginBridge): see android.h ---- */
#include "platform/android/android.h"
static jclass bridge(JNIEnv *e) {
  static jclass c; if (c) return c;
  jobject act = (jobject)SDL_AndroidGetActivity(); if (!act) return NULL;
  jclass ac = (*e)->GetObjectClass(e, act); jmethodID gl = (*e)->GetMethodID(e, ac, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject ld = (*e)->CallObjectMethod(e, act, gl); jclass lc = (*e)->FindClass(e, "java/lang/ClassLoader");
  jmethodID lm = (*e)->GetMethodID(e, lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"); jstring nm = (*e)->NewStringUTF(e, "app.stremioorigin.OriginBridge");
  jclass k = (jclass)(*e)->CallObjectMethod(e, ld, lm, nm);
  if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); k = NULL; }
  if (k) c = (jclass)(*e)->NewGlobalRef(e, k);
  (*e)->DeleteLocalRef(e, nm); (*e)->DeleteLocalRef(e, act); (*e)->DeleteLocalRef(e, ac); (*e)->DeleteLocalRef(e, ld); (*e)->DeleteLocalRef(e, lc); if (k) (*e)->DeleteLocalRef(e, k);
  return c;
}
void android_cmd(const char *name, const char *a, const char *b, int n) {
  JNIEnv *e = (JNIEnv *)SDL_AndroidGetJNIEnv(); if (!e) return; jclass c = bridge(e); if (!c) return;
  static jmethodID m; if (!m) m = (*e)->GetStaticMethodID(e, c, "cmd", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V"); if (!m) { (*e)->ExceptionClear(e); return; }
  jstring sn = (*e)->NewStringUTF(e, name), sa = a ? (*e)->NewStringUTF(e, a) : NULL, sb = b ? (*e)->NewStringUTF(e, b) : NULL;
  (*e)->CallStaticVoidMethod(e, c, m, sn, sa, sb, (jint)n);
  if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionDescribe(e); (*e)->ExceptionClear(e); }
  (*e)->DeleteLocalRef(e, sn); if (sa) (*e)->DeleteLocalRef(e, sa); if (sb) (*e)->DeleteLocalRef(e, sb);
}
static void str_call(const char *name, char *out, size_t cap) { /* a static String method of OriginBridge, copied out */
  out[0] = 0; JNIEnv *e = (JNIEnv *)SDL_AndroidGetJNIEnv(); if (!e) return; jclass c = bridge(e); if (!c) return;
  jmethodID m = (*e)->GetStaticMethodID(e, c, name, "()Ljava/lang/String;"); if (!m) { (*e)->ExceptionClear(e); return; }
  jstring s = (jstring)(*e)->CallStaticObjectMethod(e, c, m); if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); return; }
  if (s) { const char *t = (*e)->GetStringUTFChars(e, s, NULL); if (t) { snprintf(out, cap, "%s", t); (*e)->ReleaseStringUTFChars(e, s, t); } (*e)->DeleteLocalRef(e, s); }
}
const char *android_update_status(void) { static char out[160]; str_call("updateStatus", out, sizeof out); return out; }
/* the panel's real lines (2160 on a 4K TV, whose Android draws its pages at 1080) */
int plat_panel_lines(void) {
  static int lines = -1; if (lines < 0) { char b[24]; str_call("panelLines", b, sizeof b); lines = atoi(b); }
  if (lines > 0) return lines;
  SDL_DisplayMode m; return SDL_GetDesktopDisplayMode(0, &m) == 0 ? (m.h < m.w ? m.h : m.w) : 0;
}
