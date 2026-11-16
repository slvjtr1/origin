/* Subtitle lines without the clutter: quotes, brackets, music notes, asterisks and the like go; the marks that change
   what a line says stay (. , ، - ? ؟ ! and the ellipsis), so a line reads as if the file had been written that way:
   'اسمي "تركي"' becomes 'اسمي تركي', '( يضحك )' becomes 'يضحك'. The spaces are put right after (never two in a row,
   none at a line's start or end, none left before a closing mark by a removed symbol); a symbol stuck between two words
   ('قال"نعم"') leaves one space. Kept where they are part of a word or a number: don't, 10:30, 1/2, 50%, $5, f**k.
   Semicolons become commas. A line left empty goes; the whole text can end up empty. In place (never longer). */
#ifndef SB_SUBCLEAN_H
#define SB_SUBCLEAN_H
#include <stddef.h>
size_t sub_clean(char *s);   /* returns the new length */
/* sound descriptions out ([coughs], (gunshots), ♪ MUSIC ♪, JOHN:), before sub_clean; on while sub_sdh_hide (Settings) */
extern int sub_sdh_hide;
size_t sub_strip_sdh(char *s);
/* the file's markup out (<i>, {\an8}, &amp;, "\N"), before the two above: only what is markup, never the words beside it */
size_t sub_strip_tags(char *s);
/* a line that advertises a site or credits the file's makers (asked of the line as the file has it); left out while sub_ads_hide */
extern int sub_ads_hide;
int sub_is_ad(const char *line);
/* text that is not UTF-8 (old files, and tracks muxed from them): Windows-1256 when it reads as Arabic (arabic 1: it is
   known to be, -1: told by the bytes), else Windows-1252. out holds up to n * 3 + 1 bytes; zero bytes are skipped */
int sub_utf8_ok(const unsigned char *b, size_t n);
int sub_bytes_arabic(const unsigned char *b, size_t n);
size_t sub_bytes_utf8(const unsigned char *b, size_t n, int arabic, char *out);
#endif
