/* The screensaver: after a while without a key (the "ssDelay" setting) the titles of the moment fill the screen,
   one at a time, drifting slowly, with the time. Not on the sign-in page, never over a film. */
#ifndef SB_SAVER_H
#define SB_SAVER_H
void saver_tick(float dt);
int  saver_on(void);        /* showing (keys wake it) */
int  saver_covers(void);    /* fully in: the page under it need not be drawn */
void saver_draw(void);      /* over everything (also while it fades away) */
void saver_stop(void);
#endif
