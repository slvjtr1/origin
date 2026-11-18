/* Stats for nerds (Settings > About): a small panel in the top corner, over everything, with where the memory and the
   time go: the app's RAM against the TV's, what the pictures and the GPU's textures hold, the cache on disk and the
   room left there, the frames (rate, typical and slow ones, late ones, the CPU per frame) and, while something plays,
   what plays and how. The numbers are read once a second; the disk is summed on a thread of its own every 15 s. */
#include "ui/nerds.h"
#include "ui/gfx.h"
#include "ui/text.h"
#include "ui/img.h"
#include "ui/asssubs.h"
#include "core/app.h"
#include "core/store.h"
#include "core/net.h"
#include "core/update.h"
#include "screens/player.h"
#include "platform/plat.h"
#include "platform/mem.h"
#include "core/best.h"
#include "core/keys.h"
#include "ui/widgets.h"
#include <math.h>
#include <SDL2/SDL.h>
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define NL 26
static char lines[NL][2][120]; static unsigned char kind[NL]; static int nlines; /* kind: 0 a row, 1 a section's name, 2 the buffer graph */
static double cpuSum; static int cpuN, lastCalls, lastQuads; static Uint32 readAt;
static volatile long long diskData, diskFree, diskTotal; static volatile int diskBusy; static Uint32 diskAt;
#define BH 60
static float bufHist[BH]; static int bufN, bufI; static float bufNow = -1; /* the seconds buffered ahead, once a second, the last minute */

int nerds_on(void) { return !strcmp(store_get("nerdStats", "false"), "true"); }
#define FR 240
static float fr[FR]; static int frN, frI; static Uint64 frLast; /* the last frames' periods, timed here (the same on every device) */
void nerds_frame(float cpuMs, int calls, int quads) {
  cpuSum += cpuMs; cpuN++; lastCalls = calls; lastQuads = quads;
  Uint64 n = SDL_GetPerformanceCounter(); if (frLast) { fr[frI] = (float)((double)(n - frLast) * 1000.0 / (double)SDL_GetPerformanceFrequency()); frI = (frI + 1) % FR; if (frN < FR) frN++; } frLast = n;
}
static void frames_live(float *fps, float *p50, float *p95, float *worst, float *latePct) {
  float s[FR]; int n = frN, lt = 0; double sum = 0; float w = 0; for (int i = 0; i < n; i++) { s[i] = fr[i]; sum += s[i]; if (s[i] > w) w = s[i]; if (s[i] > 25.0f) lt++; } /* late: a frame past 1.5 periods at 60 */
  for (int i = 1; i < n; i++) { float x = s[i]; int j = i - 1; while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; } s[j + 1] = x; }
  *fps = n && sum > 0 ? (float)(1000.0 * n / sum) : 0; *p50 = n ? s[n / 2] : 0; *p95 = n ? s[(int)(n * 0.95)] : 0; *worst = w; *latePct = n ? 100.0f * lt / n : 0;
}

static long long dir_bytes(const char *p, int depth) { /* everything under the app's data folder (cache, state, logs) */
  DIR *d = opendir(p); if (!d) return 0; long long sum = 0; struct dirent *e; char q[1400]; struct stat st;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; snprintf(q, sizeof q, "%s/%s", p, e->d_name);
    if (lstat(q, &st)) continue; if (S_ISDIR(st.st_mode)) { if (depth < 4) sum += dir_bytes(q, depth + 1); } else sum += st.st_size; }
  closedir(d); return sum;
}
static void *disk_job(void *a) {
  (void)a; plat_thread_background(10); const char *dd = plat_data_dir();
  long long b = dir_bytes(dd, 0); struct statvfs v; long long fr = -1, tot = -1;
  if (!statvfs(dd, &v)) { fr = (long long)v.f_bavail * v.f_frsize; tot = (long long)v.f_blocks * v.f_frsize; }
  diskData = b; diskFree = fr; diskTotal = tot; diskBusy = 0; return NULL;
}
static const char *sz(long long b, char *o, size_t cap) { /* MB, or GB with a decimal from 10 GB */
  if (b >= 10240LL << 20) snprintf(o, cap, "%.1f GB", b / 1073741824.0); else snprintf(o, cap, "%lld MB", b >> 20); return o; }
