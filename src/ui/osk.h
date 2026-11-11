/* An on-screen keyboard for typing on the TV (the sign-in fields): the same keys as Search, with
   symbols for addresses and a shift. Drawn under a field; the remote's arrows move the ring. */
#ifndef SB_OSK_H
#define SB_OSK_H
#include "core/anim.h"
#include "core/app.h"
typedef struct { int r, c, shift, open; Tween a; Tween fx, fy, fw; int shown; Nudge nudge; } Osk;
void osk_open(Osk *k);
void osk_close(Osk *k);
/* keys: 1 handled; typed text appended to buf (cap), Backspace removes, OK on Done returns 2 */
int  osk_key(Osk *k, int key, char *buf, size_t cap);
void osk_tick(Osk *k, float dt);
float osk_height(void);
void osk_draw(Osk *k, float x, float y, float w);   /* x, y = the top-left of the keyboard area */
#endif
