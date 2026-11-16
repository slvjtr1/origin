#include "core/subclean.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- one character at a time (UTF-8; a broken byte is kept as it is) ---- */
static unsigned dec(const unsigned char *p, int *len) {
  unsigned c = p[0];
  if (c < 0x80) { *len = 1; return c; }
  if ((c & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) { *len = 2; return (c & 31) << 6 | (p[1] & 63); }
  if ((c & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) { *len = 3; return (c & 15) << 12 | (p[1] & 63) << 6 | (p[2] & 63); }
  if ((c & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) { *len = 4; return (c & 7) << 18 | (p[1] & 63) << 12 | (p[2] & 63) << 6 | (p[3] & 63); }
  *len = 1; return 0xfffd0000u | c; /* (a stray byte: copied unchanged, counted as a letter) */
}
static int is_digit(unsigned c) { return (c >= '0' && c <= '9') || (c >= 0x660 && c <= 0x669) || (c >= 0x6f0 && c <= 0x6f9); }
/* the marks that stay: they change what a line says */
static int is_kept_mark(unsigned c) {
  return c == '.' || c == ',' || c == '-' || c == '?' || c == '!' || c == 0x60c /* ، */ || c == 0x61f /* ؟ */ || c == 0x6d4 /* ۔ */ ||
         c == 0x2026 /* … */ || c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 || c == 0x2014 /* dashes */ || c == 0xa1 || c == 0xbf /* ¡ ¿ */;
}
static int is_closing_mark(unsigned c) { return c == '.' || c == ',' || c == '?' || c == '!' || c == 0x60c || c == 0x61f || c == 0x6d4 || c == 0x2026; }
/* the clutter */
static int is_symbol(unsigned c) {
  if (c < 0x80) return (c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) || (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e); /* ASCII punctuation (the kept ones are asked first) */
  switch (c) {
    case 0xab: case 0xbb: case 0xb7: case 0xa6: case 0xa7: case 0xb6: case 0xa4: /* « » · ¦ § ¶ ¤ */
    case 0x61b: case 0x66a: case 0x66d: case 0xfd3e: case 0xfd3f: /* ؛ ٪ ٭ ﴾ ﴿ */
      return 1; }
  if (c >= 0x2015 && c <= 0x205e) return 1;  /* quotes, bullets, primes, ‹ › and the rest of general punctuation */
  if (c >= 0x2190 && c <= 0x21ff) return 1;  /* arrows */
  if (c >= 0x2500 && c <= 0x27bf) return 1;  /* boxes, shapes, music notes ♪ ♫, dingbats */
  if (c >= 0x2e00 && c <= 0x2e7f) return 1;  /* supplemental punctuation */
  if (c >= 0x3008 && c <= 0x3011) return 1;  /* 〈 〉 《 》 「 」 『 』 【 】 */
  if (c >= 0xff01 && c <= 0xff0f) return 1;  /* fullwidth ! " # ... / */
  return 0;
}
static int is_space(unsigned c) { return c == ' ' || c == '\t' || c == 0xa0; }
/* a letter or a number: anything else that is not a mark, a symbol, a space or a line break */
static int is_word(unsigned c) { return c && c != '\n' && c != '\r' && !is_space(c) && !is_kept_mark(c) && !is_symbol(c) && !(c >= 0x200b && c <= 0x200f) && !(c >= 0x202a && c <= 0x202e) && c != 0x61c; }
static int is_letter(unsigned c) { return is_word(c) && !is_digit(c); }

/* a symbol that is part of a word or a number keeps its place */
static int keep_in_place(unsigned c, unsigned prev, unsigned next) {
  if ((c == '\'' || c == 0x2019) && is_letter(prev) && is_letter(next)) return 1;   /* don't, l'homme */
  if ((c == ':' || c == '/') && is_digit(prev) && is_digit(next)) return 1;          /* 10:30, 1/2 */
  if ((c == '%' || c == 0x66a) && is_digit(prev)) return 1;                           /* 50% */
  if ((c == '$' || c == 0x20ac || c == 0xa3) && (is_digit(next) || is_digit(prev))) return 1; /* $5, 5€ */
  if (c == '&' && is_letter(prev) && is_letter(next)) return 1;                      /* R&B */
  return 0;
}
/* a symbol the meaning needs, said another way */
static unsigned swap_for(unsigned c) { return c == ';' ? ',' : c == 0x61b ? 0x60c : 0; }

static size_t put(char *o, unsigned c) {
  if (c >= 0xfffd0000u) { o[0] = (char)(c & 0xff); return 1; }
  if (c < 0x80) { o[0] = (char)c; return 1; }
  if (c < 0x800) { o[0] = (char)(0xc0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
  if (c < 0x10000) { o[0] = (char)(0xe0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
  o[0] = (char)(0xf0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}

size_t sub_clean(char *s) {
  if (!s || !*s) return 0;
  size_t L = strlen(s), n = 0; unsigned *cp = malloc((L + 1) * sizeof *cp); char *out = malloc(L + 1); if (!cp || !out) { free(cp); free(out); return L; }
  for (size_t i = 0; i < L;) { int k; cp[n++] = dec((const unsigned char *)s + i, &k); i += (size_t)k; }
  size_t o = 0, lineStart = 0; int cut = 0; /* cut: a symbol was taken out since the last thing written */
  unsigned last = '\n'; /* what was last written ('\n' at a line's start) */
  for (size_t i = 0; i <= n; i++) {
    unsigned c = i < n ? cp[i] : '\n', prev = i ? cp[i - 1] : 0, next = i + 1 < n ? cp[i + 1] : 0;
    if (c == '\r') continue;
    if (c == '\n') { /* the line ends: no space at its end; a line with nothing left goes */
      while (o > lineStart && out[o - 1] == ' ') o--;
      int words = 0; for (size_t j = lineStart; j < o;) { int k; unsigned d = dec((const unsigned char *)out + j, &k); if (is_word(d) || is_closing_mark(d)) { words = 1; break; } j += (size_t)k; }
      if (!words) o = lineStart; /* (nothing, or a dash alone: a line of music notes was all symbols) */
      else if (i < n) { out[o++] = '\n'; lineStart = o; }
      last = '\n'; cut = 0; continue; }
    if (is_space(c)) { if (last != '\n' && last != ' ') { out[o++] = ' '; last = ' '; } continue; } /* never two, never first */
    if (c == '*') { size_t a = i, b = i; while (a > 0 && cp[a - 1] == '*') a--; while (b < n && cp[b] == '*') b++; /* f**k: the run stays when it is inside a word */
      if (a > 0 && b < n && is_letter(cp[a - 1]) && is_letter(cp[b])) { o += put(out + o, c); last = c; cut = 0; continue; } }
    if (!is_kept_mark(c) && is_symbol(c) && !keep_in_place(c, prev, next)) {
      unsigned sw = swap_for(c);
      if (!sw) { /* stuck between two words (past any other symbols around it): one space */
        size_t a = i, b = i + 1; while (a > 0 && is_symbol(cp[a - 1]) && !is_kept_mark(cp[a - 1])) a--; while (b < n && is_symbol(cp[b]) && !is_kept_mark(cp[b])) b++;
        if (a > 0 && b < n && is_word(cp[a - 1]) && is_word(cp[b]) && last != ' ' && last != '\n') { out[o++] = ' '; last = ' '; }
        cut = 1; continue; }
      c = sw; }
    if (is_closing_mark(c) && last == ' ' && cut) { o--; last = o > lineStart ? (unsigned char)out[o - 1] : '\n'; } /* the space was the removed symbol's */
    o += put(out + o, c); last = c >= 0x80 ? 'x' : c; cut = 0;
  }
  while (o && (out[o - 1] == '\n' || out[o - 1] == ' ')) o--;
  memcpy(s, out, o); s[o] = 0; free(cp); free(out); return o;
}

/* ---- sound descriptions: [coughs], (gunshots), ♪ MUSIC ♪ and speakers' names (JOHN:) go ----
   A bracket counts when it opens a word (at a line's start, after a space or a dash): "friend(s)" keeps its letters.
   Brackets of every kind, full-width ones too; they must close within the line's next 300 bytes.
   Arabic subtitles write a name between brackets ("يا (ريك)"): what a bracket holds in Arabic letters is spoken
   words and stays (the brackets alone go, in sub_clean), unless it reads as a description of a sound ((موسيقى),
   [يضحك], (بالإسبانية)). */
int sub_sdh_hide = 1;
int sub_ads_hide = 1;
static int open_br(const unsigned char *p, int *ol, const char **cl) {
  if (*p == '(') { *ol = 1; *cl = ")"; return 1; } if (*p == '[') { *ol = 1; *cl = "]"; return 1; } if (*p == '{') { *ol = 1; *cl = "}"; return 1; }
  if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0x88) { *ol = 3; *cl = "\xef\xbc\x89"; return 1; } /* （ ） */
  if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0xbb) { *ol = 3; *cl = "\xef\xbc\xbd"; return 1; } /* ［ ］ */
  if (p[0] == 0xe3 && p[1] == 0x80 && p[2] == 0x90) { *ol = 3; *cl = "\xe3\x80\x91"; return 1; } /* 【 】 */
  return 0;
}
static int word_start(const char *s, const char *p) { if (p == s) return 1; unsigned char c = (unsigned char)p[-1]; return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '-' || c == '"' || c == '\'' || c >= 0x80; }
static int caps_only(const char *a, const char *b) { int up = 0; for (const char *p = a; p < b; p++) { unsigned char c = (unsigned char)*p; if (c >= 'a' && c <= 'z') return 0; if (c >= 'A' && c <= 'Z') up++; else if (c >= 0x80) return 0; } return up >= 2; }
/* Arabic letters (U+0600..U+06FF, and the presentation forms old files are saved in) between a and b */
static int has_arabic(const char *a, const char *b) {
  for (const unsigned char *p = (const unsigned char *)a; p + 1 < (const unsigned char *)b; p++) {
    if (p[0] >= 0xd8 && p[0] <= 0xdb && (p[1] & 0xc0) == 0x80) return 1;
    if (p[0] == 0xef && p + 2 < (const unsigned char *)b && (p[1] == 0xad || p[1] == 0xae || p[1] == 0xaf || (p[1] >= 0xb0 && p[1] <= 0xbb))) return 1; }
  return 0;
}
static int span_has(const char *a, const char *b, const char *w) { size_t n = strlen(w); for (const char *p = a; p + n <= b; p++) if (!memcmp(p, w, n)) return 1; return 0; }
/* what Arabic captions call a sound, a manner of speaking or the language spoken */
static int arabic_sound(const char *a, const char *b) {
  static const char *const W[] = { "موسيق", "ضحك", "قهقه", "تنهد", "تنهّد", "صراخ", "يصرخ", "تصرخ", "صرخ", "صياح", "بكاء", "يبكي", "تبكي", "نحيب", "همس", "همهم", "طرق على", "طرقات", "رنين", "يرن", "يرنّ",
    "صوت", "أصوات", "اصوات", "تصفيق", "صمت", "أنين", "لهاث", "يلهث", "تلهث", "سعال", "يسعل", "تسعل", "انفجار", "إطلاق نار", "اطلاق نار", "طلق ناري", "طلقات", "صفير", "صافرة", "صفارة", "غناء", "يغني", "تغني", "أغنية", "اغنية",
    "نباح", "ضجيج", "ضوضاء", "شهيق", "زفير", "تأوه", "يتأوه", "هتاف", "تنحنح", "يتنحنح", "خطوات", "جرس", "صرير", "دوي", "شخير", "غير مسموع", "غير واضح", "غير مفهوم", "بالإنجليزية", "بالانجليزية", "بالإسبانية", "بالاسبانية",
    "بالفرنسية", "بالألمانية", "بالالمانية", "بالإيطالية", "بالايطالية", "بالروسية", "بالصينية", "باليابانية", "بالكورية", "بالعربية", "بلغة", "يتحدث", "تتحدث", "يتكلم", "يتمتم", "تمتمة", "يلهثون", "يضحكون", "يصرخون", "تشويش", "هدير", "أزيز", "ازيز" };
  for (unsigned i = 0; i < sizeof W / sizeof *W; i++) if (span_has(a, b, W[i])) return 1;
  return 0;
}
size_t sub_strip_sdh(char *s) {
  char *o = s; const char *p = s;
  while (*p) {
    int ol; const char *cl;
    if (open_br((const unsigned char *)p, &ol, &cl) && word_start(s, p)) { /* (…) [ … ]: gone with what is inside */
      const char *e = strstr(p + ol, cl);
      if (e && e - p <= 300 && !(has_arabic(p + ol, e) && !arabic_sound(p + ol, e))) { p = e + strlen(cl); *o++ = ' '; continue; } }
    if (!strncmp(p, "\xe2\x99\xaa", 3)) { /* ♪ IN CAPITALS ♪: a description of the music, not its words */
      const char *e = strstr(p + 3, "\xe2\x99\xaa"); const char *nl = strchr(p + 3, '\n'); if (e && (!nl || e < nl) && caps_only(p + 3, e)) { p = e + 3; *o++ = ' '; continue; } }
    *o++ = *p++;
  }
  *o = 0;
  /* JOHN: / MAN 2: at a line's start (after a dash): the name goes, the words stay */
  char *r = s, *w = s; int ls = 1;
  while (*r) {
    if (ls) { char *q = r; while (*q == ' ' || *q == '-') q++; char *e = q; int up = 0;
      while ((*e >= 'A' && *e <= 'Z') || (*e >= '0' && *e <= '9') || *e == ' ' || *e == '.' || *e == '\'' || *e == '#') { if (*e >= 'A' && *e <= 'Z') up++; e++; }
      if (up >= 3 && *e == ':' && e - q <= 30 && (e[1] == ' ' || e[1] == '\n' || !e[1]) && !(e > q && e[-1] >= '0' && e[-1] <= '9' && e[1] >= '0' && e[1] <= '9')) {
        while (r < q) *w++ = *r++; r = e + 1; } }
    ls = *r == '\n'; if (*r) *w++ = *r++;
  }
  *w = 0; return (size_t)(w - s);
}

/* ---- markup: what a subtitle file writes for the player, not for the viewer ----
   <i>, </font>, <c.yellow>, <00:01:02.000> (WebVTT), {\an8} and {\i1} (SubStation), {y:i} (MicroDVD) go; <br> is a
   new line; &amp; and the like become their character; "\N" a new line. A "<" or a "{" that opens none of these is
   a character of the line: nothing after it is lost ("5 < 6", "<<ريك>>", "{ريك}"). */
static int tag_name(const char *p, size_t n) {
  static const char *const T[] = { "i", "b", "u", "s", "em", "strong", "font", "c", "v", "ruby", "rt", "rp", "lang", "span", "br", "p", "div", "a", "small", "big", "sub", "sup", "center", "strike", "tt", "h1", "h2", "h3" };
  for (unsigned i = 0; i < sizeof T / sizeof *T; i++) if (strlen(T[i]) == n && !strncasecmp(p, T[i], n)) return 1;
  return 0;
}
/* the length of the tag at p ("<...>"), 0 when it is not one; *br: it breaks the line */
static size_t tag_len(const char *p, int *br) {
  *br = 0; const char *q = p + 1; if (*q == '/') q++;
  const char *n0 = q; while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9' && q > n0)) q++;
  size_t nl = (size_t)(q - n0);
  if (!nl) { /* <00:01:02.000>: a karaoke time */
    const char *d = n0; int dig = 0; while ((*d >= '0' && *d <= '9') || *d == ':' || *d == '.') { dig += *d >= '0' && *d <= '9'; d++; }
    return dig >= 4 && *d == '>' ? (size_t)(d - p) + 1 : 0; }
  if (!tag_name(n0, nl)) return 0;
  if (*q != '>' && *q != ' ' && *q != '.' && *q != '/' && *q != '\t') return 0;
  const char *e = q; while (*e && *e != '>' && *e != '\n' && *e != '<' && e - p < 240) e++;
  if (*e != '>') return 0;
  *br = nl == 2 && !strncasecmp(n0, "br", 2); return (size_t)(e - p) + 1;
}
static size_t entity(const char *p, char *out, size_t *ol) {
  static const struct { const char *e, *c; } E[] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&nbsp;", " " }, { "&lrm;", "" }, { "&rlm;", "" }, { "&zwnj;", "" }, { "&zwj;", "" }, { "&hellip;", "\xe2\x80\xa6" }, { "&ndash;", "-" }, { "&mdash;", "-" } };
  for (unsigned i = 0; i < sizeof E / sizeof *E; i++) { size_t n = strlen(E[i].e); if (!strncasecmp(p, E[i].e, n)) { *ol = strlen(E[i].c); memcpy(out, E[i].c, *ol); return n; } }
  if (p[1] == '#') { int hex = p[2] == 'x' || p[2] == 'X'; const char *d = p + 2 + hex; char *end; unsigned long v = strtoul(d, &end, hex ? 16 : 10);
    if (end > d && *end == ';' && end - p <= 10 && v >= 32 && v < 0x110000 && !(v >= 0xd800 && v < 0xe000)) { if (v == 0xa0) { out[0] = ' '; *ol = 1; } else *ol = put(out, (unsigned)v); return (size_t)(end - p) + 1; } }
  return 0;
}
size_t sub_strip_tags(char *s) {
  if (!s) return 0; char *o = s; const char *p = s;
  while (*p) {
    if (*p == '<') { int br; size_t n = tag_len(p, &br); if (n) { if (br) { while (o > s && o[-1] == ' ') o--; *o++ = '\n'; } p += n; continue; } }
    else if (*p == '{') { const char *e = strchr(p, '}');
      if (e && e - p <= 400 && (p[1] == '\\' || (((p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z')) && p[2] == ':') || (!strncasecmp(p + 1, "an", 2) && p[3] >= '1' && p[3] <= '9' && p[4] == '}'))) { p = e + 1; continue; } }
    else if (*p == '&') { char c[4]; size_t ol = 0, n = entity(p, c, &ol); if (n) { memcpy(o, c, ol); o += ol; p += n; continue; } } /* (never longer than what it stood for) */
    else if (*p == '\\' && (p[1] == 'N' || p[1] == 'n')) { while (o > s && o[-1] == ' ') o--; *o++ = '\n'; p += 2; continue; }
    else if (*p == '\\' && p[1] == 'h') { *o++ = ' '; p += 2; continue; }
    *o++ = *p++;
  }
  *o = 0; return (size_t)(o - s);
}

/* ---- adverts and credits: a line that sells a site or names who made the file, not a line of the film ---- */
static int ci_has(const char *s, const char *w) { size_t n = strlen(w); for (; *s; s++) if (!strncasecmp(s, w, n)) return 1; return 0; }
static int al_num(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static int has_link(const char *s) {
  if (ci_has(s, "www.") || ci_has(s, "http://") || ci_has(s, "https://") || ci_has(s, "t.me/")) return 1;
  static const char *const TLD[] = { ".com", ".org", ".net", ".tv", ".io", ".to", ".cc", ".me", ".co", ".info", ".xyz", ".site", ".club", ".link", ".ws" };
  for (const char *p = s; *p; p++) { if (*p != '.') continue;
    for (unsigned i = 0; i < sizeof TLD / sizeof *TLD; i++) { size_t n = strlen(TLD[i]); if (strncasecmp(p, TLD[i], n) || al_num(p[n])) continue;
      if ((i == 5 || i == 7) && p[n] && p[n] != '/' && p[n] != '\n') continue; /* ".to", ".me": a sentence whose full stop lost its space ("Wait.me too") is not a site */
      int k = 0; const char *q = p; while (q > s && (al_num(q[-1]) || q[-1] == '-')) { q--; k++; }
      if (k >= 4 && (q == s || q[-1] == ' ' || q[-1] == '\n' || q[-1] == '.' || q[-1] == ':' || q[-1] == '@' || q[-1] == '/' || (unsigned char)q[-1] >= 0x80)) return 1; } }
  return 0;
}
int sub_is_ad(const char *t) {
  if (!t || !*t) return 0;
  if (has_link(t)) return 1;
  static const char *const EN[] = { "opensubtitles", "open subtitles", "subtitles by", "subtitle by", "subtitled by", "subs by", "synced by", "sync by", "synced and corrected", "sync and correct", "sync & correct", "corrections by", "corrected by",
    "encoded by", "ripped by", "downloaded from", "become vip member", "advertise your product", "addic7ed", "subscene", "translated by", "translation by", "captioning sponsored", "captions by", "captioned by", "caption by", "subtitling by",
    "osdb", "podnapisi", "yify", "rate this subtitle", "watch movies online", "free download", "join our telegram", "telegram channel", "follow us on" };
  for (unsigned i = 0; i < sizeof EN / sizeof *EN; i++) if (ci_has(t, EN[i])) return 1;
  static const char *const AR[] = { "ترجمة:", "ترجمة :", "ترجمة وتعديل", "ترجمة و تعديل", "ترجمة وتدقيق", "ترجمة و تدقيق", "ترجمة وتنفيذ", "تمت الترجمة", "تمّت الترجمة", "تعديل التوقيت", "ضبط التوقيت", "توقيت:", "تدقيق:", "حقوق الترجمة", "الترجمة بواسطة",
    "ترجمة بواسطة", "ترجمة فريق", "ترجمة الفريق", "فريق الترجمة", "تابعونا", "تابعنا على", "تابعوني", "قناة التلجرام", "قناة التيليجرام", "قناة التلغرام", "قناتنا على", "تيليجرام", "تلجرام", "تلغرام", "انستغرام", "إنستغرام", "انستقرام", "إنستقرام",
    "حسابي على", "حسابنا على", "زوروا موقع", "زورو موقع", "تحميل من موقع", "مشاهدة ممتعة", "مشاهدة طيبة", "نتمنى لكم مشاهدة", "أتمنى لكم مشاهدة", "اتمنى لكم مشاهدة" };
  for (unsigned i = 0; i < sizeof AR / sizeof *AR; i++) if (strstr(t, AR[i])) return 1;
  for (const char *p = t; *p; p++) if (*p == '@' && (p == t || p[-1] == ' ' || p[-1] == '\n' || p[-1] == ':') && (al_num(p[1]) || p[1] == '_')) { int k = 1; while (al_num(p[k]) || p[k] == '_') k++; if (k >= 4) return 1; } /* @handle */
  return 0;
}

/* ---- old code pages ----
   A file saved by an old Windows program has one byte per letter. Western ones (Windows-1252) keep their accented
   letters apart, each among plain ones ("café"); an Arabic one (Windows-1256) is whole words of high bytes. That
   is how the two are told apart: most high bytes standing beside another one reads as Arabic. */
int sub_utf8_ok(const unsigned char *b, size_t n) {
  for (size_t i = 0; i < n;) { unsigned c = b[i]; int k = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (k < 0 || (k == 1 && c < 0xc2) || i + (size_t)k >= n) return 0;
    for (int j = 1; j <= k; j++) if ((b[i + j] & 0xc0) != 0x80) return 0;
    i += (size_t)k + 1; }
  return 1;
}
int sub_bytes_arabic(const unsigned char *b, size_t n) {
  size_t hi = 0, beside = 0;
  for (size_t i = 0; i < n; i++) { if (b[i] < 0xc0) continue; hi++; if ((i > 0 && b[i - 1] >= 0xc0) || (i + 1 < n && b[i + 1] >= 0xc0)) beside++; }
  return hi >= 2 && beside * 4 >= hi * 3;
}
static const unsigned short CP1252_[32] = { 0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
  0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178 };
static const unsigned short CP1256_[128] = { 0x20ac, 0x067e, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0679, 0x2039, 0x0152, 0x0686, 0x0698, 0x0688,
  0x06af, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x06a9, 0x2122, 0x0691, 0x203a, 0x0153, 0x200c, 0x200d, 0x06ba,
  0x00a0, 0x060c, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7, 0x00a8, 0x00a9, 0x06be, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
  0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7, 0x00b8, 0x00b9, 0x061b, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x061f,
  0x06c1, 0x0621, 0x0622, 0x0623, 0x0624, 0x0625, 0x0626, 0x0627, 0x0628, 0x0629, 0x062a, 0x062b, 0x062c, 0x062d, 0x062e, 0x062f,
  0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x0636, 0x00d7, 0x0637, 0x0638, 0x0639, 0x063a, 0x0640, 0x0641, 0x0642, 0x0643,
  0x00e0, 0x0644, 0x00e2, 0x0645, 0x0646, 0x0647, 0x0648, 0x00e7, 0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x0649, 0x064a, 0x00ee, 0x00ef,
  0x064b, 0x064c, 0x064d, 0x064e, 0x00f4, 0x064f, 0x0650, 0x00f7, 0x0651, 0x00f9, 0x0652, 0x00fb, 0x00fc, 0x200e, 0x200f, 0x06d2 };
size_t sub_bytes_utf8(const unsigned char *b, size_t n, int arabic, char *out) {
  if (arabic < 0) arabic = sub_bytes_arabic(b, n);
  size_t o = 0;
  for (size_t i = 0; i < n; i++) { unsigned c = b[i]; if (!c) continue;
    if (c >= 0x80) c = arabic ? CP1256_[c - 0x80] : c < 0xa0 ? CP1252_[c - 0x80] : c;
    o += put(out + o, c); }
  out[o] = 0; return o;
}