static void add(const char *k, const char *fmt, ...) {
  if (nlines >= NL) return; kind[nlines] = 0; snprintf(lines[nlines][0], 120, "%s", k);
  va_list ap; va_start(ap, fmt); vsnprintf(lines[nlines][1], 120, fmt, ap); va_end(ap); nlines++;
}
static void head(const char *name) { if (nlines >= NL) return; kind[nlines] = 1; snprintf(lines[nlines][0], 120, "%s", name); lines[nlines][1][0] = 0; nlines++; }
static void graph(void) { if (nlines >= NL) return; kind[nlines] = 2; lines[nlines][0][0] = lines[nlines][1][0] = 0; nlines++; }
/* while something plays: what plays and from where first, the buffer's last minute, then the TV in a few lines */
static void refresh_player(void) {
  nlines = 0; head("PLAYBACK");
  char pl[12][120]; int n = player_nerd_lines(pl, 12);
  for (int i = 0; i < n; i++) { char *sp = strstr(pl[i], "  "); if (sp) { *sp = 0; add(pl[i], "%s", sp + 2); } else add("", "%s", pl[i]); }
  double b = player_nerd_buffer(); bufNow = (float)b; if (b >= 0) { bufHist[bufI] = (float)b; bufI = (bufI + 1) % BH; if (bufN < BH) bufN++; graph(); }
  head("THIS TV");
  int rss, swp; plat_mem_self(&rss, &swp); int tot = plat_mem_total_mb(), fr = plat_mem_free_mb();
  if (tot > 0) add("memory", "app %d MB  \xc2\xb7  %d MB free of %d MB", rss, fr, tot); else add("memory", "app %d MB  \xc2\xb7  %d MB free", rss, fr);
  float fps, p50, p95, worst, late; frames_live(&fps, &p50, &p95, &worst, &late);
  add("frames", "%.0f fps  \xc2\xb7  %.1f %% late  \xc2\xb7  worst %.0f ms", fps, late, worst); cpuSum = 0; cpuN = 0;
  add("app", "%s", update_current());
}
static void refresh(void) {
  if (player_open()) { refresh_player(); return; }
  bufN = bufI = 0; bufNow = -1;
  nlines = 0;
  int rss, swp; plat_mem_self(&rss, &swp); int tot = plat_mem_total_mb(), fr = plat_mem_free_mb();
  char totS[24]; if (tot > 0) snprintf(totS, sizeof totS, "%d MB", tot); else snprintf(totS, sizeof totS, "?");
  add("RAM", "app %d MB of %s", rss, totS);
  if (swp > 0) add("swap", "%d MB of the app's", swp);
  add("TV free", "%d MB", fr);
  add("pictures", "%d of %d MB", img_used_mb(), img_budget_mb());
  long tb, pb; int tn; gfx_mem(&tb, &tn, &pb); add("GPU", "%ld MB in %d textures (%ld kept)", tb >> 20, tn, pb >> 20);
  if (diskData >= 0 && diskTotal > 0) { char a[24], f[24], t[24]; add("storage", "%s the app's · %s free of %s", sz(diskData, a, sizeof a), sz(diskFree, f, sizeof f), sz(diskTotal, t, sizeof t)); }
  else add("storage", "reading…");
  float fps, p50, p95, worst, late; frames_live(&fps, &p50, &p95, &worst, &late);
  add("frames", "%.0f fps · %.1f / %.1f ms · worst %.0f", fps, p50, p95, worst);
  add("late", "%.1f %% of the last 240", late);
  add("work", "%.1f ms a frame · %d calls · %d quads", cpuN ? cpuSum / cpuN : 0, lastCalls, lastQuads); cpuSum = 0; cpuN = 0;
  static const char *SN[] = { "sign-in", "home", "discover", "library", "title", "similar", "search", "settings" };
  add("screen", "%s · %d requests", (unsigned)SCR < 8 ? SN[SCR] : "?", net_pending());
  Uint32 up = SDL_GetTicks() / 1000; add("app", "%s · up %u:%02u:%02u", update_current(), up / 3600, up / 60 % 60, up % 60);
}
/* the buffer's last minute as thin columns, the newest on the right, each coloured by how safe it was (20 s and more,
   8 s and more, less); the figure now at the left, large */
