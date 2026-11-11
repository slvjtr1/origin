/* stderr becomes a pipe; a thread reads it, passes every byte on to where stderr went before (so the terminal
   and /tmp/<appid>.log read as they always did) and keeps each line scrubbed and time-stamped in applog.log in
   the data folder (400 KB, then the newer half is kept) and in memory (the last 300). A crash puts stderr back
   before the crash report is written, so the report reaches the terminal / device log even though this
   thread never sees it. */
#include "core/applog.h"
#include "core/logseal.h"
#include "core/net.h"
#include "core/update.h"
#include "platform/plat.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#if defined(__arm__) && defined(__linux__)
#include <ucontext.h>
#endif
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifndef SB_APP_ID
#define SB_APP_ID "tv.stremioorigin.app"
#endif

#define APPLOG_MAX (400 * 1024)
#define RING 300
static int teeFd = -1, fileFd = -1, pipeR = -1;
static volatile int exiting, drained;
/* this build's id: tools/elfstrip.py writes a hash of the linked binary over the dots, so a crash report names the
   exact build its addresses belong to (getlogs.js names them only from that build's symbols) */
__attribute__((used)) static const char buildId[] = "SBBUILDID:................................";
const char *applog_build_id(void) { return buildId + 10; }
static char filePath[1100], crashVer[32] = "?"; /* (the version read before any crash: nothing is read in the handler) */
static long fileSize;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char *ring[RING]; static int ringHead, ringN;

