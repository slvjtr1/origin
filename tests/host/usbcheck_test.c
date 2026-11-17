/* core/usbcheck.c on a folder standing in for a drive: the answer, the files it leaves (none of its own), a stop asked
   before and during the check, a folder it cannot write, and when an answer is no longer the one to keep. */
#include "common.h"
#include "core/usbcheck.h"
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static int leftovers(const char *dir) { int n = 0; DIR *d = opendir(dir); if (!d) return 0; struct dirent *e; while ((e = readdir(d))) if (strstr(e->d_name, ".origin-check")) n++; closedir(d); return n; }
static void *run(void *a) { usbcheck_run(a, &mu); return NULL; }
static void fresh(char *dir, size_t n, const char *base, const char *name) { snprintf(dir, n, "%s/%s", base, name); mkdir(dir, 0755); }

int main(int argc, char **argv) {
  const char *base = argc > 1 ? argv[1] : "."; char drive[600], eng[700];
  usbcheck_pause_ms = 0; usbcheck_measure_s = 0.4;
  { fresh(drive, sizeof drive, base, "drive1"); snprintf(eng, sizeof eng, "%s/app-engine", drive);
    UsbCheck c; usbcheck_init(&c, drive, eng); usbcheck_run(&c, &mu);
    CHECK(c.done == 1); CHECK(c.stage == 3); CHECKF(c.res == UC_FAST || c.res == UC_OKAY || c.res == UC_SLOW, "res %d (%s)", c.res, c.why); CHECK(c.mbs > 0);
    CHECK(leftovers(drive) == 0); CHECK(leftovers(eng) == 0);
    struct stat st; int engThere = !stat(eng, &st);
    CHECKF(c.res == UC_SLOW ? !engThere : engThere, "the engine folder is kept only for a drive in use (res %d, there %d)", c.res, engThere); }
  { /* asked to stop before it starts: nothing made, nothing left, stopped */
    fresh(drive, sizeof drive, base, "drive2"); snprintf(eng, sizeof eng, "%s/app-engine", drive);
    UsbCheck c; usbcheck_init(&c, drive, eng); usbcheck_cancel(&c); usbcheck_run(&c, &mu);
    CHECK(c.done == 1); CHECK(c.res == UC_FAIL); CHECK(!strcmp(c.why, "stopped")); CHECK(leftovers(drive) == 0);
    struct stat st; CHECK(stat(eng, &st) != 0); }
  { /* asked to stop while it measures: it comes out at its next write */
    fresh(drive, sizeof drive, base, "drive3"); snprintf(eng, sizeof eng, "%s/app-engine", drive);
    usbcheck_measure_s = 30; UsbCheck c; usbcheck_init(&c, drive, eng); pthread_t th; pthread_create(&th, NULL, run, &c);
    double t0 = host_now(); for (;;) { pthread_mutex_lock(&mu); int s = c.stage; pthread_mutex_unlock(&mu); if (s >= 2 || host_now() - t0 > 5) break; host_sleep_ms(5); }
    host_sleep_ms(100); double t1 = host_now(); usbcheck_cancel(&c); pthread_join(th, NULL);
    CHECKF(host_now() - t1 < 2.0, "stopped in %.2f s", host_now() - t1); CHECK(c.res == UC_FAIL); CHECK(leftovers(drive) == 0);
    struct stat st; CHECK(stat(eng, &st) != 0); usbcheck_measure_s = 0.4; }
  { /* a folder that is not there: it cannot be written, the first step failed */
    snprintf(drive, sizeof drive, "%s/not-there/at-all", base); UsbCheck c; usbcheck_init(&c, drive, ""); usbcheck_run(&c, &mu);
    CHECK(c.res == UC_FAIL); CHECK(c.failRow == 1); CHECK(c.why[0] != 0); }
  { /* its own files only: two in a row never share a name, and an existing file is never opened */
    fresh(drive, sizeof drive, base, "drive4"); char a[700], b[700];
    int fa = usbcheck_open_own(drive, ".origin-check", a, sizeof a), fb = usbcheck_open_own(drive, ".origin-check", b, sizeof b);
    CHECK(fa >= 0 && fb >= 0 && strcmp(a, b)); struct stat sa; fstat(fa, &sa); close(fa); close(fb);
    usbcheck_unlink_own(a, &sa); struct stat x; CHECK(stat(a, &x) != 0); CHECK(stat(b, &x) == 0); unlink(b);
#ifndef _WIN32 /* (a replaced file is told apart by its inode: Windows' stat gives none) */
    int f = usbcheck_open_own(drive, ".origin-check", a, sizeof a); struct stat own; fstat(f, &own); close(f); unlink(a);
    int g = open(a, O_CREAT | O_WRONLY, 0600); close(g); usbcheck_unlink_own(a, &own); CHECKF(stat(a, &x) == 0, "a file put in its place is left there"); unlink(a);
#endif
  }
  { /* writing through short writes */
    fresh(drive, sizeof drive, base, "drive5"); char a[700]; int f = usbcheck_open_own(drive, "w", a, sizeof a); static char big[3 << 20];
    CHECK(usbcheck_write_all(f, big, sizeof big)); CHECK(usbcheck_sync(f)); struct stat st; fstat(f, &st); CHECK(st.st_size == (long)sizeof big); close(f); unlink(a); }
  { /* which answers are kept */
    CHECK(usbcheck_keep_answer("ID1", "/tmp/usb/sda/sda1", 1, "ID1", "/tmp/usb/sda/sda1", NULL) == 1);
    CHECK(usbcheck_keep_answer("ID1", "/tmp/usb/sda/sda1", 0, NULL, NULL, NULL) == 0);                         /* pulled out */
    CHECK(usbcheck_keep_answer("ID1", "/tmp/usb/sda/sda1", 1, "ID2", "/tmp/usb/sda/sda1", NULL) == 0);        /* another drive in its place */
    CHECK(usbcheck_keep_answer("ID1", "/tmp/usb/sda/sda1", 1, "ID1", "/tmp/usb/sdb/sdb1", NULL) == 0);        /* the same drive, plugged in again elsewhere */
    CHECK(usbcheck_keep_answer("ID1", "/tmp/usb/sda/sda1", 1, "ID1", "/tmp/usb/sda/sda1", "false") == 0); }   /* the viewer chose meanwhile */
  return host_done("usbcheck");
}
