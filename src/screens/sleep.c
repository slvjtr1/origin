/* The sleep timer (screens/sleep.h). The time is the TV's own clock, not the film's: paused or playing, 30 minutes are
   30 minutes. A time that ran out while the player was closed is dropped, never acted on later (one that runs out
   while the next episode loads is acted on as soon as it plays). Once asleep, the
   screen holds a moon and two lines that dim after a few seconds and drift slowly (an OLED panel keeps no mark of
   them); two minutes on, the TV is asked to turn its screen off (a TV that refuses keeps the dark screen). */
#include "screens/sleep.h"
#include "core/app.h"
#include "core/i18n.h"
#include "core/keys.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/icon.h"
#include "ui/glass.h"
#include "ui/widgets.h"
#include "platform/plat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int MINS[] = { 0, 15, 30, 45, 60, 90, -1 }; /* -1: when what plays ends */
#define NMINS ((int)(sizeof MINS / sizeof *MINS))
#define WARN_S 60.0     /* the countdown pill: the last minute */
#define MORE_S (15 * 60) /* OK on it: this much more */
#define OFF_AFTER 120.0 /* asleep this long: the screen off */
#define SCREEN_OFF "luna://com.webos.service.tvpower/power/turnOffScreen"
#define SCREEN_ON "luna://com.webos.service.tvpower/power/turnOnScreen"

static struct { int idx; double until; int asleep, offAsked, gone; double asleepAt, warnAt; float wa, sa, ta; } S; /* gone: the player closed since it was set; wa / sa / ta: the pill, the dark screen, its words (0..1, eased) */

int sleep_count(int withEnd) { return withEnd ? NMINS : NMINS - 1; }
int sleep_index(void) { return S.idx; }
int sleep_armed(void) { return S.idx > 0; }
int sleep_asleep(void) { return S.asleep; }
static double left_s(void) { return S.idx > 0 && MINS[S.idx] > 0 ? S.until - app_now() : -1; }

void sleep_cycle(int withEnd) {
  int n = sleep_count(withEnd); S.idx = (S.idx + 1) % n; S.warnAt = 0; S.gone = 0;
  S.until = MINS[S.idx] > 0 ? app_now() + MINS[S.idx] * 60.0 : 0;
  if (MINS[S.idx] > 0) fprintf(stderr, "[sleep] in %d min\n", MINS[S.idx]); else fprintf(stderr, "[sleep] %s\n", S.idx ? "when this ends" : "off");
}
void sleep_label(char *out, size_t cap) {
  if (!S.idx) { snprintf(out, cap, "%s", T("Sleep timer off")); return; }
  if (MINS[S.idx] < 0) { snprintf(out, cap, "%s", T("Sleep when this ends")); return; }
  double l = left_s(); int m = (int)ceil((l > 0 ? l : 0) / 60.0); if (m < 1) m = 1; char n[12]; snprintf(n, sizeof n, "%d", m);
  snprintf(out, cap, "%s", T1("Sleep in {n} min", "n", n));
}

static void fall_asleep(const char *why) { S.asleep = 1; S.asleepAt = app_now(); S.offAsked = 0; S.idx = 0; S.until = 0; S.warnAt = 0; S.ta = 1; fprintf(stderr, "[sleep] asleep (%s)\n", why); }
int sleep_tick(int atEnd) {
  double now = app_now();
  if (S.asleep) {
#ifdef SB_WEBOS
    if (!S.offAsked && now - S.asleepAt >= OFF_AFTER) { S.offAsked = 1; plat_luna_send(SCREEN_OFF, "{}"); fprintf(stderr, "[sleep] screen off asked\n"); }
#endif
    return 0; }
  if (!S.idx) return 0;
  if (MINS[S.idx] < 0) { if (atEnd) { fall_asleep("the end"); return 1; } return 0; }
  if (S.gone) { if (now >= S.until) { fprintf(stderr, "[sleep] its time passed while the player was closed: dropped\n"); S.idx = 0; S.until = 0; S.gone = 0; return 0; } S.gone = 0; } /* (back before its time: it goes on) */
  if (now >= S.until) { fall_asleep("the time"); return 1; }
  if (S.until - now <= WARN_S && !S.warnAt) S.warnAt = now;
  return 0;
}
int sleep_key(int k) {
  if (S.asleep) { S.asleep = 0; fprintf(stderr, "[sleep] woken\n");
#ifdef SB_WEBOS
    if (S.offAsked) plat_luna_send(SCREEN_ON, "{}");
#endif
    return 1; }
  if (k == K_OK && S.warnAt && left_s() > 0 && left_s() <= WARN_S) { S.until = app_now() + MORE_S; S.warnAt = 0; fprintf(stderr, "[sleep] 15 minutes more\n"); return 1; }
  return 0;
}
void sleep_player_closed(void) {
  if (S.asleep) { S.asleep = 0;
#ifdef SB_WEBOS
    if (S.offAsked) plat_luna_send(SCREEN_ON, "{}");
#endif
  }
  if (S.idx && MINS[S.idx] < 0) S.idx = 0; /* (when this ends: this, not whatever plays next time) */
  if (S.idx) S.gone = 1;
  S.warnAt = 0; S.wa = S.sa = 0;
}