static void write_all(int fd, const char *p, size_t n) { while (n && fd >= 0) { ssize_t w = write(fd, p, n); if (w < 0) { if (errno == EINTR) continue; return; } p += w; n -= (size_t)w; } }
/* 2026-10-25T10:33:00.123Z, as new Date().toISOString() */
static void iso_now(char out[32]) {
  struct timeval tv; gettimeofday(&tv, NULL); time_t t = tv.tv_sec; struct tm tm; gmtime_r(&t, &tm);
  snprintf(out, 32, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
}
static void rotate(void) {
  FILE *f = fopen(filePath, "rb"); if (!f) return;
  fseek(f, 0, SEEK_END); long n = ftell(f); long from = n - APPLOG_MAX / 2; if (from < 0) from = 0;
  char *b = malloc((size_t)(n - from) + 1); fseek(f, from, SEEK_SET); size_t g = fread(b, 1, (size_t)(n - from), f); fclose(f);
  size_t k = 0; if (from > 0) { while (k < g && b[k] != '\n') k++; if (k < g) k++; } /* from the next whole line */
  char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", filePath);
  FILE *o = fopen(tmp, "wb"); if (o) { fwrite(b + k, 1, g - k, o); fclose(o); rename(tmp, filePath); }
  free(b);
  close(fileFd); fileFd = open(filePath, O_WRONLY | O_CREAT | O_APPEND, 0644); fileSize = (long)(g - k);
}
static void take_line(const char *line) {
  if (!*line) return;
  char *s = logscrub_line(line), ts[32]; iso_now(ts);
  size_t n = strlen(ts) + 1 + strlen(s) + 1; char *e = malloc(n + 1); snprintf(e, n + 1, "%s %s\n", ts, s); free(s);
  write_all(fileFd, e, n); fileSize += (long)n;
  if (fileSize > APPLOG_MAX) rotate();
  e[n - 1] = 0;
  pthread_mutex_lock(&mu);
  if (ringN == RING) { free(ring[ringHead]); ring[ringHead] = e; ringHead = (ringHead + 1) % RING; }
  else ring[(ringHead + ringN++) % RING] = e;
  pthread_mutex_unlock(&mu);
}
static void *reader(void *arg) {
  (void)arg; static char buf[8192], line[4096]; size_t ln = 0;
  for (;;) {
    struct pollfd p = { pipeR, POLLIN, 0 }; int r = poll(&p, 1, exiting ? 0 : 250);
    if (r <= 0) { if (exiting) break; continue; }
    ssize_t n = read(pipeR, buf, sizeof buf);
    if (n == 0) break; if (n < 0) { if (errno == EAGAIN || errno == EINTR) continue; break; }
    write_all(teeFd, buf, (size_t)n);
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n' || ln == sizeof line - 1) { line[ln] = 0; take_line(line); ln = 0; if (buf[i] == '\n') continue; }
      line[ln++] = buf[i];
    }
  }
  drained = 1; return NULL;
}
/* the normal end: what is still in the pipe reaches the terminal before the process goes */
static void at_exit(void) {
  if (teeFd < 0) return; fflush(stderr); dup2(teeFd, 2); exiting = 1;
  for (int i = 0; i < 100 && !drained; i++) usleep(5000);
}
/* ---- crashes: stderr straight to its old place, the pipe's rest after it, then the app's own handler ---- */
static const int SIGS[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGALRM };
static struct sigaction oldAct[sizeof SIGS / sizeof SIGS[0]];
static void drain_now(void) {
  char b[4096]; ssize_t n;
  dup2(teeFd, 2);
  while ((n = read(pipeR, b, sizeof b)) > 0) write_all(teeFd, b, (size_t)n);
}
static void on_signal(int sig, siginfo_t *si, void *uc) {
  drain_now();
  if (fileFd >= 0) { /* a mark in the kept log: the next share shows that the app died here */
    char m[64] = "[crash] signal "; size_t k = strlen(m); char d[8]; int nd = 0, v = sig; do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 8);
    while (nd) m[k++] = d[--nd]; m[k++] = '\n'; write_all(fileFd, m, k);
#if defined(__arm__) && defined(__linux__)
    { /* where: the version, the address, the code and its caller, the code addresses found on the stack (the calls
         that led there, for tools/getlogs.js to name), and where the app's own libraries were loaded */
      ucontext_t *u = uc; char b[600]; int n = snprintf(b, sizeof b, "[crash] version %s build %.12s addr %p pc %p lr %p\n", crashVer, buildId + 10, si ? si->si_addr : NULL, (void *)u->uc_mcontext.arm_pc, (void *)u->uc_mcontext.arm_lr); if (n > 0) write_all(fileFd, b, (size_t)n);
      extern char __executable_start[], etext[]; unsigned long lo = (unsigned long)__executable_start, hi = (unsigned long)etext;
      const unsigned long *sp = (const unsigned long *)u->uc_mcontext.arm_sp; int k2 = snprintf(b, sizeof b, "[crash] stack"), got = 0;
      for (int w = 0; w < 256 && got < 24 && k2 < (int)sizeof b - 12; w++) { unsigned long v = sp[w]; if (v >= lo && v < hi && (v & 1) == 0) { k2 += snprintf(b + k2, sizeof b - k2, " %lx", v); got++; } }
      b[k2++] = '\n'; write_all(fileFd, b, (size_t)k2);
      int mf = open("/proc/self/maps", O_RDONLY); if (mf >= 0) { static char mb[4096]; static char ln[512]; size_t ll = 0; ssize_t r; /* line by line, the whole file */
        while ((r = read(mf, mb, sizeof mb)) > 0) for (ssize_t q = 0; q < r; q++) {
          if (mb[q] != '\n') { if (ll < sizeof ln - 1) ln[ll++] = mb[q]; continue; }
          ln[ll] = 0; if (strstr(ln, "r-xp") && (strstr(ln, "stremioorigin") || strstr(ln, "libsbengine") || strstr(ln, "libsbdts") || strstr(ln, "libc-") || strstr(ln, "libstdc"))) { ln[ll] = '\n'; write_all(fileFd, "[crash] map ", 12); write_all(fileFd, ln, ll + 1); } ll = 0; }
        close(mf); }
      write_all(fileFd, "[crash] end\n", 12); }
#endif
  }
  for (size_t i = 0; i < sizeof SIGS / sizeof SIGS[0]; i++) if (SIGS[i] == sig) {
    struct sigaction *o = &oldAct[i];
    if (o->sa_flags & SA_SIGINFO) { if (o->sa_sigaction) o->sa_sigaction(sig, si, uc); }
    else if (o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN) o->sa_handler(sig);
    signal(sig, SIG_DFL); raise(sig); /* the app's handler came back (or there was none): the default end */
  }
}
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SB_ASAN_ON 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(SB_ASAN_ON)
void __sanitizer_set_death_callback(void (*cb)(void));
static void on_asan_death(void) { drain_now(); } /* its report went into the pipe: hand it on before the exit */
#endif

