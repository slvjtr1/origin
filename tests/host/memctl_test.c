/* core/memctl.c with a stand-in C library: trims run on the worker, spaced, one at a time, with the reason copied;
   the worker ends at shutdown even while a trim runs; a trim that gives back little makes the next one wait. */
#include "common.h"
#include "core/memctl.h"
#include <pthread.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include "memctl_test_shim.h"

static double t0;
Uint32 SDL_GetTicks(void) { return (Uint32)((host_now() - t0) * 1000) + 100000; }
void plat_thread_background(int n) { (void)n; }
static int rss = 300;
void plat_mem_self(int *r, int *s) { *r = __atomic_load_n(&rss, __ATOMIC_ACQUIRE); *s = 0; }
int plat_mem_free_mb(void) { return 200; }
void net_mem_stats(size_t *u, size_t *p, size_t *b, unsigned *r) { if (u) *u = 0; if (p) *p = 0; if (b) *b = 0; if (r) *r = 0; }

static int trims, inTrim, overlap, arena; static int trimMs = 50, give = 30;
int mallopt(int p, int v) { if (p == M_ARENA_MAX) arena = v; return 1; }
int malloc_trim(size_t pad) { (void)pad; if (__atomic_add_fetch(&inTrim, 1, __ATOMIC_ACQ_REL) > 1) overlap = 1; host_sleep_ms(trimMs); __atomic_sub_fetch(&rss, give, __ATOMIC_ACQ_REL); __atomic_add_fetch(&trims, 1, __ATOMIC_ACQ_REL); __atomic_sub_fetch(&inTrim, 1, __ATOMIC_ACQ_REL); return 1; }
struct mallinfo mallinfo(void) { struct mallinfo m; memset(&m, 0, sizeof m); m.arena = rss << 20; return m; }
static void pump_for(double s) { double e = host_now() + s; while (host_now() < e) { mem_pump(); mem_frame(16); host_sleep_ms(5); } }
static int ntrims(void) { return __atomic_load_n(&trims, __ATOMIC_ACQUIRE); }

int main(void) {
  t0 = host_now();
  mem_tune(); CHECK(arena == 4);
  { char why[32]; strcpy(why, "a film covers the pages"); mem_trim_soon(0.1, why); strcpy(why, "OVERWRITTEN"); /* (the reason is the module's copy) */
    pump_for(0.6); CHECK(ntrims() == 1); }
  /* asked again at once: not before 3 s after the last */
  mem_trim_soon(0, "the film closed"); pump_for(1.0); CHECK(ntrims() == 1); pump_for(2.6); CHECK(ntrims() == 2);
  /* a lean trim (under 2 MB back): the next waits 20 s, unless the TV is short */
  give = 0; mem_trim_soon(0, "lean"); pump_for(3.3); CHECK(ntrims() == 3); pump_for(0.3);
  mem_trim_soon(0, "after a lean one"); pump_for(3.5); CHECK(ntrims() == 3);
  mem_trim_soon(0, "the TV is short of memory"); pump_for(0.5); CHECKF(ntrims() == 4, "%d", ntrims());
  CHECK(!overlap);
  /* shutdown while a trim runs: it finishes, the worker ends, nothing hangs */
  give = 30; trimMs = 400; pump_for(3.1); mem_trim_soon(0, "the TV is short of memory"); pump_for(0.1);
  CHECK(mem_trimming()); double s = host_now(); mem_shutdown(); CHECKF(host_now() - s < 1.0, "%.2f s", host_now() - s); CHECK(!mem_trimming()); CHECK(ntrims() == 5);
  mem_shutdown(); /* (twice: harmless) */
  return host_done("memctl");
}
