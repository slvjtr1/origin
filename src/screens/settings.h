#ifndef SB_SETTINGS_H
#define SB_SETTINGS_H
#include "core/app.h"
extern Screen setScreen;
void settings_open_about(void);
void settings_open_keys(void);    /* Settings > Keys (the TMDB key, the Trakt id) */
int  settings_apply_pref(const char *key, const char *value); /* a setting from outside (a phone): kept and applied as a change here is; 1 = it changed */
void settings_open_usb(void);     /* the USB notices: Playback, the USB drive's row */   /* the update notice: About, its first row */
#endif