void applog_start(const char *dataDir) {
  if (teeFd >= 0) return;
  snprintf(crashVer, sizeof crashVer, "%s", update_current());
  snprintf(filePath, sizeof filePath, "%s/applog.log", dataDir);
  fileFd = open(filePath, O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fileFd < 0 && (errno == EACCES || errno == EPERM)) { unlink(filePath); fileFd = open(filePath, O_WRONLY | O_CREAT | O_APPEND, 0644); } /* left by another user (a development run as root): a new one, or nothing would be kept, a crash's mark included */
  if (fileFd >= 0) { struct stat st; if (!fstat(fileFd, &st)) fileSize = (long)st.st_size; if (fileSize > APPLOG_MAX) rotate(); }
  int unclean = 0; /* did the last session end with its own exit line? (the TV ends an app for memory without a word to it) */
  { int fd = open(filePath, O_RDONLY); if (fd >= 0) { static char t[16385]; off_t n = lseek(fd, 0, SEEK_END), from = n > 16384 ? n - 16384 : 0; lseek(fd, from, SEEK_SET); ssize_t g = read(fd, t, 16384); close(fd);
      if (g > 0) { t[g] = 0; char *st = NULL; for (char *q = t; (q = strstr(q, "[start] ")); q++) st = q; if (st && !strstr(st, "[exit]") && !strstr(st, "[crash]")) unclean = 1; } } }
  int p[2]; fflush(stderr);
  if (pipe(p)) return;
  teeFd = dup(2); if (teeFd < 0) { close(p[0]); close(p[1]); return; }
  fcntl(teeFd, F_SETFD, FD_CLOEXEC); fcntl(p[0], F_SETFD, FD_CLOEXEC); if (fileFd >= 0) fcntl(fileFd, F_SETFD, FD_CLOEXEC);
  fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
#ifdef F_SETPIPE_SZ
  fcntl(p[1], F_SETPIPE_SZ, 1 << 20); /* room for bursts while the thread writes the file: the main thread must never wait on a log line */
#endif
  pipeR = p[0]; dup2(p[1], 2); close(p[1]);
  setvbuf(stderr, NULL, _IOLBF, 0);
  pthread_t t; if (pthread_create(&t, NULL, reader, NULL)) { dup2(teeFd, 2); return; } pthread_detach(t);
  for (size_t i = 0; i < sizeof SIGS / sizeof SIGS[0]; i++) {
    struct sigaction o; sigaction(SIGS[i], NULL, &o); oldAct[i] = o;
    if (SIGS[i] == SIGALRM && !(o.sa_flags & SA_SIGINFO) && (o.sa_handler == SIG_DFL || o.sa_handler == SIG_IGN)) continue; /* nobody listens for it */
    struct sigaction a; memset(&a, 0, sizeof a); a.sa_sigaction = on_signal; a.sa_flags = SA_SIGINFO; sigemptyset(&a.sa_mask); sigaction(SIGS[i], &a, NULL);
  }
#if defined(__SANITIZE_ADDRESS__) || defined(SB_ASAN_ON)
  __sanitizer_set_death_callback(on_asan_death);
#endif
  atexit(at_exit);
  if (unclean) fprintf(stderr, "[start] the last session ended without its exit line: the TV ended it (memory) or it stopped answering (its last [mem] lines above say how big it was)\n");
}

