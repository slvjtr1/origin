/* Clean sources (Settings > Playback): what an addon lists is read before it is shown or chosen.
   Left out: a file that is not a video at all (a program, an archive, a shortcut: what fake uploads carry),
   a "video" too small to be one, and, for a series, a source that names another episode or season than the
   one opened. The same reading picks the file inside a torrent pack. Nothing here asks the network. */
#include "core/srcfilter.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int alnum_(char c) { return isalnum((unsigned char)c) != 0; }
static int dig_(char c) { return c >= '0' && c <= '9'; }
/* up to max digits at p: their value and length (0 when none, or when more digits follow) */
static int num_at(const char *p, int max, int *len) { int v = 0, n = 0; while (n < max && dig_(p[n])) v = v * 10 + (p[n++] - '0'); if (!n || dig_(p[n])) { *len = 0; return -1; } *len = n; return v; }
static void lower_(const char *s, char *o, size_t cap) { size_t i = 0; for (; s && s[i] && i + 1 < cap; i++) o[i] = (char)tolower((unsigned char)s[i]); o[i] = 0; }
static const char *skip_sep(const char *p) { while (*p == ' ' || *p == '.' || *p == '_' || *p == '-') p++; return p; }

int src_vid_episode(const char *vid, int *season, int *episode) {
  if (!vid || strncmp(vid, "tt", 2)) return 0;
  const char *a = strchr(vid, ':'); if (!a) return 0; const char *b = strchr(a + 1, ':'); if (!b || strchr(b + 1, ':')) return 0;
  for (const char *p = a + 1; p < b; p++) if (!dig_(*p)) return 0;
  for (const char *p = b + 1; *p; p++) if (!dig_(*p)) return 0;
  if (b == a + 1 || !b[1]) return 0;
  *season = atoi(a + 1); *episode = atoi(b + 1); return *season > 0 && *episode > 0;
}

/* every "s01e02", "s01e02-e04", "s01e02e03", "1x02" of the text: 1 when one of them is this episode, -1 when
   there are some and none is, 0 when the text has none */
static int exact_marks(const char *t, int S, int E) {
  int any = 0;
  for (const char *p = t; *p; p++) {
    if (p > t && alnum_(p[-1])) continue;
    int l, s, e;
    if (*p == 's' && (s = num_at(p + 1, 2, &l)) >= 0) {
      const char *q = skip_sep(p + 1 + l); if (*q != 'e' || (e = num_at(q + 1, 3, &l)) < 0) continue;
      any = 1; int lo = e, hi = e; q += 1 + l;
      for (;;) { const char *r = q; int named = 0; if (*r == '-') r++; if (*r == 'e') { r++; named = 1; } else if (r == q) break; int e2 = num_at(r, 3, &l); if (e2 < 0 || e2 < hi || e2 > hi + 60) break; if (!named && (l < 2 || alnum_(r[l]))) break; hi = e2; q = r + l; } /* "e02-e04", "e02e03", "e02-04" */
      if (s == S && E >= lo && E <= hi) return 1;
    } else if (dig_(*p) && (s = num_at(p, 2, &l)) > 0 && p[l] == 'x') {
      int l2; e = num_at(p + l + 1, 3, &l2); if (e < 0 || l2 < 2 || alnum_(p[l + 1 + l2])) continue; /* (not 1920x1080: at most two digits before the x) */
      any = 1; if (s == S && e == E) return 1;
    }
  }
  return any ? -1 : 0;
}
/* the seasons a text names without an episode ("s02", "s01-s04", "season 2", "seasons 1-4", "season 1 to 4"):
   1 when S is among them, -1 when some are named and S is not, 0 when none */
static int season_marks(const char *t, int S) {
  int any = 0;
  for (const char *p = t; *p; p++) {
    if (p > t && alnum_(p[-1])) continue;
    int l, a = -1; const char *q = NULL;
    if (!strncmp(p, "season", 6)) { q = p + 6; if (*q == 's') q++; q = skip_sep(q); a = num_at(q, 2, &l); if (a < 0) continue; q += l; }
    else if (*p == 's' && (a = num_at(p + 1, 2, &l)) >= 0) { q = p + 1 + l; if (alnum_(*q)) continue; }
    else continue;
    int b = a; const char *r = q; while (*r == ' ') r++;
    if (*r == '-' || !strncmp(r, "to ", 3) || *r == '~') { r += *r == 't' ? 3 : 1; while (*r == ' ') r++; if (*r == 's') r++; int b2 = num_at(r, 2, &l); if (b2 >= a) b = b2; }
    any = 1; if (S >= a && S <= b) return 1;
  }
  return any ? -1 : 0;
}
int src_episode_verdict(const char *text, int season, int episode) {
  if (!text || !*text || season <= 0 || episode <= 0) return 0;
  char t[1400]; lower_(text, t, sizeof t);
  int x = exact_marks(t, season, episode); if (x) return x;
  if (season_marks(t, season) < 0) return -1; /* another season's pack */
  return 0;
}
/* a file of a pack whose name carries no "s01e02": its own number ("05 - Title.mkv", "Episode 5", "ep05", "e05")
   when its folders do not name another season */
