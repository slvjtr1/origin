#include "core/subclean.h"
#include <stdlib.h>
#include <string.h>

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
   Brackets of every kind, full-width ones too; they must close within the line's next 300 bytes. */
int sub_sdh_hide = 1;
static int open_br(const unsigned char *p, int *ol, const char **cl) {
  if (*p == '(') { *ol = 1; *cl = ")"; return 1; } if (*p == '[') { *ol = 1; *cl = "]"; return 1; } if (*p == '{') { *ol = 1; *cl = "}"; return 1; }
  if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0x88) { *ol = 3; *cl = "\xef\xbc\x89"; return 1; } /* （ ） */
  if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0xbb) { *ol = 3; *cl = "\xef\xbc\xbd"; return 1; } /* ［ ］ */
  if (p[0] == 0xe3 && p[1] == 0x80 && p[2] == 0x90) { *ol = 3; *cl = "\xe3\x80\x91"; return 1; } /* 【 】 */
  return 0;
}
static int word_start(const char *s, const char *p) { if (p == s) return 1; unsigned char c = (unsigned char)p[-1]; return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '-' || c == '"' || c == '\'' || c >= 0x80; }
static int caps_only(const char *a, const char *b) { int up = 0; for (const char *p = a; p < b; p++) { unsigned char c = (unsigned char)*p; if (c >= 'a' && c <= 'z') return 0; if (c >= 'A' && c <= 'Z') up++; else if (c >= 0x80) return 0; } return up >= 2; }
size_t sub_strip_sdh(char *s) {
  char *o = s; const char *p = s;
  while (*p) {
    int ol; const char *cl;
    if (open_br((const unsigned char *)p, &ol, &cl) && word_start(s, p)) { /* (…) [ … ]: gone with what is inside */
      const char *e = strstr(p + ol, cl); if (e && e - p <= 300) { p = e + strlen(cl); *o++ = ' '; continue; } }
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