/* ---- the bundle ---- */
typedef struct { char *p; size_t n, cap; } Buf;
static void b_add(Buf *b, const char *s) { size_t l = strlen(s); if (b->n + l + 1 > b->cap) { b->cap = (b->n + l + 1) * 2 + 256; b->p = realloc(b->p, b->cap); } memcpy(b->p + b->n, s, l + 1); b->n += l; }
static char *read_file(const char *p, size_t max) { FILE *f = fopen(p, "rb"); if (!f) return NULL; char *b = malloc(max + 1); size_t n = fread(b, 1, max, f); b[n] = 0; fclose(f); return b; }
static void json_str(const char *j, const char *key, char *out, size_t cap) { /* a flat "key": "value" from a small system file */
  out[0] = 0; char k[64]; snprintf(k, sizeof k, "\"%s\"", key); const char *p = j ? strstr(j, k) : NULL; if (!p) return;
  p = strchr(p + strlen(k), ':'); if (!p) return; p = strchr(p, '"'); if (!p) return; p++;
  size_t i = 0; while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++; out[i] = 0;
}
static long proc_kb(const char *file, const char *key) {
  char *t = read_file(file, 16384); if (!t) return -1; char *p = strstr(t, key); long v = p ? atol(p + strlen(key)) : -1; free(t); return v;
}
char *applog_bundle(void) {
  Buf b = { 0 }; char ts[32], l[512]; iso_now(ts);
  snprintf(l, sizeof l, "Origin logs - %s\n", ts); b_add(&b, l);
  snprintf(l, sizeof l, "app %s %s (build %.12s)", SB_APP_ID, update_current(), buildId + 10); b_add(&b, l);
  char *os = read_file("/var/run/nyx/os_info.json", 8192), *di = read_file("/var/run/nyx/device_info.json", 8192);
  if (os) { char r[64], c[64], n[64], bid[64]; json_str(os, "webos_release", r, sizeof r); json_str(os, "core_os_release", c, sizeof c); json_str(os, "webos_name", n, sizeof n); json_str(os, "webos_build_id", bid, sizeof bid);
    snprintf(l, sizeof l, "\nwebOS %s (%s) %s", r[0] ? r : c[0] ? c : "?", n, bid); b_add(&b, l); }
  else { snprintf(l, sizeof l, "\n%s", plat_device_name()); b_add(&b, l); }
  if (di) { char m[96], h[96]; json_str(di, "device_name", m, sizeof m); json_str(di, "hardware_id", h, sizeof h); snprintf(l, sizeof l, "\nmodel %s", m[0] ? m : h); b_add(&b, l); }
  free(os); free(di);
  long rss = proc_kb("/proc/self/status", "VmRSS:"), avail = proc_kb("/proc/meminfo", "MemAvailable:");
  char rs[24] = "?", av[24] = "?"; if (rss >= 0) snprintf(rs, sizeof rs, "%ld", rss / 1024); if (avail >= 0) snprintf(av, sizeof av, "%ld", avail / 1024);
  snprintf(l, sizeof l, "\nnative · app %s MB · TV free %s MB · up %u s", rs, av, (unsigned)(SDL_GetTicks() / 1000)); b_add(&b, l);
  b_add(&b, "\n\n== app ==\n");
  /* the last 400 lines of the kept file (each scrubbed again, as the helper did), else the ones in memory */
  char *f = fileFd >= 0 ? read_file(filePath, APPLOG_MAX + 65536) : NULL, *p = NULL; int lines = 0; /* (a file it could not write to: the lines in memory) */
  long lastCrash = -1; if (f) { for (char *q = f; (q = strstr(q, "[crash] signal")); q++) lastCrash = q - f; } /* (found before the lines below are cut up) */
  if (f && *f) { size_t n = strlen(f); p = f + n; while (p > f && lines <= 400) { p--; if (*p == '\n') lines++; } if (*p == '\n') p++;
    for (char *s = p; *s;) { char *e = strchr(s, '\n'); if (e) *e = 0; if (*s) { char *c = logscrub_line(s); b_add(&b, c); b_add(&b, "\n"); free(c); } if (!e) break; s = e + 1; } }
  else { pthread_mutex_lock(&mu); for (int i = 0; i < ringN; i++) { b_add(&b, ring[(ringHead + i) % RING]); b_add(&b, "\n"); } pthread_mutex_unlock(&mu); }
  /* the last crash, with the lines before it, when it is further back than the lines above (the app was opened again
     and used before the logs were shared: its report must still be there) */
  if (f && *f && lastCrash >= 0) { char *c = f + lastCrash;
    if (c < p) { char *from = c; int back = 0; while (from > f && back <= 60) { from--; if (*from == '\n') back++; } if (*from == '\n') from++;
      char *to = strstr(c, "[crash] end"); to = to ? strchr(to, '\n') : NULL; if (!to) { to = c; for (int q = 0; q < 40 && to && *to; q++) { to = strchr(to, '\n'); if (to) to++; } }
      b_add(&b, "\n== the last crash ==\n"); for (char *s = from; *s && (!to || s < to);) { char *e = strchr(s, '\n'); if (e) *e = 0; if (*s) { char *cl = logscrub_line(s); b_add(&b, cl); b_add(&b, "\n"); free(cl); } if (!e) break; s = e + 1; } } }
  free(f);
  /* what the TV itself said about this app: its memory manager's lists (which apps were biggest when it ended one)
     and the launches (an "autoReload" is the TV starting it again after ending it) */
  char *m = read_file("/var/log/messages", 262144);
  if (m) { b_add(&b, "\n== system ==\n"); const char *lns[60]; int k = 0;
    for (char *s = m; *s;) { char *e = strchr(s, '\n'); if (e) *e = 0;
      if (strstr(s, SB_APP_ID) || strstr(s, "stremioorigin") || strstr(s, "memchute") || strstr(s, "Out of memory") || strstr(s, "Killed process")) { lns[k % 60] = s; k++; }
      if (!e) break; s = e + 1; }
    for (int i = k > 60 ? k - 60 : 0; i < k; i++) { char *c = logscrub_line(lns[i % 60]); b_add(&b, c); b_add(&b, "\n"); free(c); }
    free(m); }
  return b.p;
}

