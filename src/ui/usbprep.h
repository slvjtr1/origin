/* A USB drive seen for the first time: a card in the middle of the screen that says what is being done with it
   (found, checked for writing, its speed measured: a few seconds, never more than 12) and what came of it. A drive
   fast enough is switched on for torrents by itself (Settings > Playback > Cache torrents on USB turns it off); a
   slow one is left off, with the advice to use an SSD. Back puts the card away: the check ends by itself and says
   its result in a toast. */
#ifndef SB_USBPREP_H
#define SB_USBPREP_H
#include "core/usb.h"
void usbprep_show(const UsbDrive *d, const char *storeKey);
void usbprep_pump(void);          /* main thread, every frame */
void usbprep_gone(void);          /* the drive was pulled out */
int  usbprep_busy(void);          /* a check is under way (shown or not) */
#endif
