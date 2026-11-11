/* USB drives for the torrent cache. A background thread looks at the mounted filesystems every few seconds (never
   the drawing thread) and keeps the best writable USB drive: when one comes that was never answered, the notice
   slides out of the Settings icon ("USB drive found: use it for torrents?"); OK turns it on for that drive (kept per
   drive in the store). While it is on and the drive is there, torrents keep their window on it (p2p.c); when it goes,
   they move on by themselves. Development: SB_USB_DIR=<folder> stands in for a drive mounted there, SB_USB_ON=1 counts it as switched on. */
#ifndef SB_USB_H
#define SB_USB_H
#include <stddef.h>
typedef struct {
  char mnt[512];      /* where it is mounted */
  char fs[24];        /* vfat, exfat, ntfs, ext4… (msdos, exfat… on the Mac) */
  char id[96];        /* the filesystem's UUID, else its type and size: the answer is kept under it */
  char name[64];      /* its label, else "" */
  long freeMb, totalMb;
  int punch;          /* it can free bytes inside a file; 0 (FAT32, exFAT): the window is kept as segment files */
} UsbDrive;
void        usb_pump(void);                 /* main thread, every frame: what the watcher found, the notice */
int         usb_drive(UsbDrive *out);       /* 1 and the drive when one is there (any thread) */
int         usb_cache_on(UsbDrive *out);    /* 1 when torrents should use it now: there and switched on (any thread) */
int         usb_unreachable(char *name, size_t n); /* 1 when the TV has a drive mounted that this app cannot reach (a jail made before it came); its label */
unsigned    usb_serial(void);               /* changes when a drive comes or goes, or its switch changes */
void        usb_shutdown(void);             /* the watcher ends (at exit) */
#endif
