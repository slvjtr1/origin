/* The app's own log: every line written to stderr (the existing fprintf calls, untouched) still goes where it
   went (the terminal on the Mac, /tmp/<appid>.log on the TV) and is also kept, scrubbed, in a small rotating
   file in the data folder plus the last lines in memory. Settings > About > Share logs seals that and shows the code that finds it. */
#ifndef SB_APPLOG_H
#define SB_APPLOG_H
#include <stddef.h>

/* the platform calls this once, as early as it knows the data folder */
void  applog_start(const char *dataDir);
/* the bundle as shared, before the sharing scrub: device lines and the last 400 app lines (malloc'd) */
char *applog_bundle(void);
/* Share logs: seals the bundle on a worker and leaves it on the relay (a share still running, or one made in
   the last 10 minutes, answers instead) */
int   applog_share_start(void);
/* 0 = still working, 1 = done (out: the 4-character code), -1 = failed (out: "net", "relay", "seal") */
int   applog_share_state(char *out, size_t cap);
#endif
