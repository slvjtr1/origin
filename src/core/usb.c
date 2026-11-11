/* USB drives for the torrent cache (see usb.h). Where a drive is mounted is gathered three ways, because a
   developer-mode app runs jailed and may not see what the TV sees: the physical device service (luna, its
   mountName), /proc/mounts, and a walk of /tmp/usb/<disk>/<part> (webOS mounts drives there, the letters follow
   the plug order). A candidate counts only if, from inside the app, it is a mount of its own (another device than
   its parent: a mount point left behind on /tmp after the drive went is not a drive), its block device is still
   there, and a file can be written there. All of it runs on the watcher thread; the drawing thread only copies the
   result.

   webOS developer-mode jails (measured on webOS 4.4): the jail gets a private copy of /tmp/usb with the drives
   mounted when it was set up. A drive pulled out stays mounted inside the jail (its device gone: reads give old
   numbers, writes fail with EIO), and a drive plugged in afterwards (it comes back as the next letter, sdb1…) is
   an empty folder inside the jail while the TV has it mounted. The physical device service still names it, so
   the app can say why it cannot use it instead of retrying it every few seconds. */
#include "core/usb.h"
#include "core/app.h"
#include "screens/settings.h"
#include "core/keys.h"
#include "core/store.h"
#include "core/i18n.h"
#include "core/update.h"
#include "core/json.h"
#include "platform/plat.h"
#include "screens/player.h"
#include "cJSON.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#ifdef __linux__
#include <sys/vfs.h>
#include <sys/sysmacros.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif

#define MIN_FREE_MB 512   /* less than this is no help over the TV's own storage */
#define LOOK_S 3          /* seconds between looks */

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static pthread_t th; static int running, quit;
static UsbDrive cur; static int have, on; static unsigned serial; /* under mu */
static char key[128];                                               /* main thread */

static int same_dev_as_parent(const char *p) { /* a mount point left behind (an empty folder) is on its parent's device */
  char par[600]; snprintf(par, sizeof par, "%s", p); char *s = strrchr(par, '/'); if (!s) return 1; if (s == par) s[1] = 0; else *s = 0;
  struct stat a, b; if (stat(p, &a) || stat(par, &b)) return 1; return a.st_dev == b.st_dev; }
static void fs_name(const char *p, const char *hint, char *o, size_t n) { /* what the source said, else the filesystem's magic */
  if (hint && *hint && strcmp(hint, "fuseblk")) { snprintf(o, n, "%s", hint); return; }
  struct statfs f; const char *k = NULL; if (!statfs(p, &f)) {
#ifdef __linux__
    unsigned long t = (unsigned long)f.f_type;
    k = t == 0x4d44 ? "vfat" : t == 0x2011BAB0UL ? "exfat" : t == 0x5346544eUL ? "ntfs" : t == 0x7366746eUL ? "ntfs3" : t == 0xEF53 ? "ext4" : t == 0x482b ? "hfsplus" : t == 0x65735546UL ? "fuseblk" : NULL;
#else
    k = f.f_fstypename;
#endif
  }
  snprintf(o, n, "%s", k ? k : hint && *hint ? hint : "?");
}
/* the block device under a mount is still there (a drive pulled out leaves its mount behind inside a jail);
   1 when it cannot be told (no /sys/dev/block in view, the Mac, a folder standing in for a drive) */
