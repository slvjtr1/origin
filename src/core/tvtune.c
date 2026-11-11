#include "core/tvtune.h"
#include "core/store.h"
#include "core/i18n.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXEC "luna://org.webosbrew.hbchannel.service/exec"
#define SAVED "tvTuneSaved"      /* { category: { key: value before } } until put back */
#define SAVED_OLD "animePicSaved" /* (the anime-only picture of before: a flat picture map) */
#define MAXW 24

typedef struct { const char *cat, *k, *v; int rel, lo, hi; } Want; /* rel: v is a step from the TV's own value, kept within lo..hi */
typedef struct { int g, n, upscale; char note[96]; Want w[MAXW]; } Job;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
#ifdef SB_ANDROID
static int on, gen, notice, clearSaved, avail = 0; /* Android TV: no app reaches the TV's own picture processing (the LG settings it tunes are webOS's) */
#else
static int on, gen, notice, clearSaved, avail = -1;
#endif
static char noteTxt[96];
static cJSON *toSave, *saved;

static int is_on(const char *k, const char *d) { return !strcmp(store_get(k, d), "true"); }
const char *tvtune_level(int anime) {
  const char *v = store_get(anime ? "animeEnhance" : "filmEnhance", anime ? "A" : "off");
  if (!strcmp(v, "true")) return "A"; if (!strcmp(v, "false")) return "off"; /* (a switch before it had levels) */
  return !strcmp(v, "A") || !strcmp(v, "B") || !strcmp(v, "C") ? v : "off";
}

/* ---- through the Channel ---- */
static cJSON *sh(const char *cmd) {
  cJSON *p = cJSON_CreateObject(); cJSON_AddStringToObject(p, "command", cmd); char *ps = cJSON_PrintUnformatted(p); cJSON_Delete(p);
  char *r = ps ? plat_luna_call(EXEC, ps, 4000) : NULL; free(ps); if (!r) return NULL;
  cJSON *j = cJSON_Parse(r); free(r); if (!j) return NULL;
  cJSON *rv = cJSON_GetObjectItemCaseSensitive(j, "returnValue"), *out = cJSON_GetObjectItemCaseSensitive(j, "stdoutString");
  cJSON *o = cJSON_IsTrue(rv) && cJSON_IsString(out) ? cJSON_Parse(out->valuestring) : NULL; cJSON_Delete(j); return o;
}
/* only letters, digits and a few marks go into the command: nothing can leave its quotes */
static int clean(const char *s) { for (; *s; s++) { char c = *s; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("_-.:,{}[]\" ", c))) return 0; } return 1; }
static int set_cat(const char *cat, const cJSON *settings) {
  char *js = cJSON_PrintUnformatted(settings); if (!js) return 0; int ok = 0;
  if (clean(js) && clean(cat)) { char cmd[1400]; snprintf(cmd, sizeof cmd, "luna-send -n 1 -f luna://com.webos.settingsservice/setSystemSettings '{\"category\":\"%s\",\"settings\":%s}'", cat, js);
    cJSON *o = sh(cmd); ok = o && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "returnValue")); cJSON_Delete(o); }
  free(js); return ok;
}
/* a whole category at once; refused, each key alone (one this model does not take never stops the others). out: the keys taken */
static int set_keys(const char *cat, const cJSON *settings, cJSON *out) {
  if (!cJSON_GetArraySize(settings)) return 1;
  if (set_cat(cat, settings)) { const cJSON *x; cJSON_ArrayForEach(x, settings) if (out) cJSON_AddStringToObject(out, x->string, x->valuestring); return 1; }
  int any = 0; const cJSON *x; cJSON_ArrayForEach(x, settings) { cJSON *one = cJSON_CreateObject(); cJSON_AddStringToObject(one, x->string, x->valuestring);
    if (set_cat(cat, one)) { any = 1; if (out) cJSON_AddStringToObject(out, x->string, x->valuestring); } else fprintf(stderr, "[tvtune] %s.%s not taken by this TV\n", cat, x->string); cJSON_Delete(one); }
  return any;
}
static cJSON *get_cat(const char *cat, const Job *j) {
  char cmd[1000]; int n = snprintf(cmd, sizeof cmd, "luna-send -n 1 -f luna://com.webos.settingsservice/getSystemSettings '{\"category\":\"%s\",\"keys\":[", cat); int k = 0;
  for (int i = 0; i < j->n && n < (int)sizeof cmd - 40; i++) if (!strcmp(j->w[i].cat, cat)) n += snprintf(cmd + n, sizeof cmd - n, "%s\"%s\"", k++ ? "," : "", j->w[i].k);
  snprintf(cmd + n, sizeof cmd - n, "]}'"); return sh(cmd);
}

