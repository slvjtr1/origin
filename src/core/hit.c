#include "core/hit.h"
#include "ui/gfx.h"
#include <string.h>
#define MAXHIT 512
static Hit hits[MAXHIT]; static int n;
/* rects are stored in screen space: the current offset/scale/clip apply as for drawing */
void gfx_xform_point(float *x, float *y);
void hit_begin(void) { n = 0; }
int hit_count(void) { return n; }
void hit_trim(int to) { if (to >= 0 && to < n) n = to; }
static void add(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b, int shield, int nav) {
  if (n >= MAXHIT) return; float x0 = x, y0 = y, x1 = x + w, y1 = y + h; gfx_xform_point(&x0, &y0); gfx_xform_point(&x1, &y1);
  Hit *t = &hits[n++]; t->x = x0; t->y = y0; t->w = x1 - x0; t->h = y1 - y0; t->focus = focus; t->act = act; t->a = a; t->b = b; t->shield = shield; t->nav = nav;
}
void hit_add(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b) { add(x, y, w, h, focus, act, a, b, 0, 0); }
void hit_add_nav(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b) { add(x, y, w, h, focus, act, a, b, 0, 1); }
void hit_shield(float x, float y, float w, float h) { add(x, y, w, h, NULL, NULL, 0, 0, 1, 0); }
void hit_act(float x, float y, float w, float h, HitFn act, int a, int b) { add(x, y, w, h, NULL, act, a, b, 0, 0); }
const Hit *hit_at(float x, float y) { for (int i = n - 1; i >= 0; i--) { const Hit *t = &hits[i]; if (x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h) return t; } return NULL; }
int hit_same(const Hit *a, const Hit *b) { return a && b && a->focus == b->focus && a->act == b->act && a->a == b->a && a->b == b->b && a->shield == b->shield; }
const Hit *hit_find(const Hit *like) { for (int i = n - 1; i >= 0; i--) if (hit_same(&hits[i], like)) return &hits[i]; return NULL; }
