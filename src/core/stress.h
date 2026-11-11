/* SB_STRESS=<loops>: a self-driven run through every page with per-screen frame numbers (development). */
#ifndef SB_STRESS_H
#define SB_STRESS_H
void stress_init(void);
void stress_tick(void);   /* once per frame, before the frame is drawn */
int  stress_on(void);
#endif
const char *stress_screen(void);  /* the screen now (logs) */
