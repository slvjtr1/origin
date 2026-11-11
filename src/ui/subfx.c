/* The subtitles' edge and shadow: see subfx.h */
#include "ui/subfx.h"
#include "core/store.h"
#include "ui/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int lvl(const char *k, const char *d, int lo, int hi) { int v = atoi(store_get(k, d)); return v < lo || v > hi ? atoi(d) : v; }
void subfx_read(SubFx *fx, int px) {
  static const float OW[] = { 1.2f, 2.0f, 3.2f }, SA[] = { .4f, .6f, .8f, .95f }, SB[] = { 0, .06f, .11f }, SD[] = { .04f, .07f, .11f };
  memset(fx, 0, sizeof *fx); fx->ser = store_serial();
  fx->outline = strcmp(store_get("subsOutline", "false"), "false") != 0;
  fx->ow = OW[lvl("subsOutlineWidth", "2", 1, 3) - 1];
  const char *h = store_get("subsOutlineColor", "#000000"); fx->ol = 0xff000000u | (unsigned)strtoul(h[0] == '#' ? h + 1 : h, NULL, 16);
  fx->shadow = !strcmp(store_get("subsShadow", "true"), "true");
  fx->sa = SA[lvl("subsShadowStrength", "2", 1, 4) - 1];
  fx->srings = lvl("subsShadowSoft", "1", 0, 2); fx->sblur = SB[fx->srings] * px;
  int face = lvl("subsFont", "0", 0, TEXT_NFACES); fx->wt = (!strcmp(store_get("subsBold", "true"), "true") ? 800 : 600) | TEXT_FACE(face);
  float lf = (float)atoi(store_get("subsPos", "6")) / 100; fx->lift = lf < SUBFX_LIFT_MIN ? SUBFX_LIFT_MIN : lf > SUBFX_LIFT_MAX ? SUBFX_LIFT_MAX : lf;
  fx->sdist = SD[lvl("subsShadowDist", "2", 1, 3) - 1] * px;
}
/* the outline's copies: along the axes at its width, the diagonals nearer in (as the player always drew it); a thick
   one also fills the corners between */
static int ol_offsets(float w, float *ox, float *oy) {
  static const float AX[8] = { -1, 1, 0, 0, -.5f, .5f, -.5f, .5f }, AY[8] = { 0, 0, -1, 1, -.5f, .5f, .5f, -.5f };
  int n = 0; for (int i = 0; i < 8; i++) { ox[n] = AX[i] * w; oy[n] = AY[i] * w; n++; }
  if (w > 2.5f) for (int i = 4; i < 8; i++) { ox[n] = AX[i] * w * 1.42f; oy[n] = AY[i] * w * 1.42f; n++; }
  return n;
}
void subfx_paint(const SubFx *fx, uint32_t fill, SubPaint fn, void *ud) {
  float ox[12], oy[12]; int on = fx->outline ? ol_offsets(fx->ow, ox, oy) : 0;
  if (fx->shadow && fx->sa > 0) {
    /* where the copies go, round the shadow's centre: the outline's own shape when there is one, then rings out to the softness */
    float sx[40], sy[40]; int n = 0; sx[n] = sy[n] = 0; n++;
    for (int i = 0; i < on; i++) { sx[n] = ox[i]; sy[n] = oy[i]; n++; }
    if (fx->srings && fx->sblur > 0.3f) { float base = fx->outline ? fx->ow : 0;
      for (int r = 1; r <= fx->srings; r++) { float rad = base + fx->sblur * r / fx->srings; for (int k = 0; k < 8 && n < 40; k++) { float an = k * 0.7854f + (r & 1) * 0.3927f; sx[n] = cosf(an) * rad; sy[n] = sinf(an) * rad; n++; } } }
    /* each copy faint, so where they all meet (the core) the shadow is as strong as set, and it thins out to the edge */
    float a = 1 - powf(1 - fx->sa, 1.0f / n); int A = (int)(a * 255 + .5f); if (A < 2) A = 2; uint32_t sc = (uint32_t)A << 24;
    for (int i = 0; i < n; i++) fn(ud, fx->sdist + sx[i], fx->sdist + sy[i], sc);
  }
  for (int i = 0; i < on; i++) fn(ud, ox[i], oy[i], fx->ol);
  fn(ud, 0, 0, fill);
}
