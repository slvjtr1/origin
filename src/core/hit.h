/* What the pointer can land on: each screen registers its rectangles while it draws (the later one
   is on top), with what pointing at it does (focus) and what a click on it does (act, on release;
   without act a click is the OK key). A shield absorbs clicks (a sheet's body). */
#ifndef SB_HIT_H
#define SB_HIT_H
typedef void (*HitFn)(int a, int b);
typedef struct { float x, y, w, h; HitFn focus, act; int a, b, shield, nav; } Hit;
void hit_begin(void);                                  /* each frame, before drawing */
int  hit_count(void);                                  /* what is registered so far ... */
void hit_trim(int to);                                 /* ... and dropping what came after (a page drawn only to be seen leaving) */
void hit_add(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b);
void hit_add_nav(float x, float y, float w, float h, HitFn focus, HitFn act, int a, int b);
void hit_shield(float x, float y, float w, float h);
void hit_act(float x, float y, float w, float h, HitFn act, int a, int b);  /* acts on release, no focus */
const Hit *hit_at(float x, float y);                   /* the topmost one, or NULL */
int  hit_same(const Hit *a, const Hit *b);             /* the same element (not the same rect) */
#endif
