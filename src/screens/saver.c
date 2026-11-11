/* The screensaver (ambient mode): one title at a time, its artwork drifting and zooming very slowly (the direction
   alternates from scene to scene) and crossfading into the next; the title's logo and a line about it low in a corner,
   coming in a moment after the picture and leaving before it; a large, light clock with the date; the app's mark
   small in another corner, both in pills of liquid glass bending the picture under them. Each picture is drawn once,
   dimmed and with its shading baked in: per frame it is two picture quads and a few small things. Every 40 s one thing
   moves (the mark, then the clock and the title, which trade sides): the glass flows there, the title fades across, so
   nothing stays on the same pixels of the panel. */
#include "screens/saver.h"
#include "screens/player.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/store.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/glass.h"
#include <math.h>
#include "core/tmdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SSW 1280
#define SSH 720
#define MAXSS 16
extern int board_saver_items(Item *out, int max);
typedef struct { GfxTarget t; int ok, kb; double at; Tween a; } SsLayer;
static struct {
  int on, n, i, top, loading; Item list[MAXSS]; char url[512];
  SsLayer L[2]; Tween fade; double startAt, sceneAt, nextAt, checkAt, freeAt; int scene;
  /* the title and its line */
  Item cur; char logo[512], meta[96]; int infoWant, infoOut; Tween infoA, infoY; double logoWait;
  /* the clock */
  char hh[4], mm[4], date[96]; Timer1 hhNew, mmNew; double clockAt;
  /* burn-in shifts */
  int shN, mir, imir, markR, pairGone, markGone; double shiftAt, swapAt; Tween pairA, markA, pairInfoA; float clockDx, clockDy, infoDx, infoDy, markDx, markDy;
  /* the clock's and the mark's glass, flowing to their places: where they are, how fast */
  float cX, cY, cVX, cVY, mX, mY, mVX, mVY; int flowInit;
} SS;

/* elsewhere than Home (a page, the guide, the sign-in): no screensaver, the screen only dims; a key brings it back */
static int dimOn; static Tween dimA;
int saver_on(void) { return SS.on || dimOn; }
int saver_covers(void) { return SS.on && SS.fade.v >= 0.999f; }

