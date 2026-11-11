#include "ui/confirm.h"
#include "ui/widgets.h"
#include "core/keys.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/glass.h"
#include "core/app.h"
#include "core/hit.h"
#include "core/i18n.h"
#include "ui/sound.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define CW 760.0f
#define PAD 48.0f
#define BW 240.0f
#define BH 68.0f
#define RED RGB(0xff, 0x45, 0x3a)
static Screen card;
static SGlass glass;
static Screen *under;
static struct { int open, closing, sel, ok; char title[200], body[500], okl[80]; float h, lines; Timer1 in, out; void (*cb)(void *); void *ud; } C;
static float quint(float x) { return 1 - powf(1 - x, 5); }
static float lh(void) { return 26 * 1.45f; }

int confirm_open(void) { return C.open; }
static void finish(void) {
  C.open = 0; C.closing = 0; if (app_overlay == &card) app_overlay = under; under = NULL; sglass_free(&glass);
  void (*cb)(void *) = C.cb; void *ud = C.ud; int ok = C.ok; C.cb = NULL; if (ok && cb) cb(ud);
}
static void close_(int ok) { if (C.closing) return; C.ok = ok; C.closing = 1; timer_start(&C.out, 0.2f); }
static int key(int k) {
  if (C.closing) { finish(); return 1; }
  int rtl = app_rtl();
  if (k == K_LEFT || k == K_RIGHT) { int to = (k == K_RIGHT) != rtl ? 1 : 0; if (to != C.sel) { C.sel = to; sound_play(SND_FOCUS); } }
  else if (k == K_OK) { sound_play(SND_TOGGLE); close_(C.sel == 1); }
  else if (k == K_BACK) close_(0);
  return 1;
}
static void tick(float dt) { timer_tick(&C.in, dt); sglass_tick(&glass, dt); if (C.closing && timer_tick(&C.out, dt) >= 1) finish(); }
static void hit_btn(int a, int b) { (void)b; C.sel = a; close_(a == 1); }
static void draw(void) {
  if (!C.open) return;
  float ia = C.in.on ? quint(timer_x(&C.in)) : 1, a = ia, dy = 18 * (1 - ia), sc = 0.97f + 0.03f * ia;
  if (C.closing) { float x = timer_x(&C.out); a = 1 - x; dy = 12 * quint(x); sc = 1 - 0.03f * quint(x); }
  float x = SB_W / 2 - CW / 2, y = SB_H / 2 - C.h / 2, cx = SB_W / 2, w = CW - 2 * PAD;
  gfx_rect(0, 0, SB_W, SB_H, BLACK_A((int)(110 * a))); /* the page behind, dimmed */
  gfx_opacity_push(a); gfx_offset_push(0, dy); gfx_scale_push(sc, sc, cx, SB_H / 2);
  gfx_shadow(x, y + 30, CW, C.h, 32, 80, BLACK_A(153));
  sglass_draw(&glass, x, y, CW, C.h, 32); gfx_ring(x, y, CW, C.h, 32, 1, WHITE_A(20));
  float yy = y + PAD;
  gfx_circle(cx, yy + 34, 34, col_alpha(RED, 0.18f)); text_draw("!", cx, yy + 34 - text_line_height(40) / 2, 40, 800, RED, 0, 0, ALIGN_CENTER); yy += 68 + 20;
  text_draw(C.title, cx, yy, 36, 700, WHITE_A(255), -0.4f, w, ALIGN_CENTER); yy += text_line_height(36) + 12;
  if (C.body[0]) text_draw_wrapped(C.body, cx, yy, w, 6, 26, 400, WHITE_A(214), 0, ALIGN_CENTER, lh()); yy += C.lines * lh() + 36;
  int rtl = app_rtl(); float gap = 24, bx0 = cx - BW - gap / 2, bx1 = cx + gap / 2; /* Cancel first in reading order, OK after it */
  for (int i = 0; i < 2; i++) {
    float bx = (i == 0) != rtl ? bx0 : bx1; int f = C.sel == i; const char *l = i ? C.okl : T("Cancel");
    uint32_t bg = f ? (i ? RED : WHITE_A(235)) : WHITE_A(26), fg = f ? (i ? WHITE_A(255) : RGB(0x16, 0x14, 0x1d)) : (i ? RED : WHITE_A(230));
    gfx_rrect(bx, yy, BW, BH, BH / 2, bg); text_draw(l, bx + BW / 2, yy + BH / 2 - text_line_height(26) / 2, 26, 700, fg, 0, BW - 24, ALIGN_CENTER);
    if (!C.closing) hit_act(bx, yy, BW, BH, hit_btn, i, 0);
  }
  gfx_scale_pop(); gfx_offset_pop(); gfx_opacity_pop();
}
static Screen card = { NULL, NULL, key, tick, draw, NULL, NULL, NULL };

void confirm_show(const char *title, const char *body, const char *ok, void (*onOk)(void *), void *ud) {
  if (C.open) finish();
  memset(&C, 0, sizeof C); C.open = 1; C.cb = onOk; C.ud = ud; C.sel = 0; /* Cancel first: OK is a step away */
  snprintf(C.title, sizeof C.title, "%s", title ? title : ""); snprintf(C.body, sizeof C.body, "%s", body ? body : ""); snprintf(C.okl, sizeof C.okl, "%s", ok ? ok : "OK");
  C.lines = C.body[0] ? (float)text_wrap_count(C.body, CW - 2 * PAD, 6, 26, 400, 0) : 0;
  C.h = PAD + 68 + 20 + text_line_height(36) + 12 + C.lines * lh() + 36 + BH + PAD;
  timer_start(&C.in, 0.3f);
  sglass_bake(&glass, SB_W / 2 - CW / 2, SB_H / 2 - C.h / 2, CW, C.h);
  if (app_overlay != &card) under = app_overlay; app_overlay = &card;
}
