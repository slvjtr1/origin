/* see usbprep.h */
#include "ui/usbprep.h"
#include "ui/widgets.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "ui/icon.h"
#include "ui/sound.h"
#include "core/app.h"
#include "core/keys.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "core/store.h"
#include "core/usbcheck.h"
#include "platform/plat.h"
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CW 820.0f
#define PAD 52.0f
#define ROW_H 58.0f
#define BW 240.0f
#define BH 68.0f
#define GIVE_UP_S 12.0   /* the card waits this long for the check, then leaves the drive off (see below) */

enum { PR_NONE, PR_FAST, PR_OKAY, PR_SLOW, PR_FAIL };
/* The check runs on a thread of its own (core/usbcheck.c does the work). The main thread owns the card and the store;
   the thread only reports through the job, under the lock. At most one check thread lives at a time, an abandoned one
   included: a drive that stops answering can hold its thread inside the kernel (a write cannot be called back), and no
   second check starts until it has come out. The 12 s deadline is the card's: it gives up waiting and says so, it asks
   the thread to stop at its next step, and it cannot promise the thread has stopped by then. */
typedef struct { UsbCheck c; char id[96]; unsigned gen; int dropped, exited; } Job; /* dropped: the main thread let go (the thread frees it); exited: the thread let go (the main thread frees it). Both under mu */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static Job *job;         /* the check the card waits on (main thread) */
static int alive;        /* check threads still running, waited on or abandoned (under mu) */
static unsigned gen;     /* each card's own number: an answer for an earlier one is never used */

static void *check(void *a) {
  Job *j = a; plat_thread_background(5);
  usbcheck_run(&j->c, &mu);
  pthread_mutex_lock(&mu); j->exited = 1; int drop = j->dropped; alive--; pthread_mutex_unlock(&mu);
  if (drop) free(j);
  return NULL;
}
static void job_drop(void) { /* the main thread lets go of it: asked to stop; the thread frees it when it comes out */
  if (!job) return; usbcheck_cancel(&job->c);
  pthread_mutex_lock(&mu); int gone = job->exited; if (!gone) job->dropped = 1; pthread_mutex_unlock(&mu);
  if (gone) free(job); job = NULL;
}

static Screen card; static SGlass glass; static Screen *under;
static struct {
  int open, closing, shown;         /* shown: the card is up (Back or OK hides it, the check goes on) */
  char id[96], mnt[512];            /* the drive it is for */
  int stage, res, failRow; float mbs; /* as last read from the job */
  double t0, stageAt[4], doneAt;    /* app_now() when it began, when each stage was reached, when the result came */
  char key[128], name[96], facts[160], head[160], sub[200];
  Timer1 in, out; float h;
} U;
static float quint(float x) { return 1 - powf(1 - x, 5); }
static float card_h(void) { return PAD + 76 + 22 + text_line_height(36) + 8 + text_line_height(24) + 30 + 3 * ROW_H + 26 + text_line_height(28) + 6 + text_line_height(24) + 30 + BH + PAD; }

int usbprep_busy(void) { pthread_mutex_lock(&mu); int a = alive; pthread_mutex_unlock(&mu); return job != NULL || a > 0; }
static void hide(void) { if (!U.shown || U.closing) return; U.closing = 1; timer_start(&U.out, 0.2f); }
static void hidden(void) { U.shown = 0; U.closing = 0; if (app_overlay == &card) app_overlay = under; under = NULL; sglass_free(&glass); if (!job) U.open = 0; }