/* ---- Share logs: the bundle without secrets, sealed to the developer's public key below (only the matching
   private key, which never leaves the developer's computer, opens it) and left on telegra.ph (a free public pages
   service, no account) as a page named after a 4-character code and the day. The code finds the page and works for
   a week; opened again within 10 minutes, the same code. ---- */
static const char *LS_PUB = "BE8DKk7oGRNeZ8RBX+7M+4/FTgH+r+pMSrKlZtowApPZkgh3p4bE8f6qwJvxZx7gQMs7UewGl6BJx35yrcF8+uQ="; /* this fork's maintainer */
static struct { int running, state; double at; char out[64], code[5], *page; } SH; /* state: 0 working, 1 ok, -1 failed; touched under mu */
static void share_set(int st, const char *out) { pthread_mutex_lock(&mu); SH.state = st; snprintf(SH.out, sizeof SH.out, "%s", out); SH.running = 0; free(SH.page); SH.page = NULL; pthread_mutex_unlock(&mu); }
static const char *api_base(void) { const char *r = getenv("SB_LOGS_RELAY"); return r ? r : "https://api.telegra.ph"; } /* development: a local stand-in */
/* application/x-www-form-urlencoded, as querystring.stringify */
static void form_add(Buf *b, const char *k, const char *v) {
  if (b->n) b_add(b, "&"); b_add(b, k); b_add(b, "=");
  char e[4]; for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr("-_.!~*'()", *p)) { e[0] = (char)*p; e[1] = 0; }
    else snprintf(e, sizeof e, "%%%02X", *p);
    b_add(b, e); }
}
static cJSON *api_result(int status, const unsigned char *body) { /* j.ok ? j.result : null */
  cJSON *j = status == 200 ? cJSON_Parse((const char *)body) : NULL; cJSON *r = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "ok")) ? cJSON_DetachItemFromObjectCaseSensitive(j, "result") : NULL;
  cJSON_Delete(j); return r;
}
static void api_post(const char *method, const char *form, NetCb cb) {
  char url[256]; snprintf(url, sizeof url, "%s/%s", api_base(), method);
  net_send("POST", url, "Content-Type: application/x-www-form-urlencoded", form, strlen(form), 30000, cb, NULL);
}
static void on_page(int status, const unsigned char *body, size_t len, void *ud) {
  (void)len; (void)ud; cJSON *r = api_result(status, body); const cJSON *p = cJSON_GetObjectItemCaseSensitive(r, "path");
  if (cJSON_IsString(p) && *p->valuestring) { fprintf(stderr, "[logs] shared\n"); share_set(1, SH.code); }
  else { fprintf(stderr, "[logs] share failed: %s (%d)\n", r ? "relay" : "net", status); share_set(-1, r ? "relay" : "net"); }
  cJSON_Delete(r);
}
static void on_account(int status, const unsigned char *body, size_t len, void *ud) { /* a throwaway author for this one page */
  (void)len; (void)ud; cJSON *r = api_result(status, body); const cJSON *t = cJSON_GetObjectItemCaseSensitive(r, "access_token");
  if (!cJSON_IsString(t) || !*t->valuestring) { cJSON_Delete(r); fprintf(stderr, "[logs] share failed: net (%d)\n", status); share_set(-1, "net"); return; }
  char title[16], *content; snprintf(title, sizeof title, "sbl %s", SH.code);
  size_t cl = strlen(SH.page) + 64; content = malloc(cl); snprintf(content, cl, "[{\"tag\":\"p\",\"children\":[\"%s\"]}]", SH.page); /* base64 needs no JSON escaping */
  Buf f = { 0 }; form_add(&f, "access_token", t->valuestring); form_add(&f, "title", title); form_add(&f, "content", content); free(content); cJSON_Delete(r);
  api_post("createPage", f.p, on_page); free(f.p);
}
static void *share_job(void *arg) {
  (void)arg; char *b = applog_bundle(), *t = logscrub_share(b); free(b);
  unsigned char pub[65]; char *page = logseal_unb64(LS_PUB, pub, sizeof pub) == 65 ? logseal_seal(SH.code, pub, t, strlen(t)) : NULL; free(t);
  if (!page) { share_set(-1, "seal"); return NULL; }
  pthread_mutex_lock(&mu); SH.page = page; pthread_mutex_unlock(&mu);
  Buf f = { 0 }; form_add(&f, "short_name", "Origin"); api_post("createAccount", f.p, on_account); free(f.p);
  return NULL;
}
int applog_share_start(void) {
  pthread_mutex_lock(&mu);
  int busy = SH.running, fresh = SH.state == 1 && SDL_GetTicks() / 1000.0 - SH.at < 600;
  pthread_mutex_unlock(&mu);
  if (busy || fresh) return 1; /* the running share answers this one too; opened again soon after: the same code */
  char code[5]; if (!logseal_code(code)) { share_set(-1, "seal"); return 1; }
  pthread_mutex_lock(&mu); SH.running = 1; SH.state = 0; SH.out[0] = 0; memcpy(SH.code, code, 5); SH.at = SDL_GetTicks() / 1000.0; pthread_mutex_unlock(&mu);
  fprintf(stderr, "[logs] sharing\n"); fflush(stderr); /* the latest lines reach the kept file first */
  pthread_t th; if (pthread_create(&th, NULL, share_job, NULL)) { share_set(-1, "seal"); return 1; }
  pthread_detach(th); return 1;
}
int applog_share_state(char *out, size_t cap) {
  pthread_mutex_lock(&mu); int st = SH.state; if (out) snprintf(out, cap, "%s", SH.out); pthread_mutex_unlock(&mu); return st;
}