static float ease(float k) { k = k < 0 ? 0 : k > 1 ? 1 : k; return k * k * (3 - 2 * k); }
static void toward(float *v, float want, float rate) { float dt = app_dt(); *v += (want - *v) * fminf(1, dt * rate); if (fabsf(want - *v) < 0.002f) *v = want; }
/* a thin ring of beads from the top clockwise, k of the way round, over a faint track */
static void ring(float cx, float cy, float r, float k, uint32_t c) {
  int n = (int)(2 * 3.14159f * r / 4.2f); for (int i = 0; i < n; i++) { float f = (float)i / n, a = -1.5708f + 6.28318f * f; gfx_circle(cx + r * cosf(a), cy + r * sinf(a), 1.7f, f <= k ? c : col_alpha(c, 0.18f)); } }

static void draw_pill(void) {
  double l = left_s(); int on = S.warnAt && l > 0 && l <= WARN_S && !S.asleep; toward(&S.wa, on ? 1 : 0, on ? 7 : 10); if (S.wa <= 0.002f) return;
  if (l < 0) l = 0; int s = (int)ceil(l); char t[48], n[16]; snprintf(n, sizeof n, "%d:%02d", s / 60, s % 60); snprintf(t, sizeof t, "%s", T1("Sleeping in {t}", "t", n));
  const char *more = T("15 more minutes"); float k = ease(S.wa), tw = text_width(t, 26, 700, 0), mw = text_width(more, 20, 600, 0) + 64;
  float w = 22 + 60 + 20 + fmaxf(tw, mw) + 34, h = 92, x = roundf(SB_W / 2 - w / 2), y = 44 - 14 * (1 - k);
  gfx_opacity_push(k);
  gfx_shadow(x, y + 8, w, h, h / 2, 34, BLACK_A(110)); lglass_shell(x, y, w, h, h / 2, RGBA(16, 16, 24, 190), 0.9f);
  float cx = x + 22 + 30, cy = y + h / 2; gfx_circle(cx, cy, 30, WHITE_A(16)); ring(cx, cy, 30, (float)(l / WARN_S), ACC3);
  { float br = 0.5f + 0.5f * sinf((float)app_now() * 2.2f); icon_draw("p_sleep", cx - 15, cy - 15, 30, col_alpha(WHITE_A(255), 0.8f + 0.2f * br)); } /* (the moon breathing, slowly) */
  float tx = x + 22 + 60 + 20; text_draw(t, tx, y + 14, 26, 700, WHITE_A(245), 0, 0, ALIGN_LEFT);
  key_hint(tx, y + 66, K_OK, more, WHITE_A(165), 0);
  gfx_opacity_pop();
}
static void draw_dark(void) {
  toward(&S.sa, S.asleep ? 1 : 0, S.asleep ? 1.6f : 9); if (S.sa <= 0.002f) return;
  double t = app_now() - S.asleepAt; toward(&S.ta, S.asleep && t > 8 ? 0.3f : 1, 0.8f); /* (the words dim once read) */
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(255 * ease(S.sa))));
  float dx = 46 * sinf((float)t / 23.0f), dy = 30 * sinf((float)t / 31.0f + 1.1f), cx = SB_W / 2 + dx, cy = SB_H / 2 - 40 + dy; /* (drifting: no part of the panel lit for long) */
  float a = ease(S.sa) * S.ta; gfx_opacity_push(a);
  gfx_circle(cx, cy - 64, 70, col_alpha(ACC3, 0.08f)); gfx_circle(cx, cy - 64, 46, col_alpha(ACC3, 0.1f));
  icon_draw("p_sleep", cx - 26, cy - 90, 52, WHITE_A(235));
  text_draw(T("Paused for the night"), cx, cy + 6, 36, 700, WHITE_A(240), 0, 0, ALIGN_CENTER);
  text_draw(T("Your place is kept. Press any key to come back"), cx, cy + 62, 22, 500, WHITE_A(150), 0, 0, ALIGN_CENTER);
  gfx_opacity_pop();
}
void sleep_draw(void) {
  { static int dm = -1; if (dm < 0) { const char *e = getenv("SB_SLEEP_DEMO"); dm = e ? (!strcmp(e, "dark") ? 2 : 1) : 0; /* (development: SB_SLEEP_DEMO=warn|dark shows the countdown or the dark screen at once, for pictures of them) */
      if (dm == 1) { S.idx = 1; S.until = app_now() + 42; S.warnAt = app_now(); } else if (dm == 2) fall_asleep("SB_SLEEP_DEMO"); } }
  draw_pill(); draw_dark(); }