/* the result, applied once: the drive's switch in the store (usb_pump sees it within a second) */
static void result(void) {
  char v[32]; snprintf(v, sizeof v, U.mbs >= 10 ? "%.0f" : "%.1f", U.mbs);
  switch (U.res) {
    case PR_FAST: snprintf(U.head, sizeof U.head, "%s", T("Fast drive: torrents now buffer on it")); snprintf(U.sub, sizeof U.sub, "%s", T1("{n} MB/s. Seeks are quicker and the TV keeps its memory free.", "n", v)); break;
    case PR_OKAY:   snprintf(U.head, sizeof U.head, "%s", T("Ready: torrents now buffer on it")); snprintf(U.sub, sizeof U.sub, "%s", T1("{n} MB/s. An SSD would be smoother for 4K films.", "n", v)); break;
    case PR_SLOW: snprintf(U.head, sizeof U.head, "%s", T("This drive is too slow for torrents")); snprintf(U.sub, sizeof U.sub, "%s", T1("{n} MB/s. Left off: an SSD is recommended.", "n", v)); break;
    default:     snprintf(U.head, sizeof U.head, "%s", T("This drive cannot be used")); snprintf(U.sub, sizeof U.sub, "%s", T("It could not be written to. Left off.")); break; }
  int on = U.res == PR_FAST || U.res == PR_OKAY;
  if (U.key[0]) {
    UsbDrive d; int there = usb_drive(&d);
    if (usbcheck_keep_answer(U.id, U.mnt, there, there ? d.id : NULL, there ? d.mnt : NULL, store_get(U.key, NULL))) store_set(U.key, on ? "true" : "false"); }
  U.doneAt = app_now();
  if (!U.shown) { toast(U.head, 3200, NULL); U.open = 0; } else if (on) sound_play(SND_TOGGLE);
}
void usbprep_pump(void) {
  if (!job) return;
  { UsbDrive d; if (!usb_drive(&d) || strcmp(d.id, U.id) || strcmp(d.mnt, U.mnt)) { fprintf(stderr, "[usb] the drive went or changed during its check: stopped\n"); usbprep_gone(); return; } } /* (another drive in its place: never answered for this one) */
  pthread_mutex_lock(&mu); int st = job->c.stage, res = job->c.res, done = job->c.done, fr = job->c.failRow; float mbs = job->c.mbs; unsigned g = job->gen; pthread_mutex_unlock(&mu);
  double t = app_now();
  while (U.stage < st && U.stage < 3) { U.stage++; U.stageAt[U.stage] = t; }
  if (done) { job_drop(); if (g != gen) return; U.res = res == UC_FAST ? PR_FAST : res == UC_OKAY ? PR_OKAY : res == UC_SLOW ? PR_SLOW : PR_FAIL; U.mbs = mbs; U.failRow = fr; result(); return; }
  if (t - U.t0 > GIVE_UP_S) { fprintf(stderr, "[usb] the check gave no answer in %.0f s: the drive is left off (the check is told to stop; a drive stuck inside the system may keep its thread until it answers)\n", GIVE_UP_S);
    U.failRow = U.stage >= 2 ? 2 : 1; U.stage = 3; U.stageAt[3] = t; U.res = PR_FAIL; U.mbs = 0; job_drop(); result(); }
}
void usbprep_gone(void) {
  if (!U.open && !job) return;
  job_drop(); gen++; if (U.shown) hide(); else U.open = 0;
}

