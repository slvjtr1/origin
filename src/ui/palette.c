#include "ui/palette.h"
#include <math.h>
#include <string.h>

void rgb2hsl(float r, float g, float b, float *h, float *s, float *l) {
  r /= 255; g /= 255; b /= 255; float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), L = (mx + mn) / 2, d = mx - mn, H = 0, S = 0;
  if (d > 0) { S = d / (1 - fabsf(2 * L - 1)); if (mx == r) H = fmodf((g - b) / d, 6); else if (mx == g) H = (b - r) / d + 2; else H = (r - g) / d + 4; H *= 60; if (H < 0) H += 360; }
  *h = H; *s = S > 1 ? 1 : S; *l = L;
}
static float f_(float n, float h, float s, float l) { float k = fmodf(n + h / 30, 12), a = s * fminf(l, 1 - l); return l - a * fmaxf(-1, fminf(fminf(k - 3, 9 - k), 1)); }
unsigned hsl2rgb(float h, float s, float l) {
  int r = (int)(f_(0, h, s, l) * 255 + 0.5f), g = (int)(f_(8, h, s, l) * 255 + 0.5f), b = (int)(f_(4, h, s, l) * 255 + 0.5f);
  return 0xff000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
}
#define NBOX (19 * 6 * 2)
typedef struct { int n; double r, g, b; } Box;
static int pick(Swatch *sw, int ns, int maxN, float ts, float tl, float minS, float maxS, float minL, float maxL, unsigned char *used, Swatch *out) {
  int best = -1; float bs = -1;
  for (int j = 0; j < ns; j++) { Swatch *w = &sw[j]; if (used[j] || w->s < minS || w->s > maxS || w->l < minL || w->l > maxL) continue;
    float sc = (1 - fabsf(w->s - ts)) * 3 + (1 - fabsf(w->l - tl)) * 6.5f + ((float)w->n / maxN) * 2.2f; if (sc > bs) { bs = sc; best = j; } }
  if (best < 0) { out->ok = 0; return 0; }
  used[best] = 1; *out = sw[best]; out->ok = 1; return 1;
}
void palette_of(const unsigned char *px, int w, int h, int alpha, int stride, Palette *out) {
  Box box[NBOX]; memset(box, 0, sizeof box); int n = 0; if (stride < 1) stride = 1;
  for (int y = 0; y < h; y += stride) for (int x = 0; x < w; x += stride) {
    const unsigned char *p = px + ((size_t)y * w + x) * 4; if (alpha && p[3] < 200) continue;
    float H, S, L; rgb2hsl(p[0], p[1], p[2], &H, &S, &L); if (L < 0.06f || L > 0.97f) continue; n++;
    int hb = S < 0.12f ? 18 : (int)(H / 20), lb = (int)(L * 5), sb = S < 0.4f ? 0 : 1; if (hb > 18) hb = 18; if (lb > 5) lb = 5;
    Box *b = &box[(hb * 6 + lb) * 2 + sb]; b->n++; b->r += p[0]; b->g += p[1]; b->b += p[2];
  }
  Swatch sw[NBOX]; int ns = 0, maxN = 1; int minN = (int)(n * 0.006f); if (minN < 2) minN = 2;
  for (int i = 0; i < NBOX; i++) { if (box[i].n < minN) continue; Swatch *s = &sw[ns++]; rgb2hsl((float)(box[i].r / box[i].n), (float)(box[i].g / box[i].n), (float)(box[i].b / box[i].n), &s->h, &s->s, &s->l); s->n = box[i].n; if (s->n > maxN) maxN = s->n; }
  unsigned char used[NBOX]; memset(used, 0, sizeof used); memset(out, 0, sizeof *out); out->n = n;
  pick(sw, ns, maxN, 1, .5f, .35f, 1, .3f, .7f, used, &out->vibrant);
  pick(sw, ns, maxN, 1, .26f, .35f, 1, 0, .45f, used, &out->dark);
  pick(sw, ns, maxN, 1, .74f, .35f, 1, .55f, 1, used, &out->light);
  pick(sw, ns, maxN, .3f, .5f, 0, .4f, .3f, .7f, used, &out->muted);
  pick(sw, ns, maxN, .3f, .26f, 0, .4f, 0, .45f, used, &out->darkMuted);
}
