/* Frame pacing: every frame starts a fixed period after the last one, so motion stays even. The
   period is one of a few steps that divide the display's refresh (60, 30, 20 a second on a 60 Hz TV); a screen that keeps
   running late steps down at once, and steps back up after a calm while. Also keeps the numbers
   that say whether frames really were even (period spread, worst frame, late frames). */
#ifndef SB_PACE_H
#define SB_PACE_H
void   pace_init(void);
void   pace_wait(double workMs);      /* after the swap: sleeps until the next frame is due */
void   pace_set_hz(int hz);              /* the rate the screen takes frames at, as measured (48 on a webOS 4 TV) */
double pace_period(void);             /* the current step, ms */
void   pace_report(char *out, int cap);  /* the numbers since the last report, then starts again */
/* per-screen numbers for a stress run: frames are counted against the tag set before them */
void   pace_tag(int tag);                 /* 0..31 */
void   pace_tag_report(const char *const *names, int n);   /* prints p50 / p95 / worst / late frames per tag */
#endif
void pace_observe(void);   /* SB_VSYNC=1: time the frames the driver paces */