static int dev_alive(const char *p) {
#ifdef __linux__
  struct stat a; if (stat(p, &a)) return 0;
  struct stat sb; if (stat("/sys/dev/block", &sb) || !S_ISDIR(sb.st_mode)) return 1;
  if (major(a.st_dev) == 0) return 1; /* not a block device (tmpfs, fuse…): nothing to look up */
  char q[64]; snprintf(q, sizeof q, "/sys/dev/block/%u:%u", (unsigned)major(a.st_dev), (unsigned)minor(a.st_dev));
  return !access(q, F_OK);
#else
  (void)p; return 1;
#endif
}
/* can the app write there, and free bytes inside a file (else the engine keeps segment files) */
static int probe(const char *mnt, int *punch) {
  char p[640]; snprintf(p, sizeof p, "%s/.sb-probe-%d", mnt, (int)getpid());
  int fd = open(p, O_CREAT | O_TRUNC | O_RDWR, 0600); if (fd < 0) return 0;
  char buf[4096]; memset(buf, 7, sizeof buf); int ok = 1; for (int i = 0; i < 16 && ok; i++) ok = write(fd, buf, sizeof buf) == (ssize_t)sizeof buf;
  *punch = 0;
  if (ok) {
#ifdef __linux__
    *punch = !fallocate(fd, 0x01 | 0x02 /* KEEP_SIZE | PUNCH_HOLE */, 16 * 1024, 32 * 1024);
#elif defined(__APPLE__)
    struct fpunchhole h; memset(&h, 0, sizeof h); h.fp_offset = 16 * 1024; h.fp_length = 32 * 1024; *punch = !fcntl(fd, F_PUNCHHOLE, &h);
#endif
  }
  int er = ok ? 0 : errno; close(fd); unlink(p); errno = er; return ok;
}

/* the candidates of one look: path, the type the source gave, uuid and label when the source knows them */
typedef struct { char path[512], fs[24], uuid[64], label[64]; int tvHas; } Cand; /* tvHas: the TV's device service has it mounted there */
/* a filesystem id as a store key: some firmwares hand the UUID with the rest of the probe line glued on
   ('D6F0-1E21" TYPE="vfat'): it ends at the first character that is not part of an id */
