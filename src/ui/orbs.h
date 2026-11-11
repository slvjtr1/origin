/* The two soft glows behind Settings, Search and the guide (and in the guide's pictures): big accent lights resting on
   opposite corners like a vignette, drifting round and along their corners, on springs. Everything the viewer does
   reaches them, each kind of press in its own way and at its own strength: a move leans them the way it went, OK
   draws them in and lets them go, Back breathes them out, a tab sweeps them across, the colour keys surge the corner
   of their own colour, a long hold gathers them; what a press achieves (a page opened, a title added) is stronger
   than a step. */
#ifndef SB_ORBS_H
#define SB_ORBS_H
#include <stdint.h>
enum { ORB_LEFT, ORB_RIGHT, ORB_UP, ORB_DOWN, ORB_OK, ORB_BACK, ORB_TAB_L, ORB_TAB_R, ORB_RED, ORB_GREEN, ORB_HOLD, ORB_DONE, ORB_PHONE };
typedef struct { float p[2][4], v[2][4]; double t; float tint[2], tintV[2]; uint32_t tintC[2]; int placed; } Orbs; /* x, y, size, angle per glow */
/* one step: layout 0 rests them top left / bottom right (Settings), 1 top right / bottom left (Search); spread > 1
   pulls them out towards the middle and wider (coming or going), 1 at rest */
void orbs_step(Orbs *o, float dt, int layout, float spread, float stiffness);
void orbs_place(Orbs *o, int layout, float spread);  /* at once where the step would take them */
void orbs_draw(const Orbs *o, float alpha);           /* in the accent, the top one a touch lighter */
void orbs_draw_col(const Orbs *o, float alpha, uint32_t top, uint32_t bottom); /* in these colours (0: the accent) */
void orbs_draw_in(const Orbs *o, float alpha, float x, float y, float w, float h, float r); /* the screen's glows shrunk into a rounded box (not turned) */
void orbs_push(Orbs *o, int kind, float strength);    /* strength 0..1+: how much the press achieved */
/* the app's own pair (the pages' background): every press reaches it */
Orbs *orbs_app(void);
void  orbs_pulse(int kind, float strength);
#endif
