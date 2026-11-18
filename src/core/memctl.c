/* see memctl.h */
#include "core/memctl.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include "core/net.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SB_MEMCTL_TEST)
#include "memctl_test_shim.h" /* (tests/host: a stand-in for the C library's calls, to run the worker on any computer) */
#define SB_TRIM 1
#elif defined(__GLIBC__)
#include <malloc.h>
#define SB_TRIM 1
#endif

#define GAP_S 3.0        /* never two trims closer than this */
#define LEAN_GAP_S 20.0  /* after a trim that gave back under LEAN_MB: the next one waits this long (unless the TV is short) */
#define LEAN_MB 2

/* main thread only */
static double trimAt, lastTrim = -1000, gap = GAP_S; static char trimWhy[64]; static int urgent;
static double worstMs; static int framesDuring;
static double now_s(void) { return SDL_GetTicks() / 1000.0; }

/* shared with the worker, under mu */
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int quit, want, busy, workerState; /* workerState: 0 not started, 1 running, -1 could not start or ended */
static char jobWhy[64];
static struct { int done, r0, r1, freeMb; unsigned ms; long heapBefore, heapAfter; char why[64]; } res;
static pthread_t worker;

int mem_trimming(void) { pthread_mutex_lock(&mu); int b = busy; pthread_mutex_unlock(&mu); return b; }

void mem_tune(void) {
#ifdef SB_TRIM
  if (getenv("SB_MALLOC_DEFAULT")) { fprintf(stderr, "[mem] C library left as it is (SB_MALLOC_DEFAULT)\n"); return; }
  mallopt(M_ARENA_MAX, 4);
#endif
}

#ifdef SB_TRIM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations" /* (mallinfo2 is newer than the TV's C library) */
static long heap_kb(void) { struct mallinfo m = mallinfo(); return ((long)(unsigned)m.arena + (long)(unsigned)m.hblkhd) / 1024; } /* what the heaps and the large blocks hold (mapped, used or not) */
#pragma GCC diagnostic pop
static void *trim_main(void *a) {
  (void)a; plat_thread_background(10);
  pthread_mutex_lock(&mu);
  for (;;) {
    while (!quit && !want) pthread_cond_wait(&cv, &mu);
    if (quit) break;
    want = 0; busy = 1; char why[64]; memcpy(why, jobWhy, sizeof why);
    pthread_mutex_unlock(&mu);
    int r0, s0, r1, s1; plat_mem_self(&r0, &s0); long h0 = heap_kb(); Uint32 t0 = SDL_GetTicks();
    malloc_trim(0);
    unsigned ms = SDL_GetTicks() - t0; long h1 = heap_kb(); plat_mem_self(&r1, &s1); int fr = plat_mem_free_mb();
    pthread_mutex_lock(&mu);
    busy = 0; res.done = 1; res.r0 = r0; res.r1 = r1; res.ms = ms; res.freeMb = fr; res.heapBefore = h0; res.heapAfter = h1; memcpy(res.why, why, sizeof res.why);
  }
  workerState = -1; pthread_mutex_unlock(&mu);
  return NULL;
}
#endif

void mem_trim_soon(double afterSec, const char *why) {
  double at = now_s() + (afterSec > 0 ? afterSec : 0);
  if (why && (strstr(why, "short") || strstr(why, "tight"))) urgent = 1; /* the TV running short: the lean-trim wait does not apply */
  if (trimAt > 0 && trimAt <= at) return; /* one is due sooner already */
  trimAt = at; snprintf(trimWhy, sizeof trimWhy, "%s", why ? why : "");
}

void mem_frame(double ms) { if (mem_trimming()) { framesDuring++; if (ms > worstMs) worstMs = ms; } }

void mem_pump(void) {
#ifdef SB_TRIM
  { int done = 0; __typeof__(res) r; pthread_mutex_lock(&mu); if (res.done) { done = 1; r = res; res.done = 0; } pthread_mutex_unlock(&mu);
    if (done) {
      int gave = r.r0 - r.r1; gap = gave < LEAN_MB ? LEAN_GAP_S : GAP_S;
      fprintf(stderr, "[mem] handed back: app %d -> %d MB, heaps %ld -> %ld MB, in %u ms (%s); %d frame(s) meanwhile, the longest %.0f ms; TV free %d MB\n",
        r.r0, r.r1, r.heapBefore / 1024, r.heapAfter / 1024, r.ms, r.why, framesDuring, worstMs, r.freeMb);
      framesDuring = 0; worstMs = 0; } }
  if (trimAt <= 0) return; double t = now_s(); if (t < trimAt) return;
  if (mem_trimming()) return;                                  /* the last one still runs: this one right after it */
  double g = urgent ? GAP_S : gap;
  if (t - lastTrim < g) { trimAt = lastTrim + g; return; }     /* spaced */
  pthread_mutex_lock(&mu);
  if (workerState == 0) { if (pthread_create(&worker, NULL, trim_main, NULL)) { workerState = -1; fprintf(stderr, "[mem] no thread for handing memory back: not done\n"); } else workerState = 1; }
  int ok = workerState == 1;
  if (ok) { want = 1; snprintf(jobWhy, sizeof jobWhy, "%s", trimWhy); pthread_cond_signal(&cv); }
  pthread_mutex_unlock(&mu);
  trimAt = 0; lastTrim = t; urgent = 0; framesDuring = 0; worstMs = 0;
#else
  trimAt = 0;
#endif
}

void mem_note(const char *what) {
  static double lastAt = -1; double t = now_s(); if (lastAt >= 0 && t - lastAt < 0.25) return; lastAt = t;
  int rss = -1, swp = -1; plat_mem_self(&rss, &swp); size_t nu = 0, np = 0; net_mem_stats(&nu, &np, NULL, NULL);
  fprintf(stderr, "[mem] %s: app %d MB, TV free %d MB, answers in hand %u KB (most %u KB)\n", what ? what : "", rss, plat_mem_free_mb(), (unsigned)(nu >> 10), (unsigned)(np >> 10));
}
void mem_shutdown(void) {
#ifdef SB_TRIM
  pthread_mutex_lock(&mu); int st = workerState; quit = 1; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu);
  if (st == 1) pthread_join(worker, NULL);
#endif
}
