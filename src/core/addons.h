/* The Stremio addon protocol as the official clients use it: which installed addon answers which request (a
   manifest's resources, types and id prefixes), catalogs' extra arguments (search, genre, skip, required ones),
   request addresses, and the cache hints an answer carries. Main thread only (the addon list lives in the store). */
#ifndef SB_ADDONS_H
#define SB_ADDONS_H
#include <stddef.h>
#include "cJSON.h"

/* the addon answers /{resource}/{type}/{id} (switched on here, its manifest's rules); id NULL: any id */
int  addon_serves(const cJSON *ad, const char *resource, const char *type, const char *id);
/* how many addons switched on answer it */
/* subtitles, leniently: declared at all, movie and series are always asked (the id prefixes still apply); and their count */
int  addon_serves_subs(const cJSON *ad, const char *type, const char *id);
int  addon_count_subs(const char *type, const char *id);
void addon_base(const cJSON *ad, char *out, size_t cap);       /* the transport URL without /manifest.json */
const char *addon_name(const cJSON *ad);                      /* its manifest's name ("Addon") */
/* "{base}/{resource}/{type}/{id}[/{extra}].json", each part encoded; extra already "a=b&c=d" (NULL or "": none) */
void addon_url(char *out, size_t cap, const char *base, const char *resource, const char *type, const char *id, const char *extra);
/* appends "name=value" (value encoded) to an extra string */
void addon_extra_add(char *ex, size_t cap, const char *name, const char *value);
void addon_urlenc(const char *s, char *out, size_t cap);

/* a catalog's extra property by name (the full form, or the old extraSupported / extraRequired names); NULL when the
   catalog does not take it. *req: it must always be passed; *opts: its options (NULL when none) */
int  addon_cat_extra(const cJSON *cat, const char *name, int *req, const cJSON **opts, int *optsLimit);
/* the catalog can be asked with exactly these extra names (every one supported, every required one among them) */
int  addon_cat_accepts(const cJSON *cat, const char *const *names, int n);
/* what a catalog is asked with when nothing is chosen: "" (none), the first option of each required property
   ("genre=2025"), or -1 when a required property has no options (search, ids): such a catalog is no board row */
int  addon_cat_default_extra(const cJSON *cat, char *out, size_t cap);

/* an answer's cache hints in seconds (cacheMaxAge, staleRevalidate, staleError; -1 when absent) */
typedef struct { long maxAge, staleRevalidate, staleError; } AddonCache;
void addon_cache_hints(const cJSON *answer, AddonCache *out);
#endif
