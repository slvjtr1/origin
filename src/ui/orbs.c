#include "ui/orbs.h"
#include "ui/gfx.h"
#include "core/app.h"
#include <math.h>
#include <string.h>

#define TAU 6.2831853f
static const float BASE_W[2] = { 2100, 2000 }, ASPECT = 0.76f, OP[2] = { 0.45f, 0.36f };
/* where a glow rests at time t: round its corner on a wide, flat loop, and along the two edges from it (a vignette
   that breathes), turned a little */
static void orbit(int i, double t, int layout, float *x, float *y, float *deg) {
  static const float C[2][2][2] = { { { -60, -40 }, { 1980, 1120 } }, { { 1980, -40 }, { -60, 1120 } } }; /* [layout][glow] corner */
  float sx = C[layout][i][0] < 960 ? 1 : -1, sy = C[layout][i][1] < 540 ? 1 : -1; /* towards the screen */
  float ph = i * 2.1f + (float)t * TAU / 16.0f, r = 300 + 110 * sinf((float)t * TAU / 13.0f + i);
  float along = 340 * sinf((float)t * TAU / 19.0f + i * 1.7f), down = 200 * sinf((float)t * TAU / 17.0f + i * 0.6f);
  *x = C[layout][i][0] + r * cosf(ph) * 1.25f + sx * (120 + along);
  *y = C[layout][i][1] + r * sinf(ph) * 0.8f + sy * (60 + down);
  *deg = -20 + 18 * sinf((float)t * TAU / 25.0f + i * 1.3f);
}
static void target(const Orbs *o, int i, int layout, float spread, float *tg) {
  float x, y, d; orbit(i, o->t, layout, &x, &y, &d); if (spread < 1) spread = 1;
  tg[0] = 960 + (x - 960) / spread; tg[1] = 540 + (y - 540) / spread; tg[2] = BASE_W[i] * (1 + 0.125f * (spread - 1)); tg[3] = d;
}
void orbs_place(Orbs *o, int layout, float spread) {
  for (int i = 0; i < 2; i++) { target(o, i, layout, spread, o->p[i]); memset(o->v[i], 0, sizeof o->v[i]); } o->placed = 1;
}
void orbs_step(Orbs *o, float dt, int layout, float spread, float om) {
  if (dt > 0.05f) dt = 0.05f; if (!o->placed) orbs_place(o, layout, spread);
  o->t += dt;
  for (int i = 0; i < 2; i++) { float tg[4]; target(o, i, layout, spread, tg);
    for (int c = 0; c < 4; c++) { float acc = om * om * (tg[c] - o->p[i][c]) - 2 * om * o->v[i][c]; o->v[i][c] += acc * dt; o->p[i][c] += o->v[i][c] * dt; } /* a critically damped spring */
    o->tint[i] *= expf(-2.4f * dt); }
}
void orbs_draw(const Orbs *o, float a) { orbs_draw_col(o, a, 0, 0); }
void orbs_draw_col(const Orbs *o, float a, uint32_t c0, uint32_t c1) {
  if (a <= 0.001f) return;
  for (int i = 0; i < 2; i++) { float w = o->p[i][2], h = w * ASPECT;
    uint32_t c = c0 ? (i == 0 ? c0 : c1) | 0xff000000u : i == 0 ? col_mix(ACC | 0xff000000u, 0xffffffffu, 0.12f) : ACC | 0xff000000u; /* the top one a touch lighter */
    if (o->tint[i] > 0.01f) c = col_mix(c, o->tintC[i] | 0xff000000u, 0.45f * o->tint[i]);
    gfx_alpha_tex_rot(gfx_radial_tex(), o->p[i][0] - w / 2, o->p[i][1] - h / 2, w, h, o->p[i][3], col_alpha(c, OP[i] * (1 + 0.35f * o->tint[i]) * a), 0, 0, 1, 1); }
}
void orbs_draw_in(const Orbs *o, float a, float x, float y, float w, float h, float r) {
  if (a <= 0.001f) return; float sx = w / 1920, sy = h / 1080;
  for (int i = 0; i < 2; i++) { float gw = o->p[i][2], gh = gw * ASPECT;
    uint32_t c = i == 0 ? col_mix(ACC | 0xff000000u, 0xffffffffu, 0.12f) : ACC | 0xff000000u;
    if (o->tint[i] > 0.01f) c = col_mix(c, o->tintC[i] | 0xff000000u, 0.45f * o->tint[i]);
    gfx_glow_round(x + (o->p[i][0] - gw / 2) * sx, y + (o->p[i][1] - gh / 2) * sy, gw * sx, gh * sy, col_alpha(c, OP[i] * (1 + 0.35f * o->tint[i]) * a), x, y, w, h, r); }
}
/* the presses: velocity given to the springs (they come back by themselves) */
void orbs_push(Orbs *o, int kind, float s) {
  s *= 2.8f; /* (felt plainly: a push moves them a few hundred units before the springs bring them back) */
  for (int i = 0; i < 2; i++) { float *v = o->v[i], *p = o->p[i], inx = 960 - p[0], iny = 540 - p[1];
    switch (kind) {
      case ORB_LEFT: v[0] -= 520 * s; v[3] -= 10 * s; break;
      case ORB_RIGHT: v[0] += 520 * s; v[3] += 10 * s; break;
      case ORB_UP: v[1] -= 420 * s; break;
      case ORB_DOWN: v[1] += 420 * s; break;
      case ORB_OK: v[0] += inx * 1.1f * s; v[1] += iny * 1.1f * s; v[2] -= 500 * s; break;      /* drawn in, a little smaller: then let go */
      case ORB_BACK: v[0] -= inx * 0.8f * s; v[1] -= iny * 0.8f * s; v[2] += 700 * s; break;    /* breathed out */
      case ORB_TAB_L: v[0] -= 1400 * s; v[3] -= 40 * s; v[2] += 300 * s; break;
      case ORB_TAB_R: v[0] += 1400 * s; v[3] += 40 * s; v[2] += 300 * s; break;
      case ORB_HOLD: v[0] += inx * 0.6f * s; v[1] += iny * 0.6f * s; v[2] -= 300 * s; break;   /* gathered */
      case ORB_DONE: v[0] += inx * 1.4f * s; v[1] += iny * 1.4f * s; v[2] += 800 * s; break;
      case ORB_PHONE: v[0] += inx * 2.4f * s; v[1] += iny * 2.4f * s; v[2] += 600 * s; break;
      case ORB_RED: case ORB_GREEN: { int mine = (kind == ORB_RED) == (i == 0); /* the glow of the key's own corner surges, in its colour */
        float k = mine ? 1.6f : 0.5f; v[0] += inx * k * s; v[1] += iny * k * s; v[2] += (mine ? 900 : 200) * s;
        if (mine) { o->tint[i] = fminf(1, o->tint[i] + s); o->tintC[i] = kind == ORB_RED ? RGB(0xff, 0x5a, 0x5a) : RGB(0x3c, 0xe0, 0x84); } } break;
    } }
}
static Orbs appOrbs;
Orbs *orbs_app(void) { return &appOrbs; }
void orbs_pulse(int kind, float s) { orbs_push(&appOrbs, kind, s); }