/* ---- the clock and the date in the app's language ---- */
static void fill3(char *out, size_t cap, const char *fmt, const char *a, const char *b, const char *c) {
  const char *keys[3] = { "{weekday}", "{month}", "{day}" }, *vals[3] = { a, b, c }; size_t o = 0;
  while (*fmt && o + 1 < cap) { int hit = 0;
    for (int k = 0; k < 3; k++) { size_t kl = strlen(keys[k]); if (!strncmp(fmt, keys[k], kl)) { size_t vl = strlen(vals[k]); if (vl > cap - 1 - o) vl = cap - 1 - o; memcpy(out + o, vals[k], vl); o += vl; fmt += kl; hit = 1; break; } }
    if (!hit) out[o++] = *fmt++; }
  out[o] = 0;
}
static void clock_update(void) {
  time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); char h[4], m[4]; snprintf(h, sizeof h, "%02d", tm.tm_hour); snprintf(m, sizeof m, "%02d", tm.tm_min);
  if (strcmp(h, SS.hh)) { if (SS.hh[0]) timer_start(&SS.hhNew, 0.8f); snprintf(SS.hh, sizeof SS.hh, "%s", h); } /* a new minute fades in; nothing else moves */
  if (strcmp(m, SS.mm)) { if (SS.mm[0]) timer_start(&SS.mmNew, 0.8f); snprintf(SS.mm, sizeof SS.mm, "%s", m); }
  static const char *WD[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
  static const char *MO[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
  char d[24]; snprintf(d, sizeof d, "%d", tm.tm_mday);
  if (!strcmp(i18n_lang(), "ar")) { char a[24] = ""; size_t o = 0; for (const char *p = d; *p && o + 3 < sizeof a; p++) { a[o++] = (char)0xd9; a[o++] = (char)(0xa0 + (*p - '0')); } a[o] = 0; snprintf(d, sizeof d, "%s", a); } /* Arabic-Indic digits, as the language writes dates */
  char wd[48], mo[48]; snprintf(wd, sizeof wd, "%s", T(WD[tm.tm_wday])); snprintf(mo, sizeof mo, "%s", T(MO[tm.tm_mon]));
  fill3(SS.date, sizeof SS.date, T("{weekday}, {month} {day}"), wd, mo, d);
}

/* ---- scenes ---- */
static void bake(SsLayer *l, GLuint tex, float asp) {
  if (!l->t.fbo && !gfx_target_create(&l->t, SSW, SSH)) { l->ok = 0; return; }
  gfx_target_begin(&l->t, RGB(0, 0, 0));
  float u0, v0, u1, v1; img_cover_uv(asp, SB_W / SB_H, 0, &u0, &v0, &u1, &v1);
  gfx_image(tex, 0, 0, SB_W, SB_H, 0, WHITE_A(128), u0, v0, u1, v1);                     /* the picture at half strength */
  gfx_rrect_v(0, SB_H * 0.4f, SB_W, SB_H * 0.27f, 0, TINT(10, 6, 24, 0), TINT(10, 6, 24, 77));  /* the vignette rising from the bottom */
  gfx_rrect_v(0, SB_H * 0.67f, SB_W, SB_H * 0.33f + 1, 0, TINT(10, 6, 24, 77), TINT(10, 6, 24, 199));
  gfx_rrect_h(0, 0, SB_W * 0.3f, SB_H, 0, BLACK_A(82), BLACK_A(0));                      /* darker edges */
  gfx_rrect_h(SB_W * 0.7f, 0, SB_W * 0.3f, SB_H, 0, BLACK_A(0), BLACK_A(82));
  gfx_rrect_h(0, 0, SB_W * 0.55f, SB_H, 0, BLACK_A(89), BLACK_A(0));
  GLuint rt = gfx_radial_tex(); float r = SB_W * 0.4f;
  gfx_alpha_tex(rt, -r, -r, 2 * r, 2 * r, BLACK_A(102));                                 /* under the name */
  gfx_target_end(); l->ok = 1;
}
static void year_of(const char *ml, char *out, size_t cap) {
  out[0] = 0; for (const char *q = ml; *q; q++) if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9' && q[2] >= '0' && q[2] <= '9' && q[3] >= '0' && q[3] <= '9' && (q == ml || q[-1] < '0' || q[-1] > '9')) { snprintf(out, cap, "%.4s", q); return; }
}
static void scene_load(void) {
  if (!SS.n) return; SS.cur = SS.list[SS.i % SS.n]; SS.scene = SS.i++; if (tmdb_wanted(SS.cur.id)) tmdb_state(SS.cur.type, SS.cur.id, 1);
  sb_bg_mid(&SS.cur, SS.url, sizeof SS.url); SS.loading = 1; SS.nextAt = 0;
}
static void scene_start(GLuint tex, float asp) {
  SS.loading = 0; int nx = !SS.top; SsLayer *l = &SS.L[nx];
  bake(l, tex, asp); if (!l->ok) { SS.nextAt = app_now() + 0.6; return; }
  l->kb = SS.scene % 2; l->at = app_now(); tween_set(&l->a, 0); tween_to(&l->a, 1, 1.8f, EASE_IN_OUT);
  tween_to(&SS.L[SS.top].a, 0, 1.8f, EASE_IN_OUT); SS.top = nx; /* the one fading away keeps drifting */
  SS.sceneAt = app_now(); SS.nextAt = SS.sceneAt + 10.2; SS.infoWant = 1; SS.infoOut = 0; SS.logoWait = 0;
  { const TmdbLoc *TL = tmdb_wanted(SS.cur.id) ? tmdb_loc(SS.cur.type, SS.cur.id, 1) : NULL; /* in your language */
    if (TL && TL->name && TL->name[0]) snprintf(SS.cur.name, sizeof SS.cur.name, "%s", TL->name);
    if (TL && TL->logo && TL->logo[0]) snprintf(SS.logo, sizeof SS.logo, "%s", TL->logo); else if (tmdb_wanted(SS.cur.id)) SS.logo[0] = 0; else sb_logo_of(&SS.cur, SS.logo, sizeof SS.logo); }
  if (SS.logo[0]) img_prefetch(SS.logo, 460, 150);
  char y[8]; year_of(SS.cur.metaLine, y, sizeof y); const char *ty = T(!strcmp(SS.cur.type, "series") ? "Series" : "Movies");
  if (y[0]) snprintf(SS.meta, sizeof SS.meta, "%s  \xc2\xb7  %s", y, ty); else snprintf(SS.meta, sizeof SS.meta, "%s", ty);
}
static void nudge_rand(float *dx, float *dy) { *dx = (float)(rand() % 49 - 24); *dy = (float)(rand() % 37 - 18); } /* a little off its corner */
static void start(void) {
  Item all[64]; int n = board_saver_items(all, 64); if (!n) return;
  for (int i = n - 1; i > 0; i--) { int j = rand() % (i + 1); Item t = all[i]; all[i] = all[j]; all[j] = t; } /* a different order each time */
  SS.n = n < MAXSS ? n : MAXSS; memcpy(SS.list, all, SS.n * sizeof(Item));
  SS.on = 1; SS.i = 0; SS.freeAt = 0; tween_to(&SS.fade, 1, 1.1f, EASE_CSS_EASE); SS.startAt = app_now();
  tween_set(&SS.infoA, 0); tween_set(&SS.infoY, 16); SS.hh[0] = SS.mm[0] = 0; clock_update(); SS.clockAt = app_now();
  SS.shN = 0; SS.mir = SS.markR = SS.pairGone = SS.markGone = 0; SS.swapAt = 0; SS.shiftAt = app_now() + 40; tween_set(&SS.pairA, 1); tween_set(&SS.markA, 1); tween_set(&SS.pairInfoA, 1);
  SS.clockDx = SS.clockDy = SS.infoDx = SS.infoDy = SS.markDx = SS.markDy = 0; SS.flowInit = 0; SS.cVX = SS.cVY = SS.mVX = SS.mVY = 0; SS.imir = SS.mir;
  scene_load(); fprintf(stderr, "[screensaver] on\n");
}
void saver_stop(void) {
  if (dimOn) { dimOn = 0; tween_to(&dimA, 0, 0.35f, EASE_CSS_EASE); }
  if (!SS.on) return; SS.on = 0; SS.loading = 0; tween_to(&SS.fade, 0, 1.1f, EASE_CSS_EASE); tween_set(&SS.infoA, 0); SS.freeAt = app_now() + 1.2;
}
static void shift(void) {
  SS.shN++; int pair = SS.shN % 2 == 0; /* the name first, then the bottom pair, in turn */
  if (pair) { SS.pairGone = 1; SS.mir = !SS.mir; nudge_rand(&SS.clockDx, &SS.clockDy); tween_to(&SS.pairInfoA, 0, 0.9f, EASE_IN_OUT); } /* the clock's glass flows across at once; the title fades and comes back on the other side */
  else { SS.markR = !SS.markR; nudge_rand(&SS.markDx, &SS.markDy); } /* the mark's glass flows to the other corner */
  SS.swapAt = app_now() + 1.7;
}
void saver_tick(float dt) {
  double now = app_now();
  tween_tick(&SS.fade, dt); tween_tick(&dimA, dt);
  if (dimOn && (player_open() || app_idle() < 1)) { dimOn = 0; tween_to(&dimA, 0, 0.35f, EASE_CSS_EASE); }
  if (!SS.on) {
    if (SS.freeAt && now >= SS.freeAt) { SS.freeAt = 0; for (int i = 0; i < 2; i++) { gfx_target_free(&SS.L[i].t); SS.L[i].ok = 0; } } /* the pictures give their memory back */
    if (now < SS.checkAt) return; SS.checkAt = now + 10;
    double m = atof(store_get("ssDelay", "5"));
    if (m <= 0 || (player_open() && !player_paused()) || app_idle() < m * 60 || dimOn) return;
    if (!app_splash_done || SCR == SCR_LOGIN) { dimOn = 1; tween_to(&dimA, 1, 1.6f, EASE_CSS_EASE); fprintf(stderr, "[screensaver] dimmed\n"); return; } /* everywhere but the sign-in (and over a paused film) */
    start(); return; }
  if (player_open() && !player_paused()) { saver_stop(); return; } /* (a film playing again ends it) */
  for (int i = 0; i < 2; i++) tween_tick(&SS.L[i].a, dt);
  tween_tick(&SS.infoA, dt); tween_tick(&SS.infoY, dt); tween_tick(&SS.pairA, dt); tween_tick(&SS.markA, dt); tween_tick(&SS.pairInfoA, dt); timer_tick(&SS.hhNew, dt); timer_tick(&SS.mmNew, dt);
  if (now - SS.clockAt >= 15) { SS.clockAt = now; clock_update(); }
  if (now >= SS.shiftAt) { SS.shiftAt = now + 40; shift(); }
  if (SS.swapAt && now >= SS.swapAt) { SS.swapAt = 0;
    if (SS.pairGone) { SS.pairGone = 0; SS.imir = SS.mir; nudge_rand(&SS.infoDx, &SS.infoDy); tween_to(&SS.pairInfoA, 1, 0.9f, EASE_IN_OUT); } }
  if (SS.loading) { float asp; GLuint t = img_get(SS.url, SSW, SSH, &asp);
    if (t) scene_start(t, asp); else if (img_failed(SS.url)) { SS.loading = 0; SS.nextAt = now + 0.6; } }
  else if (SS.nextAt && now >= SS.nextAt) scene_load();
  /* the title comes in a moment after its picture (with its logo, or the name when it has none), leaves before the next */
  if (SS.infoWant && now - SS.sceneAt >= 0.9) {
    int ready = !SS.logo[0] || img_loaded(SS.logo) || img_failed(SS.logo); if (SS.logo[0]) img_get(SS.logo, 460, 150, NULL);
    if (ready) { SS.infoWant = 0; tween_to(&SS.infoA, 1, 0.9f, EASE_IN_OUT); tween_to(&SS.infoY, 0, 1.1f, EASE_OUT); } }
  if (!SS.infoOut && now - SS.sceneAt >= 9) { SS.infoOut = 1; SS.infoWant = 0; tween_to(&SS.infoA, 0, 0.9f, EASE_IN_OUT); tween_to(&SS.infoY, 16, 1.1f, EASE_OUT); }
}

/* ---- drawing ---- */
static void layer_draw(const SsLayer *l, float dim) {
  if (!l->ok || l->a.v <= 0.001f) return;
  float k = (float)((app_now() - l->at) / 11.0); k = clampf(k, 0, 1);
  float s = l->kb ? 1.12f - 0.08f * k : 1.04f + 0.08f * k, tx = (l->kb ? -0.01f + 0.02f * k : 0.012f - 0.024f * k) * SB_W, ty = (l->kb ? 0.008f - 0.012f * k : 0.006f - 0.012f * k) * SB_H;
  float w = SB_W * s, h = SB_H * s, x = SB_W / 2 - w / 2 + s * tx, y = SB_H / 2 - h / 2 + s * ty;
  int c = (int)(255 * (1 - dim) + 0.5f); /* the slow settling dims the picture itself: no extra full-screen layer */
  gfx_image(l->t.tex, x, y, w, h, 0, RGBA(c, c, c, (int)(255 * l->a.v + 0.5f)), 0, 1, 1, 0);
}
/* a layer's place on the screen as layer_draw draws it (its slow zoom and drift), to read its picture under a box */
static void layer_xf(const SsLayer *l, float *x, float *y, float *w, float *h) {
  float k = (float)((app_now() - l->at) / 11.0); k = clampf(k, 0, 1);
  float s = l->kb ? 1.12f - 0.08f * k : 1.04f + 0.08f * k, tx = (l->kb ? -0.01f + 0.02f * k : 0.012f - 0.024f * k) * SB_W, ty = (l->kb ? 0.008f - 0.012f * k : 0.006f - 0.012f * k) * SB_H;
  *w = SB_W * s; *h = SB_H * s; *x = SB_W / 2 - *w / 2 + s * tx; *y = SB_H / 2 - *h / 2 + s * ty; }
/* liquid glass over the scene: both pictures as they show (the one coming in over the one going), bent towards the edges */
static void ss_glass(float x, float y, float w, float h, float r, float pull, float dim) {
  for (int i = 0; i < 2; i++) { const SsLayer *l = i ? &SS.L[SS.top] : &SS.L[!SS.top]; if (!l->ok || l->a.v <= 0.001f) continue;
    float lx, ly, lw, lh; layer_xf(l, &lx, &ly, &lw, &lh);
    gfx_liquid(l->t.tex, x, y, w, h, r, pull, l->a.v, (x - lx) / lw, 1 - (y - ly) / lh, (x + w - lx) / lw, 1 - (y + h - ly) / lh); }
  gfx_rrect(x, y, w, h, r, BLACK_A((int)(255 * dim * 0.9f))); /* (the scene's own settling dim) */
  float t = (float)app_now(); lglass_light(0.02f * sinf(t * 0.13f + x * 0.01f) + 0.015f * sinf(t * 0.21f + 2) + 0.01f * sinf(t * 0.37f + 4)); lglass_shell(x, y, w, h, r, 0, 0.85f); lglass_light(0);
}
/* a pill of glass flowing to (tx, ty) on a slow spring: it stretches the way it goes and wobbles as it settles, and
   breathes a little, never quite the same, while it rests; returns its place and scale */
static void flow(float *X, float *Y, float *VX, float *VY, float tx, float ty, float dt, float *sx, float *sy, float seed) {
  const float w = 2.4f; if (dt > 0.05f) dt = 0.05f;
  *VX += (w * w * (tx - *X) - 2 * 0.62f * w * *VX) * dt; *VY += (w * w * (ty - *Y) - 2 * 0.62f * w * *VY) * dt; *X += *VX * dt; *Y += *VY * dt; /* (a little under-damped: it overshoots softly, then settles) */
  float sp = sqrtf(*VX * *VX + *VY * *VY), k = fminf(sp / 900.0f, 1), ax = fabsf(*VX) / (sp + 1), ay = fabsf(*VY) / (sp + 1), t = (float)app_now() + seed;
  *sx = 1 + 0.18f * k * ax - 0.08f * k * ay + 0.012f * sinf(t * 0.71f) + 0.008f * sinf(t * 1.13f + 1);
  *sy = 1 + 0.18f * k * ay - 0.08f * k * ax + 0.012f * sinf(t * 0.83f + 2) + 0.008f * sinf(t * 1.29f + 3);
}
void saver_draw(void) {
  if (dimA.v > 0.001f) gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(175 * dimA.v)));
  float fa = SS.fade.v; if (fa <= 0.001f) return;
  gfx_opacity_push(fa);
  const SsLayer *top = &SS.L[SS.top], *und = &SS.L[!SS.top];
  if (!(top->ok && top->a.v >= 0.999f)) gfx_rect(0, 0, SB_W, SB_H, RGB(0, 0, 0));
  float dk = clampf((float)((app_now() - SS.startAt) / 40.0), 0, 1), dim = SS.on ? 0.45f * (1 - powf(1 - dk, 2)) : 0; /* the TV settles: darker over the first 40 s */
  layer_draw(und, dim); layer_draw(top, dim);
  /* the mark: top left (or right), "Stremio" in the icon's violet, "Origin" plain and light, in a pill of glass that flows
     to the other corner when it moves */
  static float w1 = -1, w2, wc; if (w1 < 0) { w1 = text_width("Stremio ", 36, 800, 0.2f); w2 = text_width("Origin", 36, 400, 0.2f); wc = text_width(":", 88, 400, -1.5f) + 4; } /* measured once */
  float dt = app_dt(), dimA2 = dim;
  { float mw = w1 + w2 + 64, mh = 76, tx = SS.markR ? SB_W - 96 + 32 - mw - SS.markDx : 96 - 32 + SS.markDx, ty = 72 - 14 + SS.markDy;
    if (!SS.flowInit) { SS.mX = tx; SS.mY = ty; } float sx, sy; flow(&SS.mX, &SS.mY, &SS.mVX, &SS.mVY, tx, ty, dt, &sx, &sy, 11.0f);
    float ma = SS.markA.v * 0.9f; if (ma > 0.001f) { gfx_opacity_push(ma); gfx_scale_push(sx, sy, SS.mX + mw / 2, SS.mY + mh / 2);
      ss_glass(SS.mX, SS.mY, mw, mh, mh / 2, 22, dimA2); float x = SS.mX + 32, y = SS.mY + (mh - text_line_height(36)) / 2;
      text_draw("Stremio", x, y, 36, 800, RGB(0xa9, 0x8b, 0xff), 0.2f, 0, ALIGN_LEFT); text_draw("Origin", x + w1, y, 36, 400, WHITE_A(200), 0.2f, 0, ALIGN_LEFT);
      gfx_scale_pop(); gfx_opacity_pop(); } }
  /* the clock: bottom right (or left), the date under it, in a pill of glass that flows across when it moves */
  float pa = SS.pairA.v;
  static float cell; if (!cell) { for (char d = '0'; d <= '9'; d++) { char t[2] = { d, 0 }; float w = text_width(t, 88, 400, 0); if (w > cell) cell = w; } cell -= 1.5f; } /* tabular figures, measured once */
  { float tlh = text_line_height(88), dlh = text_line_height(24), wh = cell * 2, wm = cell * 2, tw = wh + wc + wm, dw = SS.date[0] ? text_width(SS.date, 24, 400, 0.3f) : 0;
    float cw = (tw > dw ? tw : dw) + 96, ch = tlh + 6 + dlh + 44, right = !SS.mir, tx = right ? SB_W - 96 + 48 - cw - SS.clockDx : 96 - 48 + SS.clockDx, ty = SB_H - 84 + 22 - ch - SS.clockDy;
    if (!SS.flowInit) { SS.cX = tx; SS.cY = ty; SS.flowInit = 1; } float sx, sy; flow(&SS.cX, &SS.cY, &SS.cVX, &SS.cVY, tx, ty, dt, &sx, &sy, 29.0f);
    if (pa > 0.001f && SS.hh[0]) { gfx_opacity_push(pa); gfx_scale_push(sx, sy, SS.cX + cw / 2, SS.cY + ch / 2);
      ss_glass(SS.cX, SS.cY, cw, ch, 44, 30, dimA2);
      float x = SS.cX + cw / 2 - tw / 2, tyy = SS.cY + 22, dy = tyy + tlh + 6;
      text_draw(SS.date, SS.cX + cw / 2, dy, 24, 400, WHITE_A(190), 0.3f, 0, ALIGN_CENTER);
      float ha = SS.hhNew.on ? timer_x(&SS.hhNew) : 1, mma = SS.mmNew.on ? timer_x(&SS.mmNew) : 1;
      if (ha < 1) gfx_opacity_push(ha); for (int k = 0; k < 2 && SS.hh[k]; k++) { char t[2] = { SS.hh[k], 0 }; text_draw(t, x + cell * k + cell / 2, tyy, 88, 400, WHITE_A(225), 0, 0, ALIGN_CENTER); } if (ha < 1) gfx_opacity_pop();
      text_draw(":", x + wh + 2, tyy - 6, 88, 400, WHITE_A(225), -1.5f, 0, ALIGN_LEFT);
      if (mma < 1) gfx_opacity_push(mma); for (int k = 0; k < 2 && SS.mm[k]; k++) { char t[2] = { SS.mm[k], 0 }; text_draw(t, x + wh + wc + cell * k + cell / 2, tyy, 88, 400, WHITE_A(225), 0, 0, ALIGN_CENTER); } if (mma < 1) gfx_opacity_pop();
      gfx_scale_pop(); gfx_opacity_pop(); } }
  /* the title: its logo or its name, and "year · type" */
  float ia = SS.infoA.v * SS.pairInfoA.v;
  if (ia > 0.001f && SS.on) { gfx_opacity_push(ia);
    int right = SS.imir; float ax = right ? SB_W - 96 - SS.infoDx : 96 + SS.infoDx; int al = right ? ALIGN_RIGHT : ALIGN_LEFT;
    float asp = 0; GLuint lt = SS.logo[0] ? img_get(SS.logo, 460, 150, &asp) : 0; float lw = 0, lh = 0, th = 0; int lines = 0;
    if (lt) { lw = 460; lh = lw / asp; if (lh > 150) { lh = 150; lw = lh * asp; } th = lh; }
    else { static int ln; static char lnName[256]; static unsigned lnLg; if (lnLg != i18n_gen() || strcmp(lnName, SS.cur.name)) { lnLg = i18n_gen(); snprintf(lnName, sizeof lnName, "%s", SS.cur.name); ln = text_wrap_count(SS.cur.name, 780, 2, 64, 800, -1.2f); } /* once per title */
      lines = ln; th = lines * 64 * 1.05f; }
    float mh = text_line_height(24), y = SB_H - 92 - SS.infoDy - mh - 18 - th + SS.infoY.v;
    if (lt) gfx_image(lt, right ? ax - lw : ax, y, lw, lh, 0, WHITE_A(255), 0, 0, 1, 1);
    else text_draw_wrapped(SS.cur.name, ax, y, 780, 2, 64, 800, WHITE_A(255), -1.2f, al, 64 * 1.05f);
    text_draw(SS.meta, ax, y + th + 18, 24, 600, WHITE_A(184), 0.4f, 780, al);
    gfx_opacity_pop(); }
  gfx_opacity_pop();
}
