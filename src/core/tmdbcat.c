#include "core/tmdbcat.h"
#include "core/tmdb.h"
#include "core/store.h"
#include "core/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ---- TMDB id -> IMDb id, kept: "m<id>" / "t<id>" -> "tt..." ("" = TMDB knows none); and "pm<id>" / "pt<id>" -> "1" when
   it streams on one of the well-known services somewhere (Netflix, Prime Video, Disney+...), "0" when not -------- */
#define MAP_KEY "tmdb:imdb-map"
static cJSON *map; static int mapDirty;
static void map_load(void) {
  if (map) return; size_t n = 0; unsigned char *t = cache_read(MAP_KEY, 0, &n);
  map = t ? cJSON_Parse((const char *)t) : NULL; free(t); if (!cJSON_IsObject(map)) { cJSON_Delete(map); map = cJSON_CreateObject(); }
}
static void map_save(void) {
  if (!mapDirty || !map) return; mapDirty = 0;
  if (cJSON_GetArraySize(map) > 12000) { cJSON *f = map->child; for (int i = 0; f && i < 3000; i++) { cJSON *nx = f->next; cJSON_Delete(cJSON_DetachItemViaPointer(map, f)); f = nx; } } /* the oldest go */
  char *s = cJSON_PrintUnformatted(map); if (s) { cache_write(MAP_KEY, (unsigned char *)s, strlen(s)); free(s); }
}
static const char *map_get(const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(map, k); return cJSON_IsString(v) ? v->valuestring : NULL; }

/* ---- one list being answered ---------------------------------------------------------------------------------- */
typedef struct { NetCb cb; void *ud; cJSON *res; int pending, done, so; char kind; char alt[3][900]; int nalt, ai; } Job; /* kind: 'm', 't' or 0 (each result says); so: the well-known services' titles only */
typedef struct { Job *j; char key[24]; } Ext;
static char kind_of(const Job *j, const cJSON *r) {
  if (j->kind) return j->kind;
  const char *mt = js_str(r, "media_type", ""); return !strcmp(mt, "movie") ? 'm' : !strcmp(mt, "tv") ? 't' : 0;
}
static void finish(Job *j) {
  if (j->done) return; j->done = 1;
  cJSON *out = cJSON_CreateObject(), *metas = cJSON_AddArrayToObject(out, "metas"), *r;
  cJSON_ArrayForEach(r, j->res) {
    char k = kind_of(j, r); if (!k || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "adult"))) continue;
    char key[24]; snprintf(key, sizeof key, "%c%d", k, (int)js_num(r, "id", 0)); const char *tt = map_get(key); if (!tt || strncmp(tt, "tt", 2)) continue;
    if (j->so) { char pk[28]; snprintf(pk, sizeof pk, "p%s", key); const char *pv = map_get(pk); if (!pv || strcmp(pv, "1")) continue; } /* (search: on a service people know, or not shown) */
    const char *nm = js_str(r, k == 'm' ? "title" : "name", ""), *pp = js_str(r, "poster_path", ""), *bp = js_str(r, "backdrop_path", ""), *dt = js_str(r, k == 'm' ? "release_date" : "first_air_date", "");
    if (!*nm) continue;
    cJSON *m = cJSON_CreateObject(); char u[300];
    cJSON_AddStringToObject(m, "id", tt); cJSON_AddStringToObject(m, "type", k == 'm' ? "movie" : "series"); cJSON_AddStringToObject(m, "name", nm);
    if (*pp) { snprintf(u, sizeof u, "https://image.tmdb.org/t/p/w342%s", pp); cJSON_AddStringToObject(m, "poster", u); }
    if (*bp) { snprintf(u, sizeof u, "https://image.tmdb.org/t/p/w1280%s", bp); cJSON_AddStringToObject(m, "background", u); }
    cJSON_AddStringToObject(m, "description", js_str(r, "overview", ""));
    if (strlen(dt) >= 4) { char y[8]; snprintf(y, sizeof y, "%.4s", dt); cJSON_AddStringToObject(m, "releaseInfo", y); }
    cJSON_AddItemToArray(metas, m); }
  char *txt = cJSON_PrintUnformatted(out); cJSON_Delete(out); cJSON_Delete(j->res); map_save();
  j->cb(200, (const unsigned char *)txt, strlen(txt), j->ud); free(txt); free(j);
}
/* the services that count: streaming (by subscription, with ads or free) on one of these, in any country */
static int known_service(const char *n) {
  static const char *S[] = { "netflix", "amazon prime video", "prime video", "disney", "hbo max", "hulu", "apple tv", "paramount", "peacock", "crunchyroll", "shahid", "osn", "starz", "showtime", NULL };
  char l[96]; size_t i = 0; for (; n[i] && i + 1 < sizeof l; i++) l[i] = (char)tolower((unsigned char)n[i]); l[i] = 0;
  if (!strcmp(l, "max")) return 1;
  for (int k = 0; S[k]; k++) if (strstr(l, S[k])) return 1;
  return 0;
}
static int on_known_service(const cJSON *x) {
  const cJSON *rs = js_obj(js_obj(x, "watch/providers"), "results"), *cc; if (!rs) return 0;
  cJSON_ArrayForEach(cc, rs) { static const char *T[3] = { "flatrate", "ads", "free" };
    for (int t = 0; t < 3; t++) { const cJSON *l = js_arr(cc, T[t]), *p; if (l) cJSON_ArrayForEach(p, l) if (known_service(js_str(p, "provider_name", ""))) return 1; } }
  return 0;
}
static void on_ext(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; Ext *e = ud; Job *j = e->j; cJSON *x = st == 200 ? net_json(b) : NULL;
  if (x) { const char *tt = js_str(js_obj(x, "external_ids"), "imdb_id", js_str(x, "imdb_id", "")); cJSON_DeleteItemFromObjectCaseSensitive(map, e->key); cJSON_AddStringToObject(map, e->key, tt && !strncmp(tt, "tt", 2) ? tt : "");
    char pk[28]; snprintf(pk, sizeof pk, "p%s", e->key); cJSON_DeleteItemFromObjectCaseSensitive(map, pk); cJSON_AddStringToObject(map, pk, on_known_service(x) ? "1" : "0"); mapDirty = 1; } /* (no answer: asked again next time) */
  net_json_free(x); free(e);
  if (--j->pending <= 0) finish(j);
}
static void on_list(int st, const unsigned char *b, size_t len, void *ud) {
  (void)len; Job *j = ud; cJSON *x = st == 200 ? net_json(b) : NULL; const cJSON *rs = js_arr(x, "results");
  if (rs && !cJSON_GetArraySize(rs) && j->ai < j->nalt) { net_json_free(x); fprintf(stderr, "[search] nothing: asked again as \"%s\"\n", j->alt[j->ai]); if (tmdb_request(j->alt[j->ai++], on_list, j)) return; j->cb(404, NULL, 0, j->ud); free(j); return; } /* a spelling slip: the next way of writing it */
  if (!rs) { net_json_free(x); j->cb(st == 200 ? 404 : (st ? st : -1), NULL, 0, j->ud); free(j); return; }
  map_load(); j->res = cJSON_Duplicate(rs, 1); net_json_free(x); j->pending = 1; /* (the list's own count, let go below) */
  const cJSON *r; cJSON_ArrayForEach(r, j->res) {
    char k = kind_of(j, r); int id = (int)js_num(r, "id", 0); if (!k || !id) continue;
    char key[24], pk[28]; snprintf(key, sizeof key, "%c%d", k, id); snprintf(pk, sizeof pk, "p%s", key); if (map_get(key) && map_get(pk)) continue;
    Ext *e = calloc(1, sizeof *e); e->j = j; snprintf(e->key, sizeof e->key, "%s", key); char p[96]; snprintf(p, sizeof p, "/%s/%d?append_to_response=external_ids,watch/providers", k == 'm' ? "movie" : "tv", id); /* its IMDb id and where it streams, in one */
    j->pending++; if (!tmdb_request(p, on_ext, e)) { j->pending--; free(e); } }
  if (--j->pending <= 0) finish(j);
}

