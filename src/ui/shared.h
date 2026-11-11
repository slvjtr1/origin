/* Shared elements across a hand-over (a page switch, the player opening or closing): see shared.c */
#ifndef SB_SHARED_H
#define SB_SHARED_H
#include "ui/gfx.h"
#include "ui/widgets.h"
void sx_frame_begin(void);   /* once a frame, before anything is drawn */
void sx_begin(int toLayer);  /* a hand-over starts: what the last frame showed may fly to the new page (toLayer 0) or the player (1) */
void sx_begin_quick(int toLayer); /* the same, as a quick dash (into and out of the player): 0.22 s, eased out, a light trail */
void sx_layer(int l);        /* what is drawn now: 0 the page, 1 the player */
int  sx_flying(void);
double sx_since(void);       /* seconds since the hand-over started */
void sx_ghost(int on);       /* 1 while a leaving page is drawn over the next one (its exit): see shared.c */
/* gfx_image with a name: drawn as usual, or (in a hand-over, when the last page had the same name) flying from
   there, drawn after the page by sx_flush(). 1 = it flies this frame */
int  sx_image(const char *key, GLuint tex, float x, float y, float w, float h, float radius, uint32_t tint, float u0, float v0, float u1, float v1);
int  sx_text_wrapped(const char *key, const char *utf8, float x, float y, float maxW, int maxLines, int px, int weight, uint32_t color, float tracking, int align, float lh); /* text_draw_wrapped with a name */
void sx_flush(void);         /* the flying ones, after the page (and the player) */
int  sx_arrived(const char *key);
unsigned sx_from_tex(const char *key);  /* the picture it had on the last page (shown until its own has come) */  /* it came over from the last page: shown at once, no entrance of its own */
#endif
