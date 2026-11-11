#ifndef SB_LOGIN_H
#define SB_LOGIN_H
#include "core/app.h"
#include "ui/gfx.h"
extern Screen loginScreen;
/* "Signed in! Loading…" and its running line (since: when it started), at opacity a: the sign-in's last step, and the start screen's look right after it */
void login_loading_draw(double since, float a);
void login_glows_draw(void); /* the sign-in's two glows, where they are now */
void login_backdrop_draw(void); /* the sign-in's whole background (its picture, the glows over it): the same from signing in through the loading and the guide */
void login_glow_bake(void);   /* (in the tick) the glows, small, for the glass over them */
GLuint login_glow_tex(void);  /* that picture (upside down, as targets are), 0 none */
#endif
