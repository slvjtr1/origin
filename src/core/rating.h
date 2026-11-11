/* A title's official age rating (the viewer notice's age when the Parents Guide has nothing). With a TMDB key: the certification for the
   country of the app's language (else the US one) with its descriptors; without one, films only, from
   Wikidata (FSK, CNC, ClassInd or MPA by language). Kept in memory for the session. */
#ifndef SB_RATING_H
#define SB_RATING_H
#include <stddef.h>

typedef struct { char rating[32]; char desc[4][64]; int ndesc; } Rating;  /* rating "" = none known */
/* cb on the main thread (straight away when it is known already); ud is handed back as given, so a caller
   that may be gone by then passes something it can check (a session number) */
typedef void (*RatingCb)(const Rating *r, void *ud);
void rating_get(const char *type, const char *id, RatingCb cb, void *ud);
/* the certification as an age: 0 all ages, 7, 12, 13, 16, 18...; -1 when it says none we can read */
int rating_age(const Rating *r);
#endif