/* ---- a search that finds nothing, written again: the slips people make in Arabic (ه for ة at a word's end and back,
   ى / ي, the alef with or without its hamza), then without its last word ------------------------------------------------ */
static void url_dec(const char *s, char *o, size_t cap) { size_t n = 0; for (; *s && n + 1 < cap; s++) { if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) { char h[3] = { s[1], s[2], 0 }; o[n++] = (char)strtol(h, NULL, 16); s += 2; } else o[n++] = *s == '+' ? ' ' : *s; } o[n] = 0; }
static void url_enc(const char *s, char *o, size_t cap) { size_t n = 0; for (; *s && n + 4 < cap; s++) { unsigned char c = (unsigned char)*s; if (isalnum(c) || strchr("-_.~", c)) o[n++] = (char)c; else n += (size_t)snprintf(o + n, cap - n, "%%%02X", c); } o[n] = 0; }
static int word_end(const char *p) { return !p[0] || p[0] == ' '; }
/* how: 1 the word-end letters swapped (ه <-> ة, ى <-> ي), 2 every letter in its plain form (ا, ه, ي), 3 the last word left out */
static int rewrite(const char *in, int how, char *out, size_t cap) {
  size_t n = 0; int arabic = 0;
  if (how == 3) { snprintf(out, cap, "%s", in); char *e = out + strlen(out); while (e > out && e[-1] == ' ') *--e = 0; char *sp = strrchr(out, ' '); if (!sp) return 0; while (sp > out && sp[-1] == ' ') sp--; *sp = 0; return strlen(out) >= 2; }
  for (const unsigned char *p = (const unsigned char *)in; *p && n + 3 < cap;) {
    if (p[0] == 0xd8 || p[0] == 0xd9) { arabic = 1; unsigned c = (unsigned)(p[0] << 8 | p[1]); const char *end = (const char *)p + 2; unsigned w = c;
      if (how == 1 && word_end(end)) w = c == 0xd987 ? 0xd8a9 : c == 0xd8a9 ? 0xd987 : c == 0xd989 ? 0xd98a : c == 0xd98a ? 0xd989 : c;
      if (how == 2) w = (c == 0xd8a3 || c == 0xd8a5 || c == 0xd8a2) ? 0xd8a7 : c == 0xd8a9 ? 0xd987 : c == 0xd989 ? 0xd98a : c;
      out[n++] = (char)(w >> 8); out[n++] = (char)(w & 0xff); p += 2; continue; }
    out[n++] = (char)*p++; }
  out[n] = 0; return arabic && strcmp(out, in) != 0;
}
static void search_alts(Job *j, const char *path, const char *tail) {
  const char *q = strstr(path, "query="); if (!q) return; q += 6; const char *qe = strchr(q, '&');
  char enc[400], raw[400]; snprintf(enc, sizeof enc, "%.*s", (int)(qe ? qe - q : (long)strlen(q)), q); url_dec(enc, raw, sizeof raw);
  char seen[3][400]; int ns = 0;
  for (int how = 1; how <= 3 && j->nalt < 3; how++) { char v[400], ve[600]; if (!rewrite(raw, how, v, sizeof v)) continue;
    int dup = 0; for (int i = 0; i < ns; i++) if (!strcmp(seen[i], v)) dup = 1; if (dup) continue; snprintf(seen[ns++], sizeof seen[0], "%s", v);
    url_enc(v, ve, sizeof ve); snprintf(j->alt[j->nalt++], sizeof j->alt[0], "%.*s%s%s%s", (int)(q - path), path, ve, qe ? qe : "", tail); }
}
int tmdbcat_search_strict(void) { return strcmp(store_get("searchKnownOnly", "true"), "false") != 0; } /* Settings: on unless turned off */
int tmdbcat_is(const char *url) { return url && !strncmp(url, "tmdb:", 5); }
void tmdbcat_make(const char *path, char *out, size_t cap) { snprintf(out, cap, "tmdb:%s:%s", tmdb_lang(), path); }
void tmdbcat_url(const char *url, int page, NetCb cb, void *ud) {
  /* tmdb:<lang>:<path>[?query][.json] */
  const char *lang = url + 5, *c = strchr(lang, ':'); if (!c) { cb(-1, NULL, 0, ud); return; }
  char lg[8]; snprintf(lg, sizeof lg, "%.*s", (int)(c - lang < 7 ? c - lang : 7), lang);
  char path[700]; snprintf(path, sizeof path, "/%s", c + 1); size_t pl = strlen(path); if (pl > 5 && !strcmp(path + pl - 5, ".json")) path[pl - 5] = 0;
  char full[900]; snprintf(full, sizeof full, "%s%clanguage=%s&include_adult=false&page=%d", path, strchr(path, '?') ? '&' : '?', lg, page < 1 ? 1 : page);
  Job *j = calloc(1, sizeof *j); j->cb = cb; j->ud = ud; j->so = !strncmp(path, "/search/", 8) && tmdbcat_search_strict();
  if (!strncmp(path, "/search/", 8)) { char tail[120]; snprintf(tail, sizeof tail, "%clanguage=%s&include_adult=false&page=%d", strchr(path, '?') ? '&' : '?', lg, page < 1 ? 1 : page); search_alts(j, path, tail); }
  j->kind = strstr(path, "/multi") || strstr(path, "/all/") ? 0 : (!strncmp(path, "/tv", 3) || strstr(path, "/tv/") || strstr(path, "/tv?")) ? 't' : 'm';
  if (!tmdb_request(full, on_list, j)) { free(j); cb(-1, NULL, 0, ud); } /* no key */
}
int tmdbcat_genre(const char *type, const char *name) {
  static const struct { const char *n; int m, t; } G[] = {
    { "Action", 28, 10759 }, { "Adventure", 12, 10759 }, { "Animation", 16, 16 }, { "Comedy", 35, 35 }, { "Crime", 80, 80 }, { "Documentary", 99, 99 },
    { "Drama", 18, 18 }, { "Family", 10751, 10751 }, { "Fantasy", 14, 10765 }, { "History", 36, 10768 }, { "Horror", 27, 9648 }, { "Music", 10402, 0 },
    { "Mystery", 9648, 9648 }, { "Romance", 10749, 18 }, { "Sci-Fi", 878, 10765 }, { "Thriller", 53, 80 }, { "War", 10752, 10768 }, { "Western", 37, 37 },
    { "Reality-TV", 0, 10764 }, { "Talk-Show", 0, 10767 }, { "News", 0, 10763 } };
  int tv = type && !strcmp(type, "series");
  for (size_t i = 0; i < sizeof G / sizeof *G; i++) if (name && !strcasecmp(G[i].n, name)) return tv ? G[i].t : G[i].m;
  return 0;
}
