#include "ui/atlas.h"
#include "ui/gfx.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PAGES 10
#define MAX_SHELVES 96
typedef struct { int y, h, x; } Shelf;
typedef struct { GLuint tex; int nshelf, top, live; unsigned gen, lastUse; int sx, sy; Shelf shelf[MAX_SHELVES]; } Page;
static Page pages[MAX_PAGES]; static int npages; static unsigned frameNo = 1;

void atlas_frame(void) { frameNo++; }
int atlas_pages(void) { return npages; }
GLuint atlas_tex(int page) { return page >= 0 && page < npages ? pages[page].tex : 0; }
int atlas_page_of(GLuint tex) { if (!tex) return -1; for (int i = 0; i < npages; i++) if (pages[i].tex == tex) return i; return -1; }

static int fit(Page *p, int w, int h, int *ox, int *oy) {
  for (int i = 0; i < p->nshelf; i++) { Shelf *sh = &p->shelf[i]; if (sh->h >= h && sh->h <= h + h / 4 + 2 && sh->x + w <= ATLAS_W) { *ox = sh->x; *oy = sh->y; sh->x += w; return 1; } }
  if (p->nshelf >= MAX_SHELVES || p->top + h > ATLAS_H) return 0;
  Shelf *sh = &p->shelf[p->nshelf++]; sh->y = p->top; sh->h = h; sh->x = w; p->top += h; *ox = 0; *oy = sh->y; return 1;
}
/* every page keeps a 6x6 block that is fully on: plain fills and the straight parts of shapes sample its middle */
static void reset(Page *p, int clear) {
  p->nshelf = 0; p->top = 0; p->live = 0; p->gen++;
  (void)clear; /* nothing to wipe: every piece brings its own empty border */
  int x, y; fit(p, 6, 6, &x, &y); unsigned char on[36]; memset(on, 255, sizeof on); gfx_tex_update(p->tex, on, x, y, 6, 6, 1); p->sx = x + 3; p->sy = y + 3;
}
static int new_page(void) {
  if (npages >= MAX_PAGES) return -1;
  Page *p = &pages[npages]; unsigned char *z = calloc((size_t)ATLAS_W * ATLAS_H, 1); p->tex = gfx_tex_create(z, ATLAS_W, ATLAS_H, 1, 1); free(z);
  if (!p->tex) return -1;
  reset(p, 0); return npages++;
}
int atlas_put(const unsigned char *alpha, int w, int h, int preferPage, int keep, AtlasSlot *out) {
  out->page = -1;
  int pw = w + 2, ph = h + 2, ox, oy, at = -1;   /* 1 px of empty space around each piece: no bleeding under linear filtering */
  if (pw > ATLAS_W || ph > ATLAS_H) return 0;
  if (!npages) new_page();
  if (preferPage >= 0 && preferPage < npages && fit(&pages[preferPage], pw, ph, &ox, &oy)) at = preferPage;
  for (int i = 0; at < 0 && i < npages; i++) if (i != preferPage && fit(&pages[i], pw, ph, &ox, &oy)) at = i;
  if (at < 0) { int n = new_page(); if (n >= 0 && fit(&pages[n], pw, ph, &ox, &oy)) at = n; }
  if (at < 0) { /* all full: the page drawn least recently starts over, unless it was drawn this frame */
    int old = -1; for (int i = 0; i < npages; i++) if (pages[i].lastUse != frameNo && (old < 0 || pages[i].lastUse < pages[old].lastUse)) old = i;
    if (old < 0) return 0;
    reset(&pages[old], 1);
    if (!fit(&pages[old], pw, ph, &ox, &oy)) return 0;
    at = old;
  }
  Page *p = &pages[at];
  { /* the piece with its 1 px empty border: a reused page needs no clearing (a 1 MB upload was a stall) */
    unsigned char *pad = calloc((size_t)pw * ph, 1); for (int y = 0; y < h; y++) memcpy(pad + (size_t)(y + 1) * pw + 1, alpha + (size_t)y * w, w);
    gfx_tex_update(p->tex, pad, ox, oy, pw, ph, 1); free(pad); }
  if (keep) p->live++;
  p->lastUse = frameNo;
  out->page = at; out->gen = p->gen; out->x = ox + 1; out->y = oy + 1; out->w = w; out->h = h;
  return 1;
}
int atlas_valid(const AtlasSlot *s) { return s->page >= 0 && s->page < npages && pages[s->page].gen == s->gen; }
void atlas_touch(const AtlasSlot *s) { if (atlas_valid(s)) pages[s->page].lastUse = frameNo; }
void atlas_release(const AtlasSlot *s) {
  if (!atlas_valid(s)) return;
  Page *p = &pages[s->page];
  if (--p->live <= 0 && p->lastUse != frameNo) reset(p, 1); /* nothing kept on it: the page is free again */
}
void atlas_uv(const AtlasSlot *s, float *u0, float *v0, float *u1, float *v1) {
  *u0 = (float)s->x / ATLAS_W; *v0 = (float)s->y / ATLAS_H; *u1 = (float)(s->x + s->w) / ATLAS_W; *v1 = (float)(s->y + s->h) / ATLAS_H;
}
void atlas_solid_uv(int page, float *u, float *v) { Page *p = &pages[page < 0 || page >= npages ? 0 : page]; *u = (p->sx + 0.5f) / ATLAS_W; *v = (p->sy + 0.5f) / ATLAS_H; }
