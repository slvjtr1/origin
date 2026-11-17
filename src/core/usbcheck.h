/* The check of a USB drive seen for the first time, as its thread runs it (ui/usbprep.c shows it on a card): a file of
   its own can be made and written through to the drive, the torrent engine's folder there can be made and written, and
   how fast 2 MB writes reach the drive. The thread reports its steps and its answer in the struct, under the lock its
   caller hands it; the caller asks it to stop with usbcheck_cancel, which it sees between steps and between writes (a
   write the drive does not finish cannot be called back: the thread comes out when the drive answers or fails).
   It only ever removes files it made itself (O_EXCL, and still the same file when removed), and the engine's folder
   only when it made it and the drive is not used. */
#ifndef SB_USBCHECK_H
#define SB_USBCHECK_H
#include <pthread.h>
#include <stddef.h>
#include <sys/stat.h>

enum { UC_NONE, UC_FAST, UC_OKAY, UC_SLOW, UC_FAIL };
#define UC_FAST_MBS 15.0f   /* at or over: a fast drive */
#define UC_OK_MBS 4.0f      /* under: too slow to help (the TV's own storage does better) */

typedef struct {
  char mnt[512], engineDir[600];
  int stage;             /* 0 found, 1 checking it can be written, 2 measuring, 3 done (under the lock) */
  int res, done, failRow; float mbs; char why[96]; /* the answer (under the lock); failRow: the step that failed (1 or 2) */
  int cancel;            /* atomics: asked to stop */
} UsbCheck;

void usbcheck_init(UsbCheck *c, const char *mnt, const char *engineDir);
void usbcheck_run(UsbCheck *c, pthread_mutex_t *mu);   /* the thread's whole job: blocks until it has an answer */
void usbcheck_cancel(UsbCheck *c);                     /* any thread */
int  usbcheck_cancelled(const UsbCheck *c);

/* a new file of the app's own in dir (never one that was there): its fd, its path in path; -1 and errno */
int  usbcheck_open_own(const char *dir, const char *prefix, char *path, size_t cap);
int  usbcheck_write_all(int fd, const void *b, size_t n); /* 1 all written (through short writes and signals), else 0 and errno */
int  usbcheck_sync(int fd);                                /* 1 the drive has it (fsync, again after a signal) */
void usbcheck_unlink_own(const char *path, const struct stat *own); /* removed only while it is still that file */

/* whether a check's answer is still the one to keep: the drive it was made for is still there, mounted where it was,
   and its switch was not set meanwhile (stored NULL). 0: not kept, with why in the log */
int  usbcheck_keep_answer(const char *forId, const char *forMnt, int there, const char *curId, const char *curMnt, const char *stored);

extern int usbcheck_pause_ms;   /* the pause before each step, so each stays long enough to be read (tests: 0) */
extern double usbcheck_measure_s; /* how long the speed is measured at most (tests: shorter) */
#endif
