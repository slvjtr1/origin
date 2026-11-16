/* see memctl.h */
#include "core/memctl.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdio.h>
#if defined(__GLIBC__) && !defined(SB_ANDROID)
#include <malloc.h>
#define SB_TRIM 1
#endif

static double trimAt; static const char *trimWhy = ""; static volatile int trimming; static double lastTrim = -1000;
static double now_s(void) { return SDL_GetTicks() / 1000.0; }

void mem_tune(void) {
#ifdef SB_TRIM
  /* - at most 4 heaps for all the threads (the default is 8 per core count: each keeps its own freed space);
     - a block of 256 KB or more is its own mapping, gone the moment it is freed (a decoded picture, an addon's
       answer, a subtitle file), and that size never creeps up as the library would otherwise let it;
     - the main heap's free top goes back once it passes 512 KB */
  mallopt(M_ARENA_MAX, 4);
  mallopt(M_MMAP_THRESHOLD, 256 * 1024);
  mallopt(M_TRIM_THRESHOLD, 512 * 1024);
  mallopt(M_TOP_PAD, 128 * 1024);
#endif
}

#ifdef SB_TRIM
static void *trim_thread(void *a) {
  const char *why = a; plat_thread_background(10);
  int r0, s0, r1, s1; plat_mem_self(&r0, &s0); Uint32 t0 = SDL_GetTicks();
  malloc_trim(0);
  plat_mem_self(&r1, &s1);
  fprintf(stderr, "[mem] handed back: app %d -> %d MB in %u ms (%s), TV free %d MB\n", r0, r1, SDL_GetTicks() - t0, why, plat_mem_free_mb());
  trimming = 0; return NULL;
}
#endif

void mem_trim_soon(double afterSec, const char *why) {
  double at = now_s() + (afterSec > 0 ? afterSec : 0);
  if (trimAt > 0 && trimAt <= at) return; /* one is due sooner already */
  trimAt = at; trimWhy = why ? why : "";
}

void mem_pump(void) {
  if (trimAt <= 0) return; double t = now_s(); if (t < trimAt) return;
  if (trimming) return;                       /* the last one still runs: this one right after it */
  if (t - lastTrim < 3) { trimAt = lastTrim + 3; return; } /* never back to back */
  trimAt = 0; lastTrim = t;
#ifdef SB_TRIM
  trimming = 1; pthread_t th;
  if (pthread_create(&th, NULL, trim_thread, (void *)trimWhy)) trimming = 0; else pthread_detach(th);
#endif
}