int src_episode_loose(const char *path, int season, int episode) {
  if (!path || season <= 0 || episode <= 0) return 0;
  char t[700]; lower_(path, t, sizeof t);
  if (season_marks(t, season) < 0) return 0;
  const char *b = strrchr(t, '/'); b = b ? b + 1 : t; int l, v;
  if ((v = num_at(b, 3, &l)) >= 0 && !alnum_(b[l])) return v == episode; /* "05 - Title.mkv" */
  for (const char *p = b; *p; p++) {
    if (p > b && alnum_(p[-1])) continue;
    const char *q = !strncmp(p, "episode", 7) ? p + 7 : !strncmp(p, "ep", 2) ? p + 2 : *p == 'e' ? p + 1 : NULL; if (!q) continue;
    q = skip_sep(q); if ((v = num_at(q, 3, &l)) >= 0 && !alnum_(q[l])) return v == episode;
  }
  for (const char *p = b; *p; p++) if (p[0] == ' ' && p[1] == '-' && p[2] == ' ' && (v = num_at(p + 3, 3, &l)) >= 0 && !alnum_(p[3 + l])) return v == episode; /* "Show - 05 [1080p]" */
  return 0;
}

static const char *ext_of(const char *n) { const char *d = n ? strrchr(n, '.') : NULL; if (!d || strlen(d) > 6 || strchr(d, ' ') || strchr(d, '/')) return ""; return d + 1; }
static int in_list(const char *e, const char *const *l, unsigned n) { for (unsigned i = 0; i < n; i++) if (!strcasecmp(e, l[i])) return 1; return 0; }
int src_is_video(const char *name) {
  static const char *const V[] = { "mkv", "mp4", "avi", "ts", "m4v", "webm", "mov", "m2ts", "mts", "mpg", "mpeg", "wmv", "flv", "ogv", "divx", "3gp", "vob" };
  return in_list(ext_of(name), V, sizeof V / sizeof *V);
}
/* a line of an addon's text that ends as a program or an archive does (the short list: a release name may end in ".com") */
static int line_harmful(const char *ln) {
  static const char *const X[] = { "exe", "scr", "msi", "bat", "cmd", "pif", "lnk", "vbs", "hta", "ps1", "jar", "apk", "zip", "rar", "7z", "cab", "docm" };
  return in_list(ext_of(ln), X, sizeof X / sizeof *X);
}
int src_is_harmful(const char *name) {
  static const char *const X[] = { "exe", "scr", "msi", "bat", "cmd", "com", "pif", "lnk", "vbs", "vbe", "js", "jse", "wsf", "hta", "ps1", "jar", "apk", "dll", "cpl", "reg", "dmg", "pkg", "sh", "run", "appx",
    "zip", "rar", "7z", "cab", "ace", "arj", "gz", "html", "htm", "url", "website", "docm", "xlsm" };
  return in_list(ext_of(name), X, sizeof X / sizeof *X);
}

int src_junk(const char *name, const char *title, const char *filename, double size, int series) {
  if (filename && *filename) { if (src_is_harmful(filename)) return SRC_HARMFUL; }
  char t[1400], raw[1400]; snprintf(raw, sizeof raw, "%s\n%s\n%s", name ? name : "", title ? title : "", filename ? filename : ""); lower_(raw, t, sizeof t);
  /* each line that ends as a file name does: a program or an archive is never the film */
  for (char *ln = t; ln && *ln;) { char *nl = strchr(ln, '\n'); if (nl) *nl = 0; char *e = ln + strlen(ln); while (e > ln && (e[-1] == ' ' || e[-1] == '\r')) *--e = 0;
    int bad = line_harmful(ln); if (nl) *nl = '\n'; if (bad) return SRC_HARMFUL; ln = nl ? nl + 1 : NULL; }
  if (strstr(t, "password") || strstr(t, "passw0rd") || strstr(t, "codec pack") || strstr(t, "install codec") || strstr(t, "player required") || strstr(t, "read me first") || strstr(t, "readme first")) return SRC_HARMFUL;
  if (size > 0 && size < (series ? 12.0 : 60.0) * 1048576.0) return SRC_TINY; /* (a size the addon states: far too small to be the episode or the film) */
  if (filename && *filename) { char f[300]; lower_(filename, f, sizeof f); char *d = strrchr(f, '.'); if (d) *d = 0; size_t n = strlen(f); /* the name's last word: "film-sample.mkv", "film.trailer.mp4" (a title may begin with either word) */
    if ((n > 7 && !strcmp(f + n - 6, "sample") && !alnum_(f[n - 7])) || (n > 8 && !strcmp(f + n - 7, "trailer") && !alnum_(f[n - 8]))) return SRC_NOT_IT; }
  return SRC_OK;
}
