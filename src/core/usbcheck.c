/* see usbcheck.h */
#include "core/usbcheck.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

int usbcheck_pause_ms = 600;
double usbcheck_measure_s = 3.0;

static double mono(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
void usbcheck_cancel(UsbCheck *c) { __atomic_store_n(&c->cancel, 1, __ATOMIC_RELEASE); }
int usbcheck_cancelled(const UsbCheck *c) { return __atomic_load_n(&c->cancel, __ATOMIC_ACQUIRE); }
static int nap(const UsbCheck *c, int ms) { /* 0 when asked to stop meanwhile */
  for (int left = ms; left > 0 && !usbcheck_cancelled(c); left -= 50) { struct timespec t = { 0, (left < 50 ? left : 50) * 1000000L }; nanosleep(&t, NULL); }
  return !usbcheck_cancelled(c);
}
static void set_stage(UsbCheck *c, pthread_mutex_t *mu, int s) { pthread_mutex_lock(mu); c->stage = s; pthread_mutex_unlock(mu); }

void usbcheck_init(UsbCheck *c, const char *mnt, const char *engineDir) {
  memset(c, 0, sizeof *c); snprintf(c->mnt, sizeof c->mnt, "%s", mnt ? mnt : ""); snprintf(c->engineDir, sizeof c->engineDir, "%s", engineDir ? engineDir : "");
}
int usbcheck_open_own(const char *dir, const char *prefix, char *path, size_t cap) {
  static unsigned seq; unsigned base = (unsigned)__atomic_add_fetch(&seq, 1, __ATOMIC_RELAXED) * 7919u ^ (unsigned)(mono() * 1000);
  for (int i = 0; i < 16; i++) {
    int n = snprintf(path, cap, "%s/%s-%d-%08x", dir, prefix, (int)getpid(), base + (unsigned)i);
    if (n < 0 || (size_t)n >= cap) { errno = ENAMETOOLONG; return -1; }
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd >= 0) return fd;
    if (errno != EEXIST && errno != EINTR) return -1; }
  errno = EEXIST; return -1;
}
int usbcheck_write_all(int fd, const void *b, size_t n) {
  const char *p = b;
  while (n) { ssize_t w = write(fd, p, n);
    if (w < 0) { if (errno == EINTR) continue; return 0; }
    if (w == 0) { errno = EIO; return 0; } /* (a drive that takes nothing: never looped on) */
    p += w; n -= (size_t)w; }
  return 1;
}
int usbcheck_sync(int fd) { for (;;) { if (!fsync(fd)) return 1; if (errno != EINTR) return 0; } }
static int same_file(const struct stat *a, const struct stat *b) {
#ifdef _WIN32 /* (the host tests on Windows: its stat has no inode, and its device differs between stat and fstat) */
  return a->st_ctime == b->st_ctime && (a->st_mode & S_IFMT) == (b->st_mode & S_IFMT); /* (its creation time there) */
#else
  return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
#endif
}
void usbcheck_unlink_own(const char *path, const struct stat *own) {
  struct stat st; if (lstat(path, &st)) return;
  if (!same_file(&st, own)) { fprintf(stderr, "[usb] %s is not the check's own file any more: left there\n", path); return; }
  unlink(path);
}

