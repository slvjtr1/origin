/* core/p2pdirs.c: the live folder is never removed; a retired folder goes at the engine's stop unless it is live again;
   renamed left-overs are swept; many threads moving the window around while the stop runs never remove the live one. */
#include "common.h"
#include "core/p2pdirs.h"
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

static char base[600];
static void mk(const char *name, char *out) { sprintf(out, "%s/%s", base, name); mkdir(out, 0755); char f[800]; sprintf(f, "%s/piece", out); FILE *x = fopen(f, "wb"); if (x) { fputs("data", x); fclose(x); } }
static int there(const char *d) { struct stat st; return !stat(d, &st); }
static void mk_in(const char *d) { mkdir(d, 0755); char f[800]; sprintf(f, "%s/piece", d); FILE *x = fopen(f, "wb"); if (x) { fputs("data", x); fclose(x); } }
static int has_piece(const char *d) { char f[800]; sprintf(f, "%s/piece", d); return there(f); }

static int stop, moves; static char A[700], B[700];
static void *mover(void *a) { (void)a; int k = 0; while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) { const char *to = k & 1 ? A : B, *from = k & 1 ? B : A; p2pdirs_set_live(to); mk_in(to); p2pdirs_retire(from); k++; /* (as the engine: its folder made, and written, once it is live) */ __atomic_store_n(&moves, k, __ATOMIC_RELEASE); } return NULL; }

int main(int argc, char **argv) {
  snprintf(base, sizeof base, "%s", argc > 1 ? argv[1] : "."); char C[700], D[700];
  mk("flash-engine", A); mk("usb-engine", B);
  /* the window moves from A to B: A retired, B live; the engine stops: both go */
  p2pdirs_set_live(A); p2pdirs_set_live(B); p2pdirs_retire(A);
  CHECK(there(A)); CHECK(p2pdirs_stopped(B) == 2); CHECK(!there(A)); CHECK(!there(B));
  /* A retired, then the window comes back to A (a drive plugged in again): A is live, never removed */
  mk("flash-engine", A); mk("usb-engine", B);
  p2pdirs_set_live(B); p2pdirs_retire(A); p2pdirs_set_live(A); p2pdirs_retire(B);
  CHECK(p2pdirs_stopped(NULL) == 1); CHECK(there(A)); CHECK(!there(B));
  { char l[700]; CHECK(p2pdirs_live(l, sizeof l) && !strcmp(l, A)); }
  CHECK(p2pdirs_stopped(A) == 1); CHECK(!there(A));
  /* retiring the live folder does nothing; retiring twice keeps one entry */
  mk("x-engine", C); p2pdirs_set_live(C); p2pdirs_retire(C); CHECK(p2pdirs_stopped(NULL) == 0); CHECK(there(C));
  mk("y-engine", D); p2pdirs_retire(D); p2pdirs_retire(D); CHECK(p2pdirs_stopped(C) == 2); CHECK(!there(C)); CHECK(!there(D));
  /* a folder already gone (a drive pulled out): nothing to do, no harm */
  p2pdirs_retire(A); CHECK(p2pdirs_stopped(NULL) == 0);
  /* left-overs renamed by an earlier run are swept, the root itself is not touched by the sweep */
  { mk("z-engine", C); char o1[800], o2[800]; sprintf(o1, "%s.old-1-1", C); sprintf(o2, "%s.old-2-7", C); mkdir(o1, 0755); mkdir(o2, 0755);
    p2pdirs_sweep_old(C); CHECK(!there(o1)); CHECK(!there(o2)); CHECK(there(C)); p2pdirs_rm_tree(C); rmdir(C); }
  /* many moves while stops run: what the engine wrote in its live folder is never removed */
  for (int round = 0; round < 20; round++) {
    mk("flash-engine", A); mk("usb-engine", B); __atomic_store_n(&stop, 0, __ATOMIC_RELEASE); __atomic_store_n(&moves, 0, __ATOMIC_RELEASE); pthread_t th; pthread_create(&th, NULL, mover, NULL);
    while (__atomic_load_n(&moves, __ATOMIC_ACQUIRE) < 2) host_sleep_ms(1); /* (the mover running before the stops begin) */
    int m0 = __atomic_load_n(&moves, __ATOMIC_ACQUIRE); for (int i = 0; i < 30; i++) { p2pdirs_stopped(NULL); }
    while (__atomic_load_n(&moves, __ATOMIC_ACQUIRE) < m0 + 50) host_sleep_ms(1); /* (moves meanwhile, interleaved with the stops) */
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE); pthread_join(th, NULL);
    char l[700]; p2pdirs_live(l, sizeof l); CHECKF(has_piece(l), "round %d: what was written in the live folder %s is there", round, l);
    p2pdirs_set_live(""); p2pdirs_stopped(NULL); p2pdirs_rm_tree(A); rmdir(A); p2pdirs_rm_tree(B); rmdir(B);
  }
  return host_done("p2pdirs");
}