static int key(int k) {
  if (U.closing) { hidden(); return 1; }
  if (k == K_BACK || k == K_OK) { if (k == K_OK) sound_play(SND_TOGGLE); hide(); } /* (OK presses the one button there is: Hide while it checks, OK after) */
  return 1; /* nothing under the card takes keys while it is up */
}
static void tick(float dt) {
  timer_tick(&U.in, dt); sglass_tick(&glass, dt);
  if (U.closing) { if (timer_tick(&U.out, dt) >= 1) hidden(); return; }
  if (U.shown && U.res != PR_NONE && app_now() - U.doneAt > 6) hide(); /* read by then: it goes by itself */
}
static void hit_ok(int a, int b) { (void)a; (void)b; hide(); }
static float fade(double since, float dur) { float x = (float)((app_now() - since) / dur); return x < 0 ? 0 : x > 1 ? 1 : x; }
static void spinner(float cx, float cy) {
  gfx_ring(cx - 13, cy - 13, 26, 26, 13, 3, col_alpha(ACC, 0.25f)); float ph = (float)fmod(app_now() / 0.8, 1) * 6.2832f;
  for (int i = 0; i < 8; i++) { float an = ph - 2.36f + i * 0.24f; gfx_circle(cx + cosf(an) * 11.5f, cy + sinf(an) * 11.5f, 1.6f, ACC2); }
}
static void draw(void) {
  if (!U.shown) return;
  float ia = U.in.on ? quint(timer_x(&U.in)) : 1, a = ia, dy = 18 * (1 - ia), sc = 0.97f + 0.03f * ia;
  if (U.closing) { float x = timer_x(&U.out); a = 1 - x; dy = 12 * quint(x); sc = 1 - 0.03f * quint(x); }
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - U.h / 2, cx = SB_W / 2, w = CW - 2 * PAD;
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(110 * a)));
  gfx_opacity_push(a); gfx_offset_push(0, dy); gfx_scale_push(sc, sc, cx, SB_H / 2);
  gfx_shadow(x, y + 30, CW, U.h, 32, 80, BLACK_A(153));
  sglass_draw(&glass, x, y, CW, U.h, 32); gfx_ring(x, y, CW, U.h, 32, 1, WHITE_A(20));
  float yy = y + PAD;
  gfx_circle(cx, yy + 38, 38, col_alpha(ACC, 0.18f)); icon_draw("g_usb", cx - 21, yy + 38 - 21, 42, ACC2); yy += 76 + 22;
  text_draw(U.name, cx, yy, 36, 700, WHITE_A(255), -0.4f, w, ALIGN_CENTER); yy += text_line_height(36) + 8;
  text_draw(U.facts, cx, yy, 24, 400, WHITE_A(150), 0, w, ALIGN_CENTER); yy += text_line_height(24) + 30;
  /* the three steps: waiting (a faint ring), under way (the spinner), done (a tick that fades in) */
  char sp[96]; const char *lb[3] = { T("Drive found"), T("Checking it can be written"), T("Measuring its speed") };
  if (U.res != PR_NONE && U.res != PR_FAIL) { char v[32]; snprintf(v, sizeof v, U.mbs >= 10 ? "%.0f" : "%.1f", U.mbs); snprintf(sp, sizeof sp, "%s", T1("Speed: {n} MB/s", "n", v)); lb[2] = sp; }
  float rx = x + PAD + 40, tx = rx + 34;
  for (int i = 0; i < 3; i++) { float cy = yy + ROW_H / 2; int failed = U.res == PR_FAIL && i == U.failRow;
    int state = U.stage > i ? 2 : U.stage == i ? 1 : 0; if (U.res == PR_FAIL && i > U.failRow) state = 0; /* (never reached) */
    if (i) gfx_rect(x + PAD + 24, yy, w - 48, 1, WHITE_A(14));
    if (state == 2 && failed) { gfx_circle(rx, cy, 13, col_alpha(RGB(0xff, 0x45, 0x3a), 0.9f)); text_draw("!", rx, cy - text_line_height(20) / 2, 20, 800, WHITE_A(255), 0, 0, ALIGN_CENTER); }
    else if (state == 2) { float k = fade(U.stageAt[i + 1 > 3 ? 3 : i + 1], 0.25f); gfx_circle(rx, cy, 13, col_alpha(ACC, 0.9f * k)); icon_draw("check", rx - 9, cy - 9, 18, col_alpha(ACC_FG, k)); }
    else if (state == 1) spinner(rx, cy);
    else gfx_ring(rx - 13, cy - 13, 26, 26, 13, 2, WHITE_A(40));
    text_draw(lb[i], tx, cy - text_line_height(27) / 2, 27, state == 1 ? 600 : 400, state == 0 ? WHITE_A(110) : WHITE_A(235), 0, w - 120, ALIGN_LEFT);
    yy += ROW_H; }
  yy += 26;
  if (U.res != PR_NONE) { float k = fade(U.doneAt, 0.3f); gfx_opacity_push(k);
    text_draw(U.head, cx, yy, 28, 700, U.res == PR_SLOW || U.res == PR_FAIL ? RGB(0xff, 0xb4, 0x54) : WHITE_A(255), -0.2f, w, ALIGN_CENTER);
    text_draw(U.sub, cx, yy + text_line_height(28) + 6, 24, 400, WHITE_A(180), 0, w, ALIGN_CENTER); gfx_opacity_pop(); }
  else text_draw(T("A few seconds, once per drive"), cx, yy + (text_line_height(28) + 6) / 2, 24, 400, WHITE_A(120), 0, w, ALIGN_CENTER);
  yy += text_line_height(28) + 6 + text_line_height(24) + 30;
  { int ready = U.res != PR_NONE; float bx = cx - BW / 2;
    gfx_rrect(bx, yy, BW, BH, BH / 2, ready ? WHITE_A(235) : WHITE_A(20));
    text_draw(ready ? T("OK") : T("Hide"), cx, yy + BH / 2 - text_line_height(26) / 2, 26, 700, ready ? RGB(0x16, 0x14, 0x1d) : WHITE_A(150), 0, BW - 24, ALIGN_CENTER);
    if (!U.closing) hit_act(bx, yy, BW, BH, hit_ok, 0, 0); }
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
static Screen card = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };

int usbprep_show(const UsbDrive *d, const char *storeKey) {
  if (!d || usbprep_busy()) return 0; /* (an earlier check, even one given up on, is still out: asked again later) */
  Job *j = calloc(1, sizeof *j); if (!j) return 0;
  { char ed[600]; usb_engine_dir(d->mnt, ed, sizeof ed); usbcheck_init(&j->c, d->mnt, ed); } snprintf(j->id, sizeof j->id, "%s", d->id); j->gen = ++gen;
  if (U.shown) sglass_free(&glass); /* an earlier drive's result still up: this one takes the card */
  if (app_overlay == &card) { app_overlay = under; under = NULL; }
  memset(&U, 0, sizeof U); U.open = 1; U.shown = 1; U.t0 = U.stageAt[0] = app_now();
  snprintf(U.id, sizeof U.id, "%s", d->id); snprintf(U.mnt, sizeof U.mnt, "%s", d->mnt);
  snprintf(U.key, sizeof U.key, "%s", storeKey ? storeKey : "");
  snprintf(U.name, sizeof U.name, "%s", d->name[0] ? d->name : T("USB drive"));
  { char gb[16]; snprintf(gb, sizeof gb, "%.*f", d->freeMb >= 10240 ? 0 : 1, d->freeMb / 1024.0);
    char fs[24]; snprintf(fs, sizeof fs, "%s", d->fs); for (char *p = fs; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    snprintf(U.facts, sizeof U.facts, "%s%s%s", T1("{n} GB free", "n", gb), fs[0] ? " \xc2\xb7 " : "", fs); }
  U.h = card_h();
  pthread_mutex_lock(&mu); alive++; pthread_mutex_unlock(&mu);
  job = j; pthread_t th;
  if (pthread_create(&th, NULL, check, j)) { pthread_mutex_lock(&mu); alive--; pthread_mutex_unlock(&mu); job = NULL; free(j); U.open = U.shown = 0; fprintf(stderr, "[usb] no thread for the check: asked again later\n"); return 0; }
  pthread_detach(th);
  timer_start(&U.in, 0.3f);
  sglass_bake(&glass, SB_W / 2 - CW / 2, SB_H / 2 - U.h / 2, CW, U.h);
  if (app_overlay != &card) under = app_overlay; app_overlay = &card;
  fprintf(stderr, "[usb] new drive %s: checking it (the card is up)\n", d->mnt);
  return 1;
}