/* ---- the worker ---- */
static void restore(cJSON *s) {
  int ok = 1; cJSON *c; cJSON_ArrayForEach(c, s) if (cJSON_IsObject(c) && c->string) ok &= set_keys(c->string, c, NULL);
  fprintf(stderr, "[tvtune] picture and sound %s\n", ok ? "put back" : "not all put back (tried again at the next start)");
  if (ok) { pthread_mutex_lock(&mu); clearSaved = 1; pthread_mutex_unlock(&mu); }
}
static void *stop_job(void *ud) { cJSON *s = ud; restore(s); cJSON_Delete(s); return NULL; }
static void *start_job(void *ud) {
  Job *j = ud; static const char *const CATS[3] = { "picture", "aiPicture", "sound" };
  cJSON *old = cJSON_CreateObject(); int reached = 0, upOk = 0, changed = 0;
  for (int c = 0; c < 3; c++) { int need = 0; for (int i = 0; i < j->n; i++) need |= !strcmp(j->w[i].cat, CATS[c]); if (!need) continue;
    pthread_mutex_lock(&mu); int still = j->g == gen; pthread_mutex_unlock(&mu); if (!still) break;
    cJSON *o = get_cat(CATS[c], j), *have = o ? cJSON_GetObjectItemCaseSensitive(o, "settings") : NULL; if (o) reached = 1;
    cJSON *nw = cJSON_CreateObject(), *was = cJSON_CreateObject();
    for (int i = 0; i < j->n && cJSON_IsObject(have); i++) { const Want *w = &j->w[i]; if (strcmp(w->cat, CATS[c])) continue;
      const cJSON *v = cJSON_GetObjectItemCaseSensitive(have, w->k); if (!cJSON_IsString(v) || !clean(v->valuestring) || cJSON_GetObjectItemCaseSensitive(nw, w->k)) continue;
      char val[24]; if (w->rel) { char *e; long cur = strtol(v->valuestring, &e, 10); if (e == v->valuestring || *e) continue; long t = cur + atoi(w->v); if (t < w->lo) t = w->lo; if (t > w->hi) t = w->hi; if (t == cur && w->rel) continue; snprintf(val, sizeof val, "%ld", t); }
      else snprintf(val, sizeof val, "%s", w->v);
      if (!strcmp(v->valuestring, val)) { if (!strcmp(w->k, "superResolution")) upOk = 1; continue; } /* already so */
      cJSON_AddStringToObject(nw, w->k, val); cJSON_AddStringToObject(was, w->k, v->valuestring); }
    cJSON *took = cJSON_CreateObject(); set_keys(CATS[c], nw, took);
    cJSON *kept = cJSON_CreateObject(); const cJSON *x; cJSON_ArrayForEach(x, took) { const cJSON *b = cJSON_GetObjectItemCaseSensitive(was, x->string); if (cJSON_IsString(b)) { cJSON_AddStringToObject(kept, x->string, b->valuestring); changed++; if (!strcmp(x->string, "superResolution")) upOk = 1; } }
    if (cJSON_GetArraySize(kept)) cJSON_AddItemToObject(old, CATS[c], kept); else cJSON_Delete(kept);
    cJSON_Delete(took); cJSON_Delete(nw); cJSON_Delete(was); cJSON_Delete(o); }
  fprintf(stderr, "[tvtune] %s: %d setting(s) changed\n", reached ? "tuned" : "no Homebrew Channel root: the TV stays as it is", changed);
  pthread_mutex_lock(&mu);
  if (reached) avail = 1; else if (avail < 0) avail = 0;
  int stale = j->g != gen; /* the player closed, or another level was chosen, while it worked */
  if (!stale) { if (cJSON_GetArraySize(old)) { cJSON_Delete(saved); saved = old; old = NULL; cJSON_Delete(toSave); toSave = cJSON_Duplicate(saved, 1); on = 1; }
    if (j->upscale && upOk) { notice = 1; snprintf(noteTxt, sizeof noteTxt, "%s", j->note); } }
  pthread_mutex_unlock(&mu);
  if (stale && cJSON_GetArraySize(old)) restore(old); /* its own changes back at once, here: before the next job reads the TV */
  cJSON_Delete(old); free(j);
  return NULL;
}
/* one worker, the jobs in the order asked: what was put back is back before the next title reads it */
static pthread_cond_t qcv = PTHREAD_COND_INITIALIZER; static void *(*qfn[16])(void *); static void *qarg[16]; static int qh, qn, worker;
static void *run(void *ud) { (void)ud;
  for (;;) { pthread_mutex_lock(&mu); while (!qn) pthread_cond_wait(&qcv, &mu); void *(*fn)(void *) = qfn[qh]; void *a = qarg[qh]; qh = (qh + 1) % 16; qn--; pthread_mutex_unlock(&mu); fn(a); } return NULL; }
