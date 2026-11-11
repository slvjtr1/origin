/* TMDB's lists as catalogs: trending, popular, top rated, genres, recommendations and search, in the language titles
   and descriptions are shown in. An address "tmdb:<lang>:<path>[?query]" (as a CatalogDef url, ".json" after it) is
   asked from TMDB and answered as an addon catalog would be ({"metas":[...]}, IMDb ids), so the catalog code, its
   cache and its pages work as for any addon. Each TMDB title is matched to its IMDb id once (kept on disk): the
   streams, the library and the watched marks stay the IMDb id's. Main thread. */
#ifndef SB_TMDBCAT_H
#define SB_TMDBCAT_H
#include "core/net.h"
/* page: 1, 2... (TMDB's 20 a page); cb gets 200 and the JSON, or another status when TMDB does not answer */
void tmdbcat_url(const char *url, int page, NetCb cb, void *ud);
int  tmdbcat_is(const char *url);                    /* a "tmdb:" address */
/* a catalog address for path (e.g. "trending/movie/week"), in the language titles are shown in */
void tmdbcat_make(const char *path, char *out, size_t cap);
/* TMDB's genre id for Cinemeta's English genre name (movie or series); 0 when it has none */
int  tmdbcat_genre(const char *type, const char *name);
/* Search shows only titles that stream on a well-known service (Netflix, Prime Video, Disney+...): Settings, on by default */
int  tmdbcat_search_strict(void);
#endif
