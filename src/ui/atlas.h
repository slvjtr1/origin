/* Shared alpha pages: text lines, icons and the corner pieces of rounded shapes live together so
   a screen draws in a few batches. A page is cut into shelves of one height; it is wiped and
   reused once nothing on it is wanted, or (when all are full) the page drawn least recently goes,
   but never one drawn in the current frame. */
#ifndef SB_ATLAS_H
#define SB_ATLAS_H
#include "ui/gl.h"
typedef struct { int page; unsigned gen; int x, y, w, h; } AtlasSlot;   /* page -1 = no slot */
#define ATLAS_W 1024
#define ATLAS_H 1024
void   atlas_frame(void);                                        /* once per frame */
int    atlas_put(const unsigned char *alpha, int w, int h, int preferPage, int keep, AtlasSlot *out); /* 1 = placed; keep = counted as live until released */
int    atlas_valid(const AtlasSlot *s);                          /* still there (its page was not wiped) */
void   atlas_touch(const AtlasSlot *s);                          /* drawn this frame */
void   atlas_release(const AtlasSlot *s);                        /* no longer wanted (only for keep = 1) */
GLuint atlas_tex(int page);
int    atlas_page_of(GLuint tex);                                /* -1 when tex is not a page */
void   atlas_uv(const AtlasSlot *s, float *u0, float *v0, float *u1, float *v1);
void   atlas_solid_uv(int page, float *u, float *v);             /* a texel that is fully on */
int    atlas_pages(void);
#endif
