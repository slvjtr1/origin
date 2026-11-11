#include "ui/qr.h"
#include "ui/gfx.h"
#include "qrcodegen.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

/* drawn soft: each module is a square whose free corners (no neighbour on either side) are rounded off, so lone
   modules are dots and runs are pills; the three eyes are a rounded ring around a rounded square. Rasterised once,
   QM pixels a module, with smooth edges. */
#define QM 10
static int on(const uint8_t *qr, int n, int x, int y) { return x >= 0 && y >= 0 && x < n && y < n && qrcodegen_getModule(qr, x, y); }
static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float rbox(float px, float py, float cx, float cy, float h, float r) { /* signed distance to a rounded square */
  float qx = fabsf(px - cx) - (h - r), qy = fabsf(py - cy) - (h - r), ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, m = qx > qy ? qx : qy;
  return sqrtf(ox * ox + oy * oy) + (m < 0 ? m : 0) - r;
}
static float cover(const uint8_t *qr, int n, float u, float v) {
  float aa = 1.0f / QM;
  for (int e = 0; e < 3; e++) { int ex = e == 1 ? n - 7 : 0, ey = e == 2 ? n - 7 : 0;
    if (u >= ex && u < ex + 7 && v >= ey && v < ey + 7) { float cx = ex + 3.5f, cy = ey + 3.5f;
      float ring = fmaxf(rbox(u, v, cx, cy, 3.5f, 2.0f), -rbox(u, v, cx, cy, 2.5f, 1.2f)), dot = rbox(u, v, cx, cy, 1.5f, 0.75f);
      return clamp01(0.5f - fminf(ring, dot) / aa); } }
  int mx = (int)u, my = (int)v; if (!on(qr, n, mx, my)) return 0;
  float fx = u - mx, fy = v - my; int dx = fx < 0.5f ? -1 : 1, dy = fy < 0.5f ? -1 : 1;
  if (on(qr, n, mx + dx, my) || on(qr, n, mx, my + dy)) return 1;
  float d = sqrtf((fx - 0.5f) * (fx - 0.5f) + (fy - 0.5f) * (fy - 0.5f)) - 0.5f; /* the free corner: a quarter of a circle */
  return clamp01(0.5f - d / aa);
}
typedef struct { char text[512]; GLuint tex; int n; } QrCached;
static QrCached cache[4]; static int next;
static QrCached *qr_get(const char *text) {
  for (int i = 0; i < 4; i++) if (cache[i].tex && !strcmp(cache[i].text, text)) return &cache[i];
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
  if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return NULL;
  int n = qrcodegen_getSize(qr), sz = n * QM; unsigned char *px = malloc((size_t)sz * sz);
  for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) px[y * sz + x] = (unsigned char)(cover(qr, n, (x + 0.5f) / QM, (y + 0.5f) / QM) * 255 + 0.5f);
  QrCached *c = &cache[next]; next = (next + 1) % 4; if (c->tex) gfx_tex_free(c->tex);
  c->tex = gfx_tex_create(px, sz, sz, 1, 1); c->n = n; snprintf(c->text, sizeof c->text, "%s", text); free(px); return c;
}
int qr_draw(const char *text, float x, float y, float size, float radius, float pad) {
  QrCached *c = qr_get(text); if (!c) return 0;
  gfx_rrect(x, y, size, size, radius, WHITE_A(255));
  float inner = size - 2 * pad, cell = inner / c->n; (void)cell;
  gfx_alpha_tex(c->tex, x + pad, y + pad, inner, inner, RGB(0, 0, 0));
  return 1;
}