int usbcheck_keep_answer(const char *forId, const char *forMnt, int there, const char *curId, const char *curMnt, const char *stored) {
  if (!there || !curId || !curMnt || strcmp(forId, curId) || strcmp(forMnt, curMnt)) { fprintf(stderr, "[usb] the drive changed during its check: its answer is not kept\n"); return 0; }
  if (stored) { fprintf(stderr, "[usb] the switch was set meanwhile (%s): left as it is\n", stored); return 0; }
  return 1;
}
void usbcheck_run(UsbCheck *c, pthread_mutex_t *mu) {
  int res = UC_FAIL, failRow = 1; float mbs = 0; char why[96] = "", p[700] = ""; struct stat own; int haveOwn = 0, madeDir = 0, ownedFd = -1;
  unsigned char *buf = NULL; enum { CH = 2 * 1024 * 1024 };
  if (!nap(c, usbcheck_pause_ms)) goto out;
  set_stage(c, mu, 1);
  { int fd = usbcheck_open_own(c->mnt, ".origin-check", p, sizeof p);
    if (fd < 0) { snprintf(why, sizeof why, "no file can be made there (%s)", strerror(errno)); goto out; }
    if (fstat(fd, &own)) { snprintf(why, sizeof why, "its file cannot be read back (%s)", strerror(errno)); close(fd); unlink(p); p[0] = 0; goto out; } /* (made with O_EXCL just now: ours) */
    haveOwn = 1; ownedFd = fd;
    char b[4096]; memset(b, 0x5a, sizeof b);
    int ok = usbcheck_write_all(fd, b, sizeof b) && usbcheck_sync(fd); int er = errno;
    if (!ok) { snprintf(why, sizeof why, "writing failed (%s)", strerror(er)); goto out; } }
  /* the torrent engine's own folder there: the drive is only of use if that can be made and written */
  if (c->engineDir[0]) {
    if (!mkdir(c->engineDir, 0755)) madeDir = 1; else if (errno != EEXIST) { snprintf(why, sizeof why, "the engine's folder cannot be made (%s)", strerror(errno)); goto out; }
    struct stat ds; if (stat(c->engineDir, &ds) || !S_ISDIR(ds.st_mode)) { snprintf(why, sizeof why, "the engine's folder is not a folder"); goto out; }
    char q[700]; int f2 = usbcheck_open_own(c->engineDir, ".origin-check", q, sizeof q);
    if (f2 < 0) { snprintf(why, sizeof why, "the engine's folder cannot be written (%s)", strerror(errno)); goto out; }
    struct stat o2; int okSt = !fstat(f2, &o2); unsigned char *b = calloc(1, 65536);
    int ok = b && okSt && usbcheck_write_all(f2, b, 65536) && usbcheck_sync(f2); int er = b ? errno : ENOMEM; free(b);
#ifndef _WIN32
    if (okSt) usbcheck_unlink_own(q, &o2);
#endif
    if (close(f2) && ok) { ok = 0; er = errno; }
#ifdef _WIN32
    if (okSt) usbcheck_unlink_own(q, &o2);
#endif
    if (!ok) { snprintf(why, sizeof why, "the engine's folder cannot be written (%s)", strerror(er)); goto out; } }
  if (!nap(c, usbcheck_pause_ms * 5 / 6)) goto out;
  set_stage(c, mu, 2); failRow = 2;
  /* its speed: 2 MB at a time, each written through to the drive, for 3 s or 32 MB, until asked to stop */
  buf = malloc(CH); if (!buf) { snprintf(why, sizeof why, "no memory for the measure"); goto out; }
  { unsigned x = 2463534242u; for (int i = 0; i < CH; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; buf[i] = (unsigned char)x; } }
#ifdef O_DSYNC
  int fd = open(p, O_WRONLY | O_DSYNC | O_CLOEXEC);
#else
  int fd = open(p, O_WRONLY | O_CLOEXEC);
#endif
  if (fd < 0) { snprintf(why, sizeof why, "its file cannot be opened again (%s)", strerror(errno)); goto out; }
  { struct stat st; if (fstat(fd, &st) || !same_file(&st, &own)) { close(fd); snprintf(why, sizeof why, "its file was replaced"); haveOwn = 0; goto out; } }
  double t0 = mono(), el = 0; int n = 0, bad = 0, er = 0;
  while (n < 16 && el < usbcheck_measure_s && !usbcheck_cancelled(c)) {
    if (!usbcheck_write_all(fd, buf, CH)) { bad = 1; er = errno; break; }
#ifndef O_DSYNC
    if (!usbcheck_sync(fd)) { bad = 1; er = errno; break; }
#endif
    n++; el = mono() - t0; }
  if (close(fd) && !bad) { bad = 1; er = errno; }
  if (bad) snprintf(why, sizeof why, "writing failed while measuring (%s)", strerror(er));
  else if (usbcheck_cancelled(c)) snprintf(why, sizeof why, "stopped");
  else if (n > 0 && el > 0) { mbs = (float)(n * 2 / el); res = mbs >= UC_FAST_MBS ? UC_FAST : mbs >= UC_OK_MBS ? UC_OKAY : UC_SLOW; }
out:
  free(buf);
#ifndef _WIN32
  if (haveOwn) usbcheck_unlink_own(p, &own);
#endif
  if (ownedFd >= 0 && close(ownedFd) && res != UC_FAIL) {
    res = UC_FAIL; snprintf(why, sizeof why, "closing the check file failed (%s)", strerror(errno));
  }
#ifdef _WIN32
  if (haveOwn) usbcheck_unlink_own(p, &own);
#endif
  if (madeDir && res != UC_FAST && res != UC_OKAY) rmdir(c->engineDir); /* (only an empty folder it made) */
  if (usbcheck_cancelled(c) && res == UC_FAIL && !why[0]) snprintf(why, sizeof why, "stopped");
  fprintf(stderr, "[usb] check of %s: %s, %.1f MB/s written%s%s\n", c->mnt, res == UC_FAIL ? "cannot be used" : res == UC_SLOW ? "too slow" : res == UC_OKAY ? "works" : "fast", mbs, why[0] ? ": " : "", why);
  pthread_mutex_lock(mu); c->mbs = mbs; c->res = res; c->failRow = failRow; snprintf(c->why, sizeof c->why, "%s", why); c->stage = 3; c->done = 1; pthread_mutex_unlock(mu);
}
