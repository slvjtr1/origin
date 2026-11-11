/* Styled ASS / SSA subtitles (libass) for an embedded track of the film playing: its script header once, its events as
   the subtitle reader brings them, the fonts the file carries. Drawn as tinted alpha pieces in the shared atlas. All
   rendering is on a thread of its own, made only when what shows changes (or at the picture's rate while an
   animation runs); the calls below are for the main thread and cost nothing while nothing is open. */
#ifndef SB_ASSSUBS_H
#define SB_ASSSUBS_H
#include "core/mkvsubs.h"
/* starts styled drawing of track n (url: the file, for its fonts; lang: the track's language, for the fallback font) */
void asssubs_open(MkvSession *mkv, int n, const char *url, const char *lang);
void asssubs_close(void);                           /* before the reader is closed; the thread finishes on its own */
int  asssubs_on(void);
/* per frame while on: the subtitle time (seconds, the viewer's delay applied), whether it runs, the picture's rate and
   where it sits (layout units) with its own size; scale: the viewer's subtitle size (1 = 100 %); lift: the viewer's height
   (a part of the screen's height, above 0 raising the lines the script leaves at the bottom) */
void asssubs_tick(double t, int playing, double fps, float vx, float vy, float vw, float vh, int videoW, int videoH, float scale, float lift);
void asssubs_draw(void);
int  asssubs_too_slow(void);                        /* 1 once: renders stayed above 40 ms: drawn plain from now on */
double asssubs_render_ms(void); /* the styled renders' running average (0 before the first) */
#endif
