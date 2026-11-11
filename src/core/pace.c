#include "core/pace.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double steps[4] = { 1000.0 / 48, 1000.0 / 32, 1000.0 / 24, 0 }; static int nsteps = 3, step;
static Uint64 freq, lastStart; static int fixed;
static double work[60]; static int nwork, wi; static int calm;
#define NI 512
static float iv[NI]; static int niv; static double ivWorst; static int late, lateRun, lateRunMax, stepsDown, stepsUp;

/* per tag: a 1 ms histogram up to 250 ms, the worst, late frames and runs of them */
#define NT 32
typedef struct { unsigned hist[251]; unsigned n, late, run, runMax; float worst; double sum; } TagStat;
static TagStat tagS[NT]; static int curTag;
void pace_tag(int t) { curTag = t < 0 || t >= NT ? 0 : t; }
static void tag_add(float d, double period) {
  if (curTag < 0) return; TagStat *t = &tagS[curTag]; int b = (int)(d + 0.5f); if (b > 250) b = 250; if (b < 0) b = 0;
  t->hist[b]++; t->n++; t->sum += d; if (d > t->worst) t->worst = d;
  if (d > period * 1.5) { t->late++; if (++t->run > t->runMax) t->runMax = t->run; } else t->run = 0;
}
static int pct(const unsigned *h, unsigned n, double q) { unsigned want = (unsigned)(n * q), acc = 0; for (int i = 0; i <= 250; i++) { acc += h[i]; if (acc > want) return i; } return 250; }
void pace_tag_report(const char *const *names, int n) {
  for (int i = 0; i < NT && i < n; i++) { TagStat *t = &tagS[i]; if (!t->n) continue;
    fprintf(stderr, "[stress] %-10s %6u frames | avg %.1f p50 %d p95 %d p99 %d worst %.0f ms | late %u (%.2f%%), longest run %u\n", names[i] ? names[i] : "?", t->n, t->sum / t->n, pct(t->hist, t->n, 0.5), pct(t->hist, t->n, 0.95), pct(t->hist, t->n, 0.99), t->worst, t->late, 100.0 * t->late / t->n, t->runMax); }
}
static double now_ms(void) { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)freq; }
void pace_init(void) {
  freq = SDL_GetPerformanceFrequency();
  /* the steps divide the display's own refresh (60 Hz: 60 / 30 / 20 a second): a period that does not divide it shows
     frames for one refresh, then two, unevenly (a 20.8 ms pace on a 60 Hz compositor is a 1-1-1-2 judder) */
  /* 60 a second on every panel (a 50 or 120 Hz one too): the app's motion is made for it; SB_HZ overrides (development) */
  { SDL_DisplayMode m; int hz = 0, panel = 0; if (!SDL_GetDesktopDisplayMode(0, &m)) panel = m.refresh_rate; hz = 60; if (getenv("SB_HZ")) hz = atoi(getenv("SB_HZ")); if (hz < 24 || hz > 240) hz = 60;
    fprintf(stderr, "[pace] panel %d Hz, the app at %d\n", panel, hz);
    steps[0] = 1000.0 / hz; steps[1] = 2000.0 / hz; steps[2] = 3000.0 / hz; fprintf(stderr, "[pace] display %d Hz: steps %.1f / %.1f / %.1f ms\n", hz, steps[0], steps[1], steps[2]); }
  /* the display's own rate, always: a frame that runs long costs one refresh, where stepping down for a few slow frames
     (a picture arriving, a page opening) halved the rate for seconds and showed as 60-then-30 motion. SB_PACE=auto
     brings the steps back; SB_PACE=<ms> fixes another period (development) */
  const char *p = getenv("SB_PACE");
  if (p && !strcmp(p, "auto")) {}
  else if (p && atof(p) > 0) { steps[0] = atof(p); nsteps = 1; fixed = 1; }
  else { nsteps = 1; fixed = 1; }
}
double pace_period(void) { return steps[step]; }
void pace_set_hz(int hz) { if (hz < 24 || hz > 240 || getenv("SB_HZ") || getenv("SB_PACE")) return; steps[0] = 1000.0 / hz; steps[1] = 2000.0 / hz; steps[2] = 3000.0 / hz; fprintf(stderr, "[pace] the screen takes %d frames a second: the app at %d\n", hz, hz); }
static void sleep_until(double t) {
  double d = t - now_ms();
  if (d > 2.0) SDL_Delay((Uint32)(d - 1.5));          /* the scheduler wakes late: sleep short of it, then wait the rest out */
  while (now_ms() < t) { }
}
void pace_wait(double workMs) {
  double period = steps[step];
  /* steps: three of the last 30 frames that missed the period -> the next step down at once; 2.5 s with
     the slowest 10% of frames well under the faster step -> back up */
  work[wi] = workMs; wi = (wi + 1) % 60; if (nwork < 60) nwork++;
  if (!fixed) {
    int over = 0; for (int i = 0; i < 30 && i < nwork; i++) if (work[(wi - 1 - i + 60) % 60] > period * 0.97) over++;
    if (over >= 3 && step + 1 < nsteps) { step++; stepsDown++; calm = 0; nwork = 0; }
    else if (step > 0 && nwork == 60) {
      double s[60]; memcpy(s, work, sizeof s); for (int i = 1; i < 60; i++) { double x = s[i]; int j = i - 1; while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; } s[j + 1] = x; }
      if (s[54] < steps[step - 1] * 0.75) { if (++calm >= 2500 / period) { step--; stepsUp++; calm = 0; nwork = 0; } } else calm = 0;
    }
    period = steps[step];
  }
  double start = lastStart ? (double)lastStart * 1000.0 / (double)freq : 0, due = start + period, t = now_ms();
  if (lastStart && t < due) sleep_until(due);
  else if (lastStart && t > due + period) due = t;     /* far behind (a stall): start again from now, no catching up */
  double ns = lastStart && due > now_ms() - 1 ? due : now_ms();
  Uint64 nowc = (Uint64)(ns * (double)freq / 1000.0);
  if (lastStart) {
    float d = (float)((double)(nowc - lastStart) * 1000.0 / (double)freq);
    if (niv < NI) iv[niv++] = d;
    if (d > ivWorst) ivWorst = d;
    if (d > period * 1.5f) { late++; if (++lateRun > lateRunMax) lateRunMax = lateRun; } else lateRun = 0;
    tag_add(d, period);
  }
  lastStart = nowc;
}
/* with the driver waiting for the screen (SB_VSYNC=1): the frames are timed, not paced */
void pace_observe(void) {
  if (!freq) freq = SDL_GetPerformanceFrequency();
  Uint64 nowc = SDL_GetPerformanceCounter();
  if (lastStart) { float d = (float)((double)(nowc - lastStart) * 1000.0 / (double)freq); double period = steps[0];
    if (niv < NI) iv[niv++] = d; if (d > ivWorst) ivWorst = d;
    if (d > period * 1.5f) { late++; if (++lateRun > lateRunMax) lateRunMax = lateRun; } else lateRun = 0;
    tag_add(d, period); }
  lastStart = nowc;
}
void pace_report(char *out, int cap) {
  float s[NI]; int n = niv; memcpy(s, iv, n * sizeof *s);
  for (int i = 1; i < n; i++) { float x = s[i]; int j = i - 1; while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; } s[j + 1] = x; }
  double sum = 0; for (int i = 0; i < n; i++) sum += s[i];
  snprintf(out, cap, "pace %.1f ms (%s) | period avg %.1f p50 %.1f p95 %.1f worst %.1f | late %d (longest run %d) | steps down %d up %d | %d frames",
           steps[step], fixed ? "fixed" : "auto", n ? sum / n : 0, n ? s[n / 2] : 0, n ? s[(int)(n * 0.95)] : 0, ivWorst, late, lateRunMax, stepsDown, stepsUp, n);
  niv = 0; ivWorst = 0; late = 0; lateRunMax = 0; stepsDown = stepsUp = 0;
}
