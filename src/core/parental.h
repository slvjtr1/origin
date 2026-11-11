/* The parental guide of a title: IMDb's Parents Guide votes (nudity, violence, profanity, drugs and alcohol,
   frightening scenes), each category kept at the level most voted for unless "none" has as many votes; asked
   once per title, kept for the session. The player reads it as an age and its reasons as the film starts. */
#ifndef SB_PARENTAL_H
#define SB_PARENTAL_H
#define PG_CATS 5
enum { PG_NUDITY, PG_VIOLENCE, PG_PROFANITY, PG_DRUGS, PG_FRIGHT };
enum { PG_MILD = 1, PG_MODERATE, PG_SEVERE };
typedef struct { int n, cat[PG_CATS], lvl[PG_CATS]; } ParGuide; /* in the categories' order; n 0 = nothing to say */
typedef void (*ParGuideCb)(const ParGuide *g, void *ud);
/* id: an IMDb id ("tt..." , an episode's "tt..:s:e" is the show's); cb on the main thread, at once when known (in
   memory or on disk); cb NULL: only fetched and kept, ahead of need (the title page asks as it opens) */
void parental_get(const char *id, ParGuideCb cb, void *ud);
const char *parental_cat_name(int cat);   /* translated */
#endif
