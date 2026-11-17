/* A USB drive seen for the first time: a card in the middle of the screen that says what is being done with it
   (found, checked for writing, its speed measured: a few seconds, never more than 12) and what came of it. A drive
   fast enough, whose folder for the torrent engine can be made and written, is switched on for torrents by itself
   (Settings > Playback > Cache torrents on USB turns it off); a slow one is left off, with the advice to use an SSD.
   Back or OK puts the card away: the check ends by itself and says its result in a toast. The answer is kept only
   while the same drive is still there and nobody set its switch meanwhile. 12 s is how long the card waits, not a
   promise that the check has stopped by then: a drive stuck inside the system holds its thread until it answers, and
   no other check starts before that. */
#ifndef SB_USBPREP_H
#define SB_USBPREP_H
#include "core/usb.h"
int  usbprep_show(const UsbDrive *d, const char *storeKey); /* 1 the card is up; 0 not now (a check is still out) */
void usbprep_pump(void);          /* main thread, every frame */
void usbprep_gone(void);          /* the drive was pulled out */
int  usbprep_busy(void);          /* a check is under way (shown, hidden or given up on but still running) */
#endif
