/* Interface sounds: short soft taps (assets/sounds/<name>.wav, from Kenney's Interface Sounds, CC0), one for each kind of
   move: the focus (glass_006), the main tabs (glass_003), the Library's filters (glass_002), a switch or a check box
   (toggle_004), holding OK on a title (maximize_008, a low swell under the line), and going into the player
   (confirmation_001), and once at start-up, very faint (confirmation_004). They play through SDL's audio on a thread of their own: loading them and opening
   the device can take a moment on a TV, and the drawing never waits for that. The device is let go after a while
   without sounds and whenever the player is up, so a film always has the audio to itself. */
#include "ui/sound.h"
#define SOUND_DEF "true"
#include "core/store.h"
#include "core/app.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define IDLE_CLOSE 12.0 /* s without a sound: the device goes */
static const struct { const char *file; float vol; } DEF[SND_N] = { /* each one's loudness beside the focus tap; the level in Settings scales them all */
  { "focus", 1.0f }, { "tab", 1.07f }, { "filter", 0.9f }, { "toggle", 0.95f }, { "hold", 0.7f }, { "play", 0.66f }, { "start", 0.5f } };
static struct { Sint16 *pcm, *soft; Uint32 bytes; } SN[SND_N];
static SDL_AudioSpec spec; static int haveSpec;
static SDL_sem *wake; static SDL_atomic_t want, quiet, softNext, level; static int started;
static SDL_AudioDeviceID dev; static double lastAsk;

/* each sound, read once: 16-bit, at its own loudness and a quieter copy (a held key) */
static void load(void) {
  for (int k = 0; k < SND_N; k++) {
    char p[1200]; snprintf(p, sizeof p, "%s/sounds/%s.wav", plat_asset_dir(), DEF[k].file);
    Uint8 *buf; Uint32 len; SDL_AudioSpec w;
    if (!SDL_LoadWAV(p, &w, &buf, &len)) { fprintf(stderr, "[sound] %s: %s\n", p, SDL_GetError()); continue; }
    if (w.format != AUDIO_S16SYS || (haveSpec && (w.freq != spec.freq || w.channels != spec.channels))) { SDL_FreeWAV(buf); fprintf(stderr, "[sound] %s: not 16-bit, or not like the others\n", p); continue; }
    int n = (int)(len / 2); const Sint16 *src = (const Sint16 *)buf; SN[k].pcm = malloc(len); SN[k].soft = malloc(len);
    for (int i = 0; i < n; i++) { float v = src[i] * DEF[k].vol; if (v > 32767) v = 32767; if (v < -32768) v = -32768; SN[k].pcm[i] = (Sint16)v; SN[k].soft[i] = (Sint16)(v * 0.55f); }
    SN[k].bytes = len; if (!haveSpec) { spec = w; haveSpec = 1; } SDL_FreeWAV(buf);
  }
}
/* the queue calls came with SDL 2.0.4: looked up, so a TV with an older SDL starts all the same, without the clicks */
#include <dlfcn.h>
static int (*qAudio)(SDL_AudioDeviceID, const void *, Uint32); static Uint32 (*qSize)(SDL_AudioDeviceID); static void (*qClear)(SDL_AudioDeviceID);
static int q_ok(void) { static int ok = -1; if (ok < 0) {
#ifdef SB_WEBOS
  *(void **)(&qAudio) = dlsym(RTLD_DEFAULT, "SDL_QueueAudio"); *(void **)(&qSize) = dlsym(RTLD_DEFAULT, "SDL_GetQueuedAudioSize"); *(void **)(&qClear) = dlsym(RTLD_DEFAULT, "SDL_ClearQueuedAudio");
#else
  qAudio = SDL_QueueAudio; qSize = SDL_GetQueuedAudioSize; qClear = SDL_ClearQueuedAudio; /* (the preview links its own SDL) */
#endif
 
  ok = qAudio && qSize && qClear; if (!ok) fprintf(stderr, "[sound] this SDL has no audio queue: no interface sounds\n"); } return ok; }
static void dev_close(void) { if (dev) { SDL_CloseAudioDevice(dev); dev = 0; } }
static int dev_open(void) {
  if (!q_ok()) return 0;
  if (dev) return 1;
  static int said; /* the log: how the first opening went, once */
  if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { if (!said++) fprintf(stderr, "[sound] no audio: %s\n", SDL_GetError()); return 0; }
  SDL_AudioSpec w = spec; w.samples = 512; w.callback = NULL; w.userdata = NULL;
  dev = SDL_OpenAudioDevice(NULL, 0, &w, NULL, 0);
  if (!said++) fprintf(stderr, dev ? "[sound] on (%s)\n" : "[sound] no device: %s\n", dev ? SDL_GetCurrentAudioDriver() : SDL_GetError());
  if (!dev) return 0;
  SDL_PauseAudioDevice(dev, 0); return 1;
}
static int run(void *u) {
  (void)u; double last = 0; load();
  for (;;) {
    SDL_SemWaitTimeout(wake, SDL_AtomicGet(&quiet) && dev ? 30 : 1000);
    double now = SDL_GetTicks() / 1000.0; int w = SDL_AtomicSet(&want, 0) - 1;
    if (w >= 0 && w < SND_N && SN[w].bytes && haveSpec && dev_open()) {
      const Sint16 *src = SDL_AtomicGet(&softNext) ? SN[w].soft : SN[w].pcm; int n = (int)(SN[w].bytes / 2); float lv = SDL_AtomicGet(&level) / 100.0f;
      Sint16 *out = malloc(SN[w].bytes); if (out) { for (int i = 0; i < n; i++) out[i] = (Sint16)(src[i] * lv); qClear(dev); qAudio(dev, out, SN[w].bytes); free(out); } last = now; }
    if (SDL_AtomicGet(&quiet)) { if (dev && qSize(dev) == 0) dev_close(); continue; } /* the player: what plays finishes first */
    if (dev && now - last > IDLE_CLOSE) dev_close();
  }
  return 0;
}
void sound_play(int which) {
  if (which == SND_PLAY || which == SND_START || which == SND_HOLD) return; /* two sounds only: the tabs' and one for everything else */
  if (which != SND_TAB) which = SND_FOCUS;
  if (which < 0 || which >= SND_N || SDL_AtomicGet(&quiet) || !strcmp(store_get("uiSound", SOUND_DEF), "false")) return;
  static double prev[SND_N], prevAny; double now = app_now();
  if (now - prev[which] < 0.06) return; /* asked twice for one move (the key and the drawing): once */
  SDL_AtomicSet(&softNext, which != SND_HOLD && which != SND_PLAY && which != SND_START && now - prevAny < 0.16); /* a held key: quieter */
  SDL_AtomicSet(&level, atoi(store_get("uiSoundVol", "30"))); /* the level chosen in Settings, in % of the full sound */
  prev[which] = prevAny = lastAsk = now;
  if (!started) { started = 1; wake = SDL_CreateSemaphore(0); SDL_Thread *t = SDL_CreateThread(run, "sound", NULL); if (t) SDL_DetachThread(t); else { started = -1; return; } }
  if (started < 0) return;
  SDL_AtomicSet(&want, which + 1); SDL_SemPost(wake);
}
void sound_focus(void) { sound_play(SND_FOCUS); }
double sound_last(void) { return lastAsk; }
void sound_quiet(int on) { SDL_AtomicSet(&quiet, on); if (started > 0) SDL_SemPost(wake); }
