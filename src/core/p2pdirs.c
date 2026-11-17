/* see p2pdirs.h */
#include "core/p2pdirs.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define NRET 8
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char liveDir[1024], retired[NRET][1024]; static int nretired; static unsigned seq; /* under mu */

void p2pdirs_rm_tree(const char *dir) {
  DIR *d = opendir(dir); if (!d) return; struct dirent *e; char p[1400];
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st; if (lstat(p, &st)) continue; if (S_ISDIR(st.st_mode)) { p2pdirs_rm_tree(p); rmdir(p); } else unlink(p); }
  closedir(d);
}
int p2pdirs_live(char *out, unsigned cap) { pthread_mutex_lock(&mu); int has = liveDir[0] != 0; if (out && cap) snprintf(out, cap, "%s", liveDir); pthread_mutex_unlock(&mu); return has; }
void p2pdirs_set_live(const char *d) {
  pthread_mutex_lock(&mu); snprintf(liveDir, sizeof liveDir, "%s", d ? d : "");
  for (int i = 0; i < nretired; i++) if (!strcmp(retired[i], liveDir)) { memmove(retired[i], retired[i + 1], (size_t)(nretired - i - 1) * sizeof retired[0]); nretired--; i--; }
  pthread_mutex_unlock(&mu);
}
void p2pdirs_retire(const char *d) {
  if (!d || !*d) return; pthread_mutex_lock(&mu);
  int dup = !strcmp(d, liveDir); for (int i = 0; i < nretired && !dup; i++) if (!strcmp(retired[i], d)) dup = 1;
  if (!dup) { if (nretired == NRET) { memmove(retired[0], retired[1], (NRET - 1) * sizeof retired[0]); nretired--; fprintf(stderr, "[p2p] more than %d folders retired: the oldest is left to the next start's sweep\n", NRET); }
    snprintf(retired[nretired++], sizeof retired[0], "%s", d); }
  pthread_mutex_unlock(&mu);
}
/* d removed when it is not live (renamed first, under the lock: from then on nobody can make it live by its name); 1 when it was */
static int dir_remove(const char *d) {
  if (!d || !*d) return 0; char trash[1100]; struct stat st;
  pthread_mutex_lock(&mu);
  if (!strcmp(d, liveDir)) { pthread_mutex_unlock(&mu); return 0; }
  if (lstat(d, &st)) { pthread_mutex_unlock(&mu); return 0; } /* (not there: a drive pulled out) */
  snprintf(trash, sizeof trash, "%s.old-%d-%u", d, (int)getpid(), ++seq);
  int moved = !rename(d, trash);
  if (!moved) { p2pdirs_rm_tree(d); rmdir(d); pthread_mutex_unlock(&mu); return 1; } /* (cannot be renamed: emptied in place, still under the lock, so it cannot become live meanwhile) */
  pthread_mutex_unlock(&mu);
  p2pdirs_rm_tree(trash); rmdir(trash); return 1;
}
int p2pdirs_stopped(const char *dir) {
  char list[NRET + 1][1024]; int n = 0;
  pthread_mutex_lock(&mu);
  if (dir && !strcmp(dir, liveDir)) liveDir[0] = 0;
  for (int i = 0; i < nretired; i++) snprintf(list[n++], sizeof list[0], "%s", retired[i]); nretired = 0;
  pthread_mutex_unlock(&mu);
  if (dir && *dir) snprintf(list[n++], sizeof list[0], "%s", dir);
  int k = 0; for (int i = 0; i < n; i++) k += dir_remove(list[i]);
  return k;
}
void p2pdirs_sweep_old(const char *root) {
  char par[1024]; snprintf(par, sizeof par, "%s", root); char *sl = strrchr(par, '/'); if (!sl || sl == par) return; char base[1024]; snprintf(base, sizeof base, "%s", sl + 1); *sl = 0;
  size_t bl = strlen(base); DIR *d = opendir(par); if (!d) return; struct dirent *e; char p[2200];
  while ((e = readdir(d))) if (!strncmp(e->d_name, base, bl) && !strncmp(e->d_name + bl, ".old-", 5)) { snprintf(p, sizeof p, "%s/%s", par, e->d_name); p2pdirs_rm_tree(p); rmdir(p); }
  closedir(d);
}
