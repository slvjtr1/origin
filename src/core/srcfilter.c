/* Clean sources (Settings > Playback): what an addon lists is read before it is shown or chosen.
   Left out: a file that is not a video at all (a program, an archive, a shortcut: what fake uploads carry),
   a "video" too small to be one, and, for a series, a source that names another episode or season than the
   one opened. The same reading picks the file inside a torrent pack. Super clean also reads the title each source
   names (further down). Nothing here asks the network. */
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

/* every "s01e02", "s01e02-e04", "s01e02e03", "1x02", "season 1 episode 2" of the text: 1 when one of them is this
   episode, -1 when there are some and none is, 0 when the text has none. A: the episode counted from the series'
   first one (0 unknown): "s01e45" is then the 5th of season 3 too, as releases that never start over number it.
   *s1: every mark found was of season 1 (that way of numbering) */
static int exact_marks(const char *t, int S, int E, int A, int *s1) {
  int any = 0, all1 = 1;
  for (const char *p = t; *p; p++) {
    if (p > t && alnum_(p[-1])) continue;
    int l, s, e;
    if (!strncmp(p, "season", 6)) { const char *q = skip_sep(p + 6); if ((s = num_at(q, 2, &l)) < 0) continue; q = skip_sep(q + l);
      const char *r = !strncmp(q, "episode", 7) ? q + 7 : !strncmp(q, "ep", 2) ? q + 2 : NULL; if (!r) continue; r = skip_sep(r); if ((e = num_at(r, 3, &l)) < 0) continue;
      any = 1; if (s != 1) all1 = 0; if ((s == S && e == E) || (A > 0 && s == 1 && S > 1 && e == A)) return 1; continue; }
    if (*p == 's' && (s = num_at(p + 1, 2, &l)) >= 0) {
      const char *q = skip_sep(p + 1 + l); if (*q != 'e' || (e = num_at(q + 1, 4, &l)) < 0) continue;
      any = 1; if (s != 1) all1 = 0; int lo = e, hi = e; q += 1 + l;
      for (;;) { const char *r = q; int named = 0; if (*r == '-') r++; if (*r == 's' && r > q) { int l2, s2 = num_at(r + 1, 2, &l2); if (s2 == s && r[1 + l2] == 'e') r += 1 + l2; } if (*r == 'e') { r++; named = 1; } else if (r == q) break; int e2 = num_at(r, 4, &l); if (e2 < 0 || e2 < hi || e2 > hi + 60) break; if (!named && (l < 2 || alnum_(r[l]))) break; hi = e2; q = r + l; } /* "e02-e04", "e02e03", "e02-04", "e01-s01e10" */
      if (s == S && E >= lo && E <= hi) return 1;
      if (A > 0 && s == 1 && S > 1 && A >= lo && A <= hi) return 1;
    } else if (dig_(*p) && (s = num_at(p, 2, &l)) > 0 && p[l] == 'x') {
      int l2; e = num_at(p + l + 1, 3, &l2); if (e < 0 || l2 < 2 || alnum_(p[l + 1 + l2])) continue; /* (not 1920x1080: at most two digits before the x) */
      any = 1; if (s != 1) all1 = 0; if (s == S && e == E) return 1;
    }
  }
  if (s1) *s1 = any && all1;
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
int src_episode_verdict_abs(const char *text, int season, int episode, int absEp, int *s1) {
  if (s1) *s1 = 0; if (!text || !*text || season <= 0 || episode <= 0) return 0;
  char t[1700]; lower_(text, t, sizeof t);
  int x = exact_marks(t, season, episode, absEp, s1); if (x) return x;
  if (season_marks(t, season) < 0) return -1; /* another season's pack */
  return 0;
}
int src_episode_verdict(const char *text, int season, int episode) { return src_episode_verdict_abs(text, season, episode, 0, NULL); }
/* a file of a pack whose name carries no "s01e02": its own number ("05 - Title.mkv", "Episode 5", "ep05", "e05")
   when its folders do not name another season */
int src_episode_loose(const char *path, int season, int episode) { return src_episode_loose_abs(path, season, episode, 0); }
/* absEp: a pack numbered from the series' first episode ("Show - 045.mkv" in no season's folder) */
int src_episode_loose_abs(const char *path, int season, int episode, int absEp) {
  if (!path || season <= 0 || episode <= 0) return 0;
  char t[700]; lower_(path, t, sizeof t);
  if (absEp > 0 && season > 1 && absEp != episode && !season_marks(t, season)) { int a = src_episode_loose_abs(path, 1, absEp, 0); if (a) return 1; }
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

/* ---- Super clean: the title a source names ----------------------------------------------------------------------
   A source's lines (its torrent's name, its file's name, the file its address ends with) are read as a release is
   named: the words before the first thing that is not the title (S01E02, a year, 1080p, BluRay...). Those words are
   then set against the title opened: the same words name it; the same words with more around them name another title
   (a spin-off, a sequel: "Fear the Walking Dead", "John Wick Chapter 2"); the name of another real title that shares
   its words (told by TMDB) names that one; a film's line with another year is another film. A line in another
   language, or one that says nothing, is left alone: only what plainly names something else goes. */
typedef struct { char w[14][28]; int n; } Tk;
typedef struct { char t[40][28]; int n; } Raw;
static char fold_c3(unsigned char b) { /* À..ÿ as their plain letter (release names drop the accents) */
  static const char M[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyts" "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if (b < 0x80 || b > 0xbf) return 0; char c = M[b - 0x80]; return c == '/' ? 0 : c;
}
static void raw_push(Raw *r, char *cur, int *l) { if (*l) { cur[*l] = 0; if (r->n < 40) memcpy(r->t[r->n++], cur, (size_t)*l + 1); *l = 0; } }
static void raw_tokens(const char *s, const char *end, Raw *r) {
  r->n = 0; char cur[28]; int l = 0;
  for (const unsigned char *p = (const unsigned char *)s; (const char *)p < end && *p;) {
    unsigned char c = *p;
    if (c < 0x80) {
      if (isalnum(c)) { if (l < 27) cur[l++] = (char)tolower(c); }
      else if (c == '\'') { int k = 0; while (isalpha(p[1 + k])) k++; if (k > 2 || p[1 + k] >= 0x80) raw_push(r, cur, &l); } /* marvel's, don't: one word; d'Amelie: two */
      else if (c == '&') { raw_push(r, cur, &l); if (r->n < 40) strcpy(r->t[r->n++], "and"); }
      else raw_push(r, cur, &l);
      p++; continue; }
    if (c == 0xc3 && p[1]) { char f = fold_c3(p[1]); if (f) { if (l < 27) cur[l++] = f; } else raw_push(r, cur, &l); p += 2; continue; }
    if (c == 0xe2 && p[1] == 0x80 && p[2] == 0x99) { p += 3; continue; } /* ’ */
    int k = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
    int letter = (c >= 0xc4 && c <= 0xdf) || c == 0xe0 || c == 0xe1 || (c >= 0xe3 && c <= 0xed); /* another script's letters; symbols and emoji part the words */
    if (!letter) raw_push(r, cur, &l);
    for (int i = 0; i < k && (const char *)p < end && *p; i++, p++) if (letter && l < 27) cur[l++] = (char)*p;
  }
  raw_push(r, cur, &l);
}
static int all_dig(const char *t) { if (!*t) return 0; for (; *t; t++) if (!dig_(*t)) return 0; return 1; }
static int is_year(const char *t) { return strlen(t) == 4 && all_dig(t) && ((t[0] == '1' && t[1] == '9') || (t[0] == '2' && t[1] == '0')); }
static int ep_token(const char *t) { /* s01, s01e02, e02, ep02, 1x02 */
  const char *p = t;
  if (*p == 's' && dig_(p[1])) { p++; int n = 0; while (dig_(*p)) { p++; n++; } if (!*p) return n <= 2; return *p == 'e' && dig_(p[1]); }
  if (*p == 'e' && dig_(p[1])) { p++; int n = 0; while (dig_(*p)) { p++; n++; } return !*p && n >= 2 && n <= 4; }
  if (p[0] == 'e' && p[1] == 'p' && dig_(p[2])) return all_dig(p + 2);
  if (dig_(*p)) { int n = 0; while (dig_(*p)) { p++; n++; } if (n <= 2 && *p == 'x' && dig_(p[1]) && dig_(p[2])) return all_dig(p + 1); }
  return 0;
}
static int word_in(const char *t, const char *const *l, unsigned n) { for (unsigned i = 0; i < n; i++) if (!strcmp(t, l[i])) return 1; return 0; }
static int stop_token(const char *t) {
  static const char *const S[] = { "season", "seasons", "saison", "temporada", "stagione", "staffel", "sezon", "episode", "episodes", "complete", "completa", "integrale", "aka",
    "480p", "540p", "576p", "720p", "1080p", "1080i", "1440p", "2160p", "4320p", "4k", "8k", "uhd", "fhd", "bluray", "blu", "bdrip", "brrip", "bdremux", "bdmv", "webrip", "webdl", "web", "hdtv", "pdtv", "sdtv",
    "dvdrip", "dvd", "dvdscr", "hdrip", "hdcam", "camrip", "cam", "hdts", "telesync", "hdtc", "remux", "x264", "x265", "h264", "h265", "hevc", "avc", "av1", "xvid", "divx", "10bit", "8bit", "hdr", "hdr10", "dv", "dovi",
    "aac", "ac3", "eac3", "dts", "ddp", "dd5", "atmos", "truehd", "flac", "opus", "proper", "repack", "rerip", "extended", "unrated", "uncut", "uncensored", "remastered", "theatrical", "imax", "internal", "limited",
    "multi", "dual", "dubbed", "subbed", "vostfr", "nf", "amzn", "dsnp", "hmax", "atvp", "hulu", "mkv", "mp4", "avi", "3d",
    "german", "french", "italian", "spanish", "latino", "castellano", "english", "eng", "ita", "fre", "ger", "spa", "rus", "hindi", "tamil", "telugu", "korean", "japanese", "dub", "sub", "subs", "esub", "msub", "nordic", "swedish", "danish", "polish" };
  return ep_token(t) || word_in(t, S, sizeof S / sizeof *S);
}
/* a title's words as they are compared: no "the", numbers as digits */
static void tk_norm(const Raw *r, int from, int to, Tk *t) {
  static const char *const NW[][2] = { { "one", "1" }, { "two", "2" }, { "three", "3" }, { "four", "4" }, { "five", "5" }, { "six", "6" }, { "seven", "7" }, { "eight", "8" }, { "nine", "9" }, { "ten", "10" },
    { "ii", "2" }, { "iii", "3" }, { "iv", "4" }, { "vi", "6" }, { "vii", "7" }, { "viii", "8" }, { "ix", "9" } };
  t->n = 0;
  for (int i = from; i < to && t->n < 14; i++) { const char *w = r->t[i];
    if (!strcmp(w, "the") || !strcmp(w, "a") || !strcmp(w, "an") || !strcmp(w, "and")) continue;
    for (unsigned k = 0; k < sizeof NW / sizeof *NW; k++) if (!strcmp(w, NW[k][0])) { w = NW[k][1]; break; }
    snprintf(t->w[t->n++], 28, "%s", w); }
}
static void tk_name(const char *name, Tk *t) { Raw r; raw_tokens(name, name + strlen(name), &r); tk_norm(&r, 0, r.n, t); }
static int tk_eq(const Tk *a, const Tk *b) { if (a->n != b->n || !a->n) return 0; for (int i = 0; i < a->n; i++) if (strcmp(a->w[i], b->w[i])) return 0; return 1; }
static int tk_find(const Tk *hay, const Tk *nd) { if (!nd->n || nd->n > hay->n) return -1; for (int i = 0; i + nd->n <= hay->n; i++) { int k = 0; while (k < nd->n && !strcmp(hay->w[i + k], nd->w[k])) k++; if (k == nd->n) return i; } return -1; }
/* one line as a release name: its title's words and its year; 0 when it does not read as one */
static int rel_title(const char *s, const char *end, Tk *t, int *year) {
  *year = 0; t->n = 0;
  while (s < end && (*s == ' ' || *s == '\t')) s++;
  while (s < end && *s == '[') { const char *c = memchr(s, ']', (size_t)(end - s)); if (!c) break; s = c + 1; while (s < end && (*s == ' ' || *s == '-' || *s == '_' || *s == '.')) s++; } /* [the group] first */
  Raw r; raw_tokens(s, end, &r); int from = 0;
  if (r.n > 2 && !strcmp(r.t[0], "www")) { from = r.n; for (int i = 1; i < r.n && i < 5; i++) if (strlen(r.t[i]) <= 4 && !dig_(r.t[i][0]) && i >= 2) { from = i + 1; break; } } /* "www.site.org - Title" */
  int stop = -1; for (int i = from; i < r.n; i++) if (stop_token(r.t[i])) { stop = i; break; }
  int lim = stop >= 0 ? stop : r.n, yi = -1; for (int i = from + 1; i < lim; i++) if (is_year(r.t[i])) yi = i;
  if (stop < 0 && yi < 0) return 0;
  if (yi >= 0) { *year = atoi(r.t[yi]); lim = yi; }
  tk_norm(&r, from, lim, t); return t->n > 0;
}
static int tk_acronym(const char *name, const Tk *r) {
  if (r->n != 1 || strlen(r->w[0]) < 3) return 0; Raw w; raw_tokens(name, name + strlen(name), &w); if (w.n < 3) return 0;
  char a[42] = "", b[42] = ""; int la = 0, lb = 0;
  for (int i = 0; i < w.n; i++) { a[la++] = w.t[i][0]; if (strcmp(w.t[i], "the") && strcmp(w.t[i], "a") && strcmp(w.t[i], "an") && strcmp(w.t[i], "and")) b[lb++] = w.t[i][0]; }
  a[la] = b[lb] = 0; return !strcmp(a, r->w[0]) || (lb >= 3 && !strcmp(b, r->w[0]));
}
static int ascii_tok(const char *t) { for (; *t; t++) if ((unsigned char)*t >= 0x80) return 0; return 1; }
/* the words a release has around the title: 1 they change nothing ("US", a network's name, a year, a translation
   beside it), 0 a collection of several titles, -1 another title */
static int extras_kind(const Tk *r, int at, int n) {
  static const char *const OK[] = { "us", "uk", "au", "nz", "ca", "tv", "show", "series", "miniseries", "mini", "marvel", "marvels", "dc", "dcs", "star", "wars", "tom", "clancys", "stephen", "kings", "disney", "disneys",
    "netflix", "bbc", "hbo", "amc", "nbc", "abc", "cbs", "fox", "itv", "pbs", "anime", "pixar", "natgeo", "movie", "film", "official" };
  static const char *const PACK[] = { "collection", "trilogy", "duology", "quadrilogy", "tetralogy", "pentalogy", "hexalogy", "anthology", "saga", "pack", "movies", "films", "universe", "franchise", "boxset", "box", "set", "all", "parts" };
  int other = 0, nums = 0;
  for (int i = 0; i < r->n; i++) { if (i >= at && i < at + n) continue; const char *w = r->w[i];
    if (word_in(w, PACK, sizeof PACK / sizeof *PACK)) return 0;
    if (is_year(w) || !ascii_tok(w) || word_in(w, OK, sizeof OK / sizeof *OK)) continue;
    if (all_dig(w) && strlen(w) <= 2) { nums++; continue; }
    other++; }
  if (nums >= 2 && !other) return 0; /* "John Wick 1 4": the lot */
  return other || nums ? -1 : 1;
}
enum { TV_NONE = 0, TV_YES = 1, TV_WEAK = -1, TV_STRONG = -2 };
static int line_title(const char *s, const char *end, const SrcWant *w) {
  Tk r; int yr; if (!rel_title(s, end, &r, &yr)) return TV_NONE;
  Tk ry = r; if (yr && ry.n < 14) snprintf(ry.w[ry.n++], 28, "%d", yr); /* a title that ends in a number read as a year ("Wonder Woman 1984") */
  int verdict = TV_NONE, named = 0;
  for (int i = 0; i < w->nname; i++) { Tk n; tk_name(w->name[i], &n); if (!n.n) continue;
    if (tk_eq(&ry, &n) && yr) return TV_YES;
    if (tk_eq(&r, &n)) { named = 1;
      if (yr && w->year && !w->series && abs(yr - w->year) >= 2) { verdict = TV_STRONG; continue; } /* the film of that name made another year */
      if (yr && w->year && w->series && (yr < w->year - 1 || (w->yearEnd && yr > w->yearEnd + 1))) { verdict = TV_WEAK; continue; } /* the series of that name from another time */
      return TV_YES; }
    if (tk_acronym(w->name[i], &r)) return TV_YES;
    int at = tk_find(&r, &n);
    if (at >= 0) { int k = extras_kind(&r, at, n.n); if (k > 0) return TV_YES; if (k < 0 && verdict > TV_WEAK) verdict = TV_WEAK; else if (!k) named = 1; continue; }
    at = tk_find(&n, &r);
    if (at >= 0) { /* fewer words than the title: its second half ("Andor" of "Star Wars: Andor"), or it without its owner ("Daredevil" of "Marvel's Daredevil") */
      const char *c = strstr(w->name[i], ": "); if (!c) c = strstr(w->name[i], " - "); if (c) { Tk b; tk_name(c + 2, &b); if (tk_eq(&r, &b)) return TV_YES; }
      if (at + r.n == n.n && at > 0 && (strstr(w->name[i], "'s ") || strstr(w->name[i], "\xe2\x80\x99s "))) return TV_YES;
      named = 1; } }
  for (int i = 0; i < w->nother; i++) { Tk o; tk_name(w->other[i], &o); if (!o.n || !(tk_eq(&r, &o) || tk_eq(&ry, &o))) continue;
    int same = 0; for (int k = 0; k < w->nname; k++) { Tk n; tk_name(w->name[k], &n); if (tk_eq(&n, &o)) same = 1; }
    if (!same) return TV_STRONG; /* another real title's own name */
    if (yr && w->oyear[i] && abs(yr - w->oyear[i]) <= 1 && w->year && abs(yr - w->year) >= 2) return TV_STRONG; } /* the same name, the other one's year */
  if (verdict != TV_NONE) return verdict;
  if (!named && !w->series && yr && w->year && abs(yr - w->year) >= 2) return TV_WEAK; /* a film's line with another film's year */
  return TV_NONE;
}
int src_title_verdict(const char *text, const SrcWant *w) {
  if (!text || !w || !w->nname) return 0; int weak = 0, strong = 0;
  for (const char *p = text; *p;) { const char *e = p; while (*e && *e != '\n' && *e != '/' && *e != '|' && *e != '\\') e++;
    int v = line_title(p, e, w); if (v == TV_YES) return 1; if (v == TV_STRONG) strong = 1; else if (v == TV_WEAK) weak = 1;
    p = *e ? e + 1 : e; }
  return strong ? -2 : weak ? -1 : 0;
}
/* a recording made in a cinema */
int src_is_cam(const char *text) {
  if (!text) return 0; static const char *const C[] = { "cam", "camrip", "hdcam", "hdts", "telesync", "hdtc", "telecine", "hdtelesync", "tsrip" };
  for (const char *p = text; *p;) { const char *e = p; while (*e && *e != '\n') e++; Raw r; raw_tokens(p, e, &r);
    for (int i = 1; i < r.n; i++) { if (word_in(r.t[i], C, sizeof C / sizeof *C)) return 1; if (!strcmp(r.t[i], "ts") && i + 1 < r.n && (is_year(r.t[i - 1]) || stop_token(r.t[i + 1]))) return 1; } /* ("ts" as a word of the release, not the file's ending) */
    p = *e ? e + 1 : e; }
  return 0;
}
/* the file a direct address ends with ("…/The.Show.S01E02.mkv?token"), its %xx read; "" when it does not end as a video */
const char *src_url_file(const char *url, char *out, size_t cap) {
  out[0] = 0; if (!url || strncmp(url, "http", 4)) return out;
  const char *q = url + strcspn(url, "?#"), *b = q; while (b > url && b[-1] != '/') b--;
  size_t o = 0; for (const char *p = b; p < q && o + 1 < cap; p++) { if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) { char h[3] = { p[1], p[2], 0 }; char c = (char)strtol(h, NULL, 16); out[o++] = c == '/' || c == '\n' || !c ? ' ' : c; p += 2; } else out[o++] = *p == '+' ? ' ' : *p; }
  out[o] = 0; if (!src_is_video(out)) out[0] = 0; return out;
}