static uint32_t buf_col(float s) { return best_health_color(s >= 20 ? BEST_H_GOOD : s >= 8 ? BEST_H_FAIR : BEST_H_POOR); }
#define GH 74.0f
static void graph_draw(float x, float y, float w) {
  char now[24]; snprintf(now, sizeof now, "%.0f s", bufNow > 0 ? bufNow : 0); uint32_t c = buf_col(bufNow);
  text_draw("buffer", x, y + 4, 19, 600, WHITE_A(140), 0, 0, ALIGN_LEFT); text_draw(now, x, y + 30, 32, 800, c, -0.5f, 0, ALIGN_LEFT);
  float gx = x + 120, gw = w - 120, gh = GH - 12, mx = 30; for (int i = 0; i < bufN; i++) if (bufHist[i] > mx) mx = bufHist[i];
  gfx_rrect(gx, y + 4, gw, gh, 8, WHITE_A(10));
  float cw = gw / BH; for (int i = 0; i < bufN; i++) { float v = bufHist[(bufI - bufN + i + BH) % BH], hh = fmaxf(2, (gh - 6) * v / mx);
    gfx_rrect(gx + gw - (bufN - i) * cw + 1, y + 4 + gh - 3 - hh, fmaxf(1, cw - 2), hh, 1.5f, col_alpha(buf_col(v), i == bufN - 1 ? 1 : 0.62f)); }
  { char m[16]; snprintf(m, sizeof m, "%.0f s", mx); text_draw(m, gx + 8, y + 7, 15, 600, WHITE_A(110), 0, 0, ALIGN_LEFT); } }
void nerds_draw(void) {
  static float a; float want = nerds_on() ? 1 : 0; a += (want - a) * fminf(1, app_dt() * 12); if (want && a > 0.995f) a = 1; if (!want && a < 0.01f) { a = 0; return; } /* (in and out over a few frames, never a pop) */
  Uint32 now = SDL_GetTicks();
  if (want && !diskBusy && (!diskAt || now - diskAt > 15000)) { diskAt = now; diskBusy = 1; diskData = diskData ? diskData : -1; pthread_t t; if (pthread_create(&t, NULL, disk_job, NULL)) diskBusy = 0; else pthread_detach(t); }
  static int wasPlayer; int pl = player_open(); if (want && (!readAt || now - readAt >= 1000 || pl != wasPlayer)) { readAt = now; wasPlayer = pl; refresh(); }
  const float PAD = 22, LH = 30, HH = 38; float W = pl ? 660 : 560, LW = pl ? 130 : 120, h = PAD * 2 + 34, x, y = pl ? 40 : 104;
  for (int i = 0; i < nlines; i++) h += kind[i] == 1 ? HH : kind[i] == 2 ? GH + 10 : LH;
  int rtl = app_rtl(); x = pl ? (rtl ? SB_W - 40 - W : 40) : (rtl ? 40 : SB_W - 40 - W); /* (in the player: the free top corner, away from its clock and its trays) */
  gfx_opacity_push(a); gfx_offset_push(0, -10 * (1 - a));
  gfx_shadow(x, y, W, h, 22, 30, BLACK_A(120)); gfx_rrect(x, y, W, h, 22, RGBA(10, 10, 16, 246)); gfx_ring(x, y, W, h, 22, 1, WHITE_A(36));
  text_draw(pl ? "PLAYBACK INFO" : "STATS FOR NERDS", x + PAD, y + PAD, 17, 800, ACC3, 1.6f, 0, ALIGN_LEFT);
  if (pl) key_hint(x + W - PAD, y + PAD + 10, K_BLUE, "Close", WHITE_A(150), 1);
  float ly = y + PAD + 34;
  for (int i = 0; i < nlines; i++) {
    if (kind[i] == 1) { if (i) gfx_rect(x + PAD, ly + 8, W - 2 * PAD, 1, WHITE_A(26)); text_draw(lines[i][0], x + PAD, ly + 16, 14, 800, WHITE_A(120), 1.8f, 0, ALIGN_LEFT); ly += HH; continue; }
    if (kind[i] == 2) { graph_draw(x + PAD, ly, W - 2 * PAD); ly += GH + 10; continue; }
    text_draw(lines[i][0], x + PAD, ly, 19, 600, WHITE_A(140), 0, LW - 8, ALIGN_LEFT);
    text_draw(lines[i][1], x + PAD + LW, ly, 19, 500, WHITE_A(240), 0, W - 2 * PAD - LW, ALIGN_LEFT); ly += LH; }
  gfx_offset_pop(); gfx_opacity_pop();
}