static void clean_id(char *s) { char *o = s; for (; *s; s++) { char ch = *s; if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '-' || ch == '_') *o++ = ch; else break; } *o = 0; }
static int add(Cand *c, int n, int max, const char *path, const char *fs, const char *uuid, const char *label) {
  if (!path || !*path || n >= max) return n;
  for (int i = 0; i < n; i++) if (!strcmp(c[i].path, path)) {
    if (uuid && *uuid && !c[i].uuid[0]) { snprintf(c[i].uuid, sizeof c[i].uuid, "%s", uuid); clean_id(c[i].uuid); }
    if (label && *label && !c[i].label[0]) snprintf(c[i].label, sizeof c[i].label, "%s", label);
    if (fs && *fs && !c[i].fs[0]) snprintf(c[i].fs, sizeof c[i].fs, "%s", fs);
    return n; }
  memset(&c[n], 0, sizeof *c); snprintf(c[n].path, sizeof c[n].path, "%s", path); snprintf(c[n].fs, sizeof c[n].fs, "%s", fs ? fs : "");
  snprintf(c[n].uuid, sizeof c[n].uuid, "%s", uuid ? uuid : ""); clean_id(c[n].uuid); snprintf(c[n].label, sizeof c[n].label, "%s", label ? label : ""); return n + 1;
}
#ifdef __linux__
static void unescape(char *s) { char *o = s; while (*s) { if (s[0] == '\\' && s[1] >= '0' && s[1] <= '7' && s[2] && s[3]) { *o++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0')); s += 4; } else *o++ = *s++; } *o = 0; }
static int from_mounts(Cand *c, int n, int max) {
  FILE *f = fopen("/proc/mounts", "r"); if (!f) return n; char ln[1200];
  while (fgets(ln, sizeof ln, f)) { char dev[256], mnt[512], fs[32], opt[256]; if (sscanf(ln, "%255s %511s %31s %255s", dev, mnt, fs, opt) != 4) continue; unescape(mnt);
    if (!strcmp(opt, "ro") || !strncmp(opt, "ro,", 3)) continue;
    int usbPath = !strncmp(mnt, "/tmp/usb/", 9) || !strncmp(mnt, "/mnt/usb", 8) || !strncmp(mnt, "/media/usb", 10) || !strncmp(mnt, "/run/media/", 11);
    if (!usbPath && !(!strncmp(dev, "/dev/sd", 7) && strcmp(mnt, "/"))) continue; /* USB mass storage is sdX on a TV (its own flash is mmcblk) */
    static const char *ok[] = { "vfat", "msdos", "exfat", "texfat", "ntfs", "ntfs3", "tntfs", "fuseblk", "ext2", "ext3", "ext4", "hfsplus", "f2fs", "btrfs", "xfs" };
    int good = 0; for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) if (!strcmp(fs, ok[i])) good = 1; if (!good) continue;
    n = add(c, n, max, mnt, fs, NULL, NULL); }
  fclose(f); return n;
}
/* KEY="value" from a probe line ("/dev/sda1: LABEL="GTAVC" UUID="D6F0-1E21" TYPE="vfat"") */
static void probe_val(const char *ln, const char *key, char *o, size_t n) {
  o[0] = 0; char k[24]; snprintf(k, sizeof k, " %s=\"", key); const char *q = strstr(ln, k); if (!q) return; q += strlen(k);
  size_t i = 0; while (*q && *q != '"' && i + 1 < n) o[i++] = *q++; o[i] = 0;
}
static int from_walk(Cand *c, int n, int max) { /* /tmp/usb/<disk>/<part>, with what /tmp/usb/blockinfo/<part> says of it */
  DIR *d = opendir("/tmp/usb"); if (!d) return n; struct dirent *e;
  while ((e = readdir(d))) { if (e->d_name[0] == '.' || !strcmp(e->d_name, "blockinfo")) continue; char p[300]; snprintf(p, sizeof p, "/tmp/usb/%s", e->d_name); DIR *d2 = opendir(p); if (!d2) continue; struct dirent *e2;
    while ((e2 = readdir(d2))) { if (e2->d_name[0] == '.') continue; char q[600]; snprintf(q, sizeof q, "%s/%s", p, e2->d_name);
      char bi[400], ln[512] = "", uuid[64], label[64], type[24]; snprintf(bi, sizeof bi, "/tmp/usb/blockinfo/%s", e2->d_name);
      FILE *f = fopen(bi, "r"); if (f) { if (!fgets(ln, sizeof ln, f)) ln[0] = 0; fclose(f); }
      probe_val(ln, "UUID", uuid, sizeof uuid); probe_val(ln, "LABEL", label, sizeof label); probe_val(ln, "TYPE", type, sizeof type);
      n = add(c, n, max, q, type, uuid, label); }
    closedir(d2); }
  closedir(d); return n;
}
/* the physical device service: what the TV has mounted (USB storage only, not its own flash), with the
   filesystem's UUID and label */
static int from_pdm(Cand *c, int n, int max) {
  char *r = plat_luna_call("luna://com.webos.service.pdm/getAttachedStorageDeviceList", "{\"subscribe\":false}", 2000); if (!r) return n;
  cJSON *j = cJSON_Parse(r); free(r); const cJSON *dl = js_arr(j, "storageDeviceList"), *d;
  cJSON_ArrayForEach(d, dl) {
    const char *ty = js_str(d, "deviceType", ""); if (*ty && strcmp(ty, "USB_STORAGE")) continue;
    const cJSON *vl = js_arr(d, "storageDriveList"), *v; cJSON_ArrayForEach(v, vl) {
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v, "isMounted"))) continue;
    const char *mn = js_str(v, "mountName", ""); int m = add(c, n, max, mn, js_str(v, "fsType", ""), js_str(v, "uuid", ""), js_str(v, "volumeLabel", ""));
    for (int i = 0; i < m; i++) if (!strcmp(c[i].path, mn)) c[i].tvHas = 1;
    n = m; } }
  cJSON_Delete(j); return n;
}
#endif

/* why a drive in view is not used, said once per appearance (not every look): the reasons of the last look are
   kept, a line is written only for a path or reason that was not in it */
enum { WHY_WRITE = 1, WHY_STALE, WHY_JAIL };
static struct { char path[512]; int why; } said[16], saying[16]; static int nsaid, nsaying;
static int note(const char *path, int why) {
  if (nsaying < 16) { snprintf(saying[nsaying].path, sizeof saying[nsaying].path, "%s", path); saying[nsaying++].why = why; }
  for (int i = 0; i < nsaid; i++) if (said[i].why == why && !strcmp(said[i].path, path)) return 0;
  return 1;
}
static int unreach; static char unreachName[64]; /* under mu: the TV has a drive the app cannot reach */