static void spawn(void *(*fn)(void *), void *arg) {
  pthread_mutex_lock(&mu);
  if (!worker) { pthread_t t; if (!pthread_create(&t, NULL, run, NULL)) { pthread_detach(t); worker = 1; } }
  if (worker && qn < 16) { int i = (qh + qn) % 16; qfn[i] = fn; qarg[i] = arg; qn++; pthread_cond_signal(&qcv); pthread_mutex_unlock(&mu); return; }
  pthread_mutex_unlock(&mu);
  if (fn == stop_job) cJSON_Delete(arg); else free(arg); /* (no thread: nothing done) */
}

/* ---- what is asked ---- */
static void add(Job *j, const char *cat, const char *k, const char *v) { if (j->n < MAXW) j->w[j->n++] = (Want){ cat, k, v, 0, 0, 0 }; }
static void addr(Job *j, const char *cat, const char *k, const char *step, int lo, int hi) { if (j->n < MAXW) j->w[j->n++] = (Want){ cat, k, step, 1, lo, hi }; }
static void wish(Job *j, int anime) {
  const char *lv = tvtune_level(anime); int L = !strcmp(lv, "A") ? 3 : !strcmp(lv, "B") ? 2 : !strcmp(lv, "C") ? 1 : 0;
  if (L) { j->upscale = 1; snprintf(j->note, sizeof j->note, "%s", T1(anime ? "Anime upscale: {l}" : "Upscale: {l}", "l", T(L == 3 ? "High" : L == 2 ? "Medium" : "Low")));
    static const char *const SR[4] = { "off", "low", "medium", "high" };
    add(j, "picture", "superResolution", SR[L]);
    if (anime) { /* drawn pictures: clean lines, flat fills without bands, no smearing of fine line work */
      add(j, "picture", "smoothGradation", SR[L]); add(j, "picture", "mpegNoiseReduction", L == 3 ? "medium" : "low");
      if (L >= 2) add(j, "picture", "noiseReduction", "off");
      if (L >= 2) addr(j, "picture", "sharpness", L == 3 ? "4" : "2", 0, 50);
      if (L == 3) add(j, "picture", "edgeEnhancer", "on"); }
    else { /* filmed pictures: detail up, the grain left alone, the film's own motion kept */
      add(j, "picture", "noiseReduction", "off"); if (L >= 2) add(j, "picture", "mpegNoiseReduction", "low");
      addr(j, "picture", "sharpness", L == 3 ? "8" : L == 2 ? "5" : "3", 0, 50);
      if (L >= 2) add(j, "picture", "edgeEnhancer", "on");
      if (L == 3) add(j, "picture", "realCinema", "on"); } }
  if (is_on("brightBoost", "false")) { addr(j, "picture", "backlight", "20", 0, 100); add(j, "picture", "energySaving", "off"); }
  if (is_on("darkSee", "false")) { add(j, "aiPicture", "ai_Brightness", "on"); addr(j, "picture", "brightness", "2", 0, 56); }
  if (is_on("voiceClear", "true")) add(j, "sound", "clearVoice", "level2");
  if (is_on("evenVolume", "false")) add(j, "sound", "autoVolume", "on");
}

