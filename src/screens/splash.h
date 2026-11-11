/* The start screen: the app's icon coming in over its glows, glass crossing it once, until Home is complete. */
#ifndef SB_SPLASH_H
#define SB_SPLASH_H
void splash_start(int waitForHome, double signedInAt); /* signedInAt: right after signing in (when its loading began), 0 at a start */
void splash_tick(float dt);
void splash_draw(void);
int  splash_up(void);       /* the start screen is showing (or leaving) */
int  splash_signed_in(void); /* ... as the loading right after signing in */
#endif