/* one look: the writable drive with the most room, or none */
static int look(UsbDrive *best, const UsbDrive *prev, int hadPrev) {
  Cand c[16]; int n = 0; nsaying = 0; int jail = 0; char jailName[64] = "";
#ifdef __linux__
  n = from_pdm(c, n, 16); n = from_mounts(c, n, 16); n = from_walk(c, n, 16);
#endif
  const char *dev = getenv("SB_USB_DIR"); if (dev && *dev) n = add(c, n, 16, dev, NULL, NULL, NULL); /* development: a folder (or a disk image's volume) as the drive */
  int found = 0;
  for (int i = 0; i < n; i++) {
    int devDir = dev && !strcmp(c[i].path, dev);
    struct statvfs v; if (statvfs(c[i].path, &v)) continue;
    if (!devDir && same_dev_as_parent(c[i].path)) {
      /* the TV has it mounted there, and the app sees an empty folder: plugged in after the app's jail was made */
      if (c[i].tvHas) { jail = 1; snprintf(jailName, sizeof jailName, "%s", c[i].label);
        if (note(c[i].path, WHY_JAIL)) fprintf(stderr, "[usb] the TV has a drive at %s%s%s, but this app does not see it there (plugged in after the app's sandbox was set up: it shows after the TV restarts)\n", c[i].path, c[i].label[0] ? ", label " : "", c[i].label); }
      continue; }
    if (!devDir && !dev_alive(c[i].path)) { if (note(c[i].path, WHY_STALE)) fprintf(stderr, "[usb] %s: its drive was pulled out (only the mount is left), not used\n", c[i].path); continue; }
    long fr = (long)((unsigned long long)v.f_bavail * v.f_frsize >> 20), tot = (long)((unsigned long long)v.f_blocks * v.f_frsize >> 20);
    if (fr < MIN_FREE_MB || (found && fr <= best->freeMb)) continue;
    UsbDrive d; memset(&d, 0, sizeof d); snprintf(d.mnt, sizeof d.mnt, "%s", c[i].path); fs_name(c[i].path, c[i].fs, d.fs, sizeof d.fs);
    d.freeMb = fr; d.totalMb = tot; snprintf(d.name, sizeof d.name, "%s", c[i].label);
    if (c[i].uuid[0]) snprintf(d.id, sizeof d.id, "%s", c[i].uuid); else snprintf(d.id, sizeof d.id, "%s-%ld", d.fs, tot); /* no UUID in view: its type and size */
    if (hadPrev && !strcmp(prev->mnt, d.mnt) && !strcmp(prev->id, d.id)) d.punch = prev->punch; /* the same drive: probed already */
    else if (!probe(d.mnt, &d.punch)) { if (note(d.mnt, WHY_WRITE)) fprintf(stderr, "[usb] %s (%s, %ld MB free): cannot write there (%s), not used\n", d.mnt, d.fs, fr, strerror(errno)); continue; }
    *best = d; found = 1; }
  memcpy(said, saying, sizeof said); nsaid = nsaying;
  int u = found ? 0 : jail;
  pthread_mutex_lock(&mu); if (u != unreach) serial++; unreach = u; snprintf(unreachName, sizeof unreachName, "%s", jailName); pthread_mutex_unlock(&mu);
  return found;
}
static void *watch(void *a) { (void)a; plat_thread_background(15);
  UsbDrive prev; memset(&prev, 0, sizeof prev); int had = 0;
  pthread_mutex_lock(&mu);
  while (!quit) {
    pthread_mutex_unlock(&mu);
    UsbDrive d; int got = look(&d, &prev, had);
    if (got && (!had || strcmp(d.mnt, prev.mnt) || strcmp(d.id, prev.id)))
      fprintf(stderr, "[usb] found %s %s %ld MB free of %ld MB%s%s%s\n", d.mnt, d.fs, d.freeMb, d.totalMb, d.punch ? "" : " (no hole punching: segment files)", d.name[0] ? ", label " : "", d.name);
    else if (!got && had) fprintf(stderr, "[usb] removed %s\n", prev.mnt);
    pthread_mutex_lock(&mu);
    int changed = got != had || (got && (strcmp(d.mnt, cur.mnt) || strcmp(d.id, cur.id)));
    if (got) cur = d; else memset(&cur, 0, sizeof cur);
    have = got; if (changed) { serial++; if (!got) on = 0; } /* gone: off at once, before the main thread looks */
    prev = d; had = got;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += LOOK_S;
    if (!quit) pthread_cond_timedwait(&wake, &mu, &ts);
  }
  pthread_mutex_unlock(&mu); return NULL;
}

