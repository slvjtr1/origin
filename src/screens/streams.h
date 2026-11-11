/* The streams sheet: slides in from the right over the title page. */
#ifndef SB_STREAMS_H
#define SB_STREAMS_H
void streams_empty_note(const char *text); /* after streams_show: the words instead of "No streams found." */
void streams_show(const char *type, const char *id, const char *vid, const char *title, const char *epLabel);
void streams_close(int instant);
int  streams_open(void);
#endif
