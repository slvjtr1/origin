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
#endif