void tvtune_start(int anime) {
  if (tvtune_state() == 0) return; /* no root here: never asked again this session */
  Job *j = calloc(1, sizeof *j); if (!j) return; wish(j, anime); if (!j->n) { free(j); return; }
  pthread_mutex_lock(&mu); int go = !on; if (go) j->g = ++gen; pthread_mutex_unlock(&mu); /* (one still working becomes stale: it puts back its own) */
  if (go) spawn(start_job, j); else free(j);
}
void tvtune_stop(void) {
  pthread_mutex_lock(&mu); gen++; notice = 0; cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } pthread_mutex_unlock(&mu);
  if (s) spawn(stop_job, s);
}
void tvtune_cycle(int anime) {
  static const char *const NEXT[4][2] = { { "off", "C" }, { "C", "B" }, { "B", "A" }, { "A", "off" } };
  const char *cur = tvtune_level(anime); for (int i = 0; i < 4; i++) if (!strcmp(cur, NEXT[i][0])) { store_set(anime ? "animeEnhance" : "filmEnhance", NEXT[i][1]); break; }
  tvtune_stop();
  tvtune_start(anime);
}
int tvtune_poll(char *msg, size_t cap) { pthread_mutex_lock(&mu); int n = notice; notice = 0; if (n) snprintf(msg, cap, "%s", noteTxt); pthread_mutex_unlock(&mu); return n; }
int tvtune_state(void) { pthread_mutex_lock(&mu); int a = avail; pthread_mutex_unlock(&mu); return a; }
static void recover(void) {
  cJSON *s = store_node(SAVED), *o = store_node(SAVED_OLD);
  if (cJSON_IsObject(o) && cJSON_GetArraySize(o)) { cJSON *w = cJSON_CreateObject(); cJSON_AddItemToObject(w, "picture", cJSON_Duplicate(o, 1)); spawn(stop_job, w); store_remove(SAVED_OLD); }
  if (cJSON_IsObject(s) && cJSON_GetArraySize(s)) { fprintf(stderr, "[tvtune] left tuned when the app closed: putting it back\n"); spawn(stop_job, cJSON_Duplicate(s, 1)); }
}
void tvtune_tick(void) {
  static int first = 1; if (first) { first = 0; recover(); /* once, a moment after start */
    const char *a = store_get("animeEnhance", ""); if (!strcmp(a, "true")) store_set("animeEnhance", "A"); else if (!strcmp(a, "false")) store_set("animeEnhance", "off"); } /* (a switch before it had levels) */
  pthread_mutex_lock(&mu); cJSON *s = toSave; toSave = NULL; int c = clearSaved; clearSaved = 0; pthread_mutex_unlock(&mu);
  if (s) { store_set_node(SAVED, s); store_flush(); }
  else if (c && !on) { store_remove(SAVED); store_flush(); }
}
void tvtune_quit(void) {
  pthread_mutex_lock(&mu); cJSON *s = on ? saved : NULL; if (on) { saved = NULL; on = 0; } gen++; pthread_mutex_unlock(&mu);
  if (s) { int ok = 1; cJSON *c; cJSON_ArrayForEach(c, s) if (cJSON_IsObject(c) && c->string) ok &= set_keys(c->string, c, NULL); if (ok) store_remove(SAVED); cJSON_Delete(s); }
}