int usb_drive(UsbDrive *o) { pthread_mutex_lock(&mu); int h = have; if (h && o) *o = cur; pthread_mutex_unlock(&mu); return h; }
int usb_cache_on(UsbDrive *o) { pthread_mutex_lock(&mu); int r = have && on; if (r && o) *o = cur; pthread_mutex_unlock(&mu); return r; }
int usb_unreachable(char *name, size_t n) { pthread_mutex_lock(&mu); int u = unreach; if (u && name && n) snprintf(name, n, "%s", unreachName); pthread_mutex_unlock(&mu); return u; }
unsigned usb_serial(void) { pthread_mutex_lock(&mu); unsigned s = serial; pthread_mutex_unlock(&mu); return s; }

/* the notice: asked once per drive while it was never answered, when no film plays */
static unsigned seen = ~0u; static int ask, tellJail; static char askedFor[96];
static void open_usb_setting(void);
static void open_usb_restart(void) { open_usb_setting(); } /* (its own name: the drive-found notice is cancelled when the drive goes, this one is not) */
static void open_usb_setting(void) { /* OK on a USB notice: Settings, on the drive's row (it is switched on there) */
  if (app_overlay) { app_overlay->key(K_BACK); app_overlay = NULL; }
  topNav.idx = NAV_SET; app_go(SCR_SET, NULL, 1); settings_open_usb();
}
static int calm(void) { return app_splash_done && SCR != SCR_LOGIN && !app_overlay && !player_open() && app_idle() > 1; } /* never over sign-in, a sheet or a film */
void usb_pump(void) {
  if (!running) { running = 1; if (pthread_create(&th, NULL, watch, NULL)) running = -1; }
  static double nextT; double t = app_now(); unsigned s = usb_serial();
  if (s != seen || t >= nextT) { /* a drive came or went; else once a second, Settings may have flipped the switch */
    nextT = t + 1; seen = s;
    UsbDrive d; int h = usb_drive(&d); if (h) snprintf(key, sizeof key, "usbCache_%s", d.id); else key[0] = 0;
    int dev = getenv("SB_USB_ON") != NULL; /* development: the drive counts as switched on, nothing stored */
    int want = h && (dev || !strcmp(store_get(key, "false"), "true"));
    pthread_mutex_lock(&mu); if (want != on) { on = want; serial++; seen = serial; } pthread_mutex_unlock(&mu);
    if (!h) { ask = 0; askedFor[0] = 0; update_notice_cancel(open_usb_setting); }
    else if (!dev && !store_get(key, NULL) && strcmp(askedFor, d.id)) { ask = 1; snprintf(askedFor, sizeof askedFor, "%s", d.id); fprintf(stderr, "[usb] %s never answered: the notice comes once the app is calm\n", key); } /* never answered: asked once while it stays */
    else if (!dev && strcmp(askedFor, d.id)) { snprintf(askedFor, sizeof askedFor, "%s", d.id); fprintf(stderr, "[usb] %s answered before (%s): no notice\n", key, store_get(key, "")); }
    /* the TV has a drive this app cannot reach: said once while it stays */
    char nm[64]; int u = !h && usb_unreachable(nm, sizeof nm);
    if (!u) tellJail = 0; else if (tellJail == 0) tellJail = 1;
  }
  if (ask && calm()) { ask = 0; fprintf(stderr, "[usb] notice: use the drive for torrents?\n"); update_notice(T("USB drive found"), T("Set up"), open_usb_setting); }
  if (tellJail == 1 && calm()) { tellJail = 2; update_notice(T("Restart the TV for USB"), T("Why"), open_usb_restart); } /* (like the update: from the Settings icon) */
}
void usb_shutdown(void) {
  if (running != 1) return;
  pthread_mutex_lock(&mu); quit = 1; pthread_cond_signal(&wake); pthread_mutex_unlock(&mu);
  pthread_join(th, NULL); running = 0;
}
