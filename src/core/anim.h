/* Motion: values that glide to a target with an easing curve, ticked once per frame. The curves
   are the ones the web app used (cubic-bezier(.2,.8,.2,1) and friends), approximated by named
   easings so every screen moves the same way. */
#ifndef SB_ANIM_H
#define SB_ANIM_H
#include <math.h>

typedef enum { EASE_OUT = 0, EASE_IN_OUT, EASE_OUT_BACK, EASE_LINEAR, EASE_OUT_SOFT,
  EASE_CSS_EASE, EASE_CSS_EASEOUT, EASE_ROW /* cubic-bezier(.25,1,.5,1) */, EASE_SNAP /* (.23,1,.32,1) */, EASE_STD /* (.4,0,.2,1) */, EASE_CSS_EASEIN, EASE_WEB /* (.2,.8,.2,1) */,
  EASE_CSS_IO /* CSS 'ease-in-out' (.42,0,.58,1) */ } Ease;
static inline float ease_bezier(float x1, float y1, float x2, float y2, float x);

typedef struct {
  float from, to, v;      /* start, target, current */
  float t, dur;           /* seconds elapsed / duration */
  Ease ease;
  int on;
} Tween;

static inline float ease_apply(Ease e, float x) {
  if (x <= 0) return 0; if (x >= 1) return 1;
  switch (e) {
    case EASE_LINEAR: return x;
    case EASE_IN_OUT: return x < 0.5f ? 2 * x * x : 1 - powf(-2 * x + 2, 2) / 2;
    case EASE_OUT_BACK: { float c1 = 1.70158f * 0.55f, c3 = c1 + 1; float y = x - 1; return 1 + c3 * y * y * y + c1 * y * y; }
    case EASE_OUT_SOFT: return 1 - powf(1 - x, 2);
    case EASE_CSS_EASE: return ease_bezier(.25f, .1f, .25f, 1, x);
    case EASE_CSS_EASEOUT: return ease_bezier(0, 0, .58f, 1, x);
    case EASE_ROW: return ease_bezier(.25f, 1, .5f, 1, x);
    case EASE_SNAP: return ease_bezier(.23f, 1, .32f, 1, x);
    case EASE_STD: return ease_bezier(.4f, 0, .2f, 1, x);
    case EASE_CSS_EASEIN: return ease_bezier(.42f, 0, 1, 1, x);
    case EASE_WEB: return ease_bezier(.2f, .8f, .2f, 1, x);
    case EASE_CSS_IO: return ease_bezier(.42f, 0, .58f, 1, x);
    default: return 1 - powf(1 - x, 3);  /* EASE_OUT ~ cubic-bezier(.2,.8,.2,1) */
  }
}
/* the whole app's motion, one knob: every tween, timer and glide runs this share of the time it asks for (Reduce
   motion in Settings: far less, the app snaps from place to place) */
extern float anim_speed;
#define ANIM_SPEED (anim_speed)
static inline void tween_set(Tween *t, float v) { t->from = t->to = t->v = v; t->on = 0; t->t = 0; }
static inline void tween_to(Tween *t, float target, float dur, Ease e) {
  if (t->to == target && (t->on || t->v == target)) return;
  t->from = t->v; t->to = target; t->t = 0; dur *= ANIM_SPEED; t->dur = dur > 0 ? dur : 0.0001f; t->ease = e; t->on = 1;
  if (dur <= 0) { t->v = target; t->on = 0; }
}
static inline int tween_tick(Tween *t, float dt) {
  if (!t->on) return 0;
  t->t += dt; float x = t->t / t->dur;
  if (x >= 1) { t->v = t->to; t->on = 0; return 1; }
  t->v = t->from + (t->to - t->from) * ease_apply(t->ease, x); return 1;
}
static inline int tween_done(const Tween *t) { return !t->on; }

/* a one-shot timer: 0..1 over dur seconds */
typedef struct { float t, dur; int on; } Timer1;
static inline void timer_start(Timer1 *t, float dur) { t->t = 0; t->dur = dur * ANIM_SPEED; t->on = 1; }
static inline float timer_tick(Timer1 *t, float dt) { if (!t->on) return 1; t->t += dt; if (t->t >= t->dur) { t->on = 0; return 1; } return t->t / t->dur; }
static inline float timer_x(const Timer1 *t) { return t->on ? (t->dur > 0 ? t->t / t->dur : 1) : 1; }

/* a CSS cubic-bezier(x1,y1,x2,y2) at progress x (0..1): the web's exact curves where a named easing is too far off */
static inline float ease_bezier(float x1, float y1, float x2, float y2, float x) {
  if (x <= 0) return 0; if (x >= 1) return 1;
  float lo = 0, hi = 1, t = x;
  for (int i = 0; i < 18; i++) { float u = 1 - t, bx = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; if (bx < x) lo = t; else hi = t; t = (lo + hi) / 2; }
  float u = 1 - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
}
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
/* one glide for every highlight, pill and ring that moves with the focus (sheets, menu, seasons, Library, top bar,
   episodes, faces): the same curve and time everywhere, so nothing moves differently */
#define GLIDE_EASE EASE_ROW
#define GLIDE_DUR 0.28f
#endif
