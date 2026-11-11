/* The Library: the focused title on top, the categories and the sort, a grid of posters. */
#ifndef SB_LIB_H
#define SB_LIB_H
#include "core/app.h"
extern Screen libScreen;
void lib_from_bar(void);
int  lib_warm(void);       /* off screen, after start: the first screen made ready (1 when it is) */   /* the bar hands the focus down: the categories */
#endif
