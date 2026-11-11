#include "core/parental.h"
#include "core/net.h"
#include "core/json.h"
#include "core/i18n.h"
#include "core/store.h"
#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PG_URL "https://api.tiffara.com/titles/%s/parentsGuide"
#define KEEP 60
static const char *CAT_KEY[PG_CATS] = { "SEXUAL_CONTENT", "VIOLENCE", "PROFANITY", "ALCOHOL_DRUGS", "FRIGHTENING_INTENSE_SCENES" };
static const char *CAT_NAME[PG_CATS] = { "Nudity", "Violence", "Profanity", "Drugs and alcohol", "Frightening scenes" };
static const char *LVL_KEY[4] = { "none", "mild", "moderate", "severe" }; /* (their order: PG_MILD..PG_SEVERE) */

typedef struct { char id[16]; ParGuide g; } Kept;
static Kept kept[KEEP]; static int keptN, keptNext;
/* kept on disk too, 30 days (an answer that says nothing: 3 days), so a title seen before needs no request: cache/pg/<id> */
static void disk_path(const char *id, char *out, size_t cap) { snprintf(out, cap, "%s/cache/pg/%s", store_data_dir(), id); }
static int disk_read(const char *id, ParGuide *g) {
  char p[1300]; disk_path(id, p, sizeof p); FILE *f = fopen(p, "r"); if (!f) return 0; long at = 0; int n = 0, ok = fscanf(f, "%ld %d", &at, &n) == 2 && n >= 0 && n <= PG_CATS;
  memset(g, 0, sizeof *g); for (int i = 0; ok && i < n; i++) ok = fscanf(f, "%d %d", &g->cat[i], &g->lvl[i]) == 2 && g->cat[i] >= 0 && g->cat[i] < PG_CATS && g->lvl[i] >= 1 && g->lvl[i] <= 3;
  fclose(f); if (!ok || time(NULL) - at > (n ? 30 : 3) * 86400L) return 0; g->n = n; return 1; }
static void disk_write(const char *id, const ParGuide *g) {
  char p[1300], d[1300]; snprintf(d, sizeof d, "%s/cache/pg", store_data_dir()); mkdir(d, 0755); disk_path(id, p, sizeof p);
  FILE *f = fopen(p, "w"); if (!f) return; fprintf(f, "%ld %d", (long)time(NULL), g->n); for (int i = 0; i < g->n; i++) fprintf(f, " %d %d", g->cat[i], g->lvl[i]); fprintf(f, "\n"); fclose(f); }
static void keep(const char *id, const ParGuide *g) { for (int i = 0; i < keptN; i++) if (!strcmp(kept[i].id, id)) { kept[i].g = *g; return; }
  Kept *e = &kept[keptNext]; keptNext = (keptNext + 1) % KEEP; if (keptN < KEEP) keptN++; snprintf(e->id, sizeof e->id, "%s", id); e->g = *g; }
typedef struct Ask { char id[16]; int nw; ParGuideCb cb[4]; void *ud[4]; struct Ask *next; } Ask; /* waiters on one request */
static Ask *flying; /* the requests on their way: a second ask for one joins it */

/* a category's level: the one with the most votes other than "none", dropped when "none" has as many or more
   (some vote for everything: without this every film would show all five) */
static int level_of(const cJSON *cat) {
  double best = 0, none = 0; int lv = 0; const cJSON *b;
  cJSON_ArrayForEach(b, js_arr(cat, "severityBreakdowns")) {
    const char *s = js_str(b, "severityLevel", ""); double v = js_num(b, "voteCount", 0);
    if (!strcmp(s, "none")) { none = v; continue; }
    for (int k = 1; k < 4; k++) if (!strcmp(s, LVL_KEY[k]) && v > best) { best = v; lv = k; } }
  return lv && best > none ? lv : 0;
}
static void on_net(int st, const unsigned char *body, size_t len, void *ud) {
  Ask *a = ud; (void)len; if (st == -1) { for (Ask **pp = &flying; *pp; pp = &(*pp)->next) if (*pp == a) { *pp = a->next; break; } free(a); return; }
  ParGuide g; memset(&g, 0, sizeof g); cJSON *j = st == 200 ? net_json(body) : NULL; const cJSON *c;
  for (int k = 0; k < PG_CATS; k++) cJSON_ArrayForEach(c, js_arr(j, "parentsGuide")) if (!strcmp(js_str(c, "category", ""), CAT_KEY[k])) { int lv = level_of(c); if (lv) { g.cat[g.n] = k; g.lvl[g.n] = lv; g.n++; } break; }
  net_json_free(j);
  fprintf(stderr, "[parental] %s: status %d, %d categories\n", a->id, st, g.n);
  if (st == 200) { keep(a->id, &g); disk_write(a->id, &g); } /* (a failure is not kept: asked again next time) */
  for (Ask **pp = &flying; *pp; pp = &(*pp)->next) if (*pp == a) { *pp = a->next; break; }
  for (int i = 0; i < a->nw; i++) if (a->cb[i]) a->cb[i](&g, a->ud[i]); free(a);
}
void parental_get(const char *id, ParGuideCb cb, void *ud) {
  char t[16]; size_t n = 0; if (!id || strncmp(id, "tt", 2)) return; while (id[n] && id[n] != ':' && n + 1 < sizeof t) { t[n] = id[n]; n++; } t[n] = 0; /* (an episode: its show's guide) */
  for (int i = 0; i < keptN; i++) if (!strcmp(kept[i].id, t)) { if (cb) cb(&kept[i].g, ud); return; }
  { ParGuide g; if (disk_read(t, &g)) { keep(t, &g); if (cb) cb(&g, ud); return; } }
  for (Ask *f = flying; f; f = f->next) if (!strcmp(f->id, t)) { if (cb && f->nw < 4) { f->cb[f->nw] = cb; f->ud[f->nw] = ud; f->nw++; } return; } /* (already on its way) */
  Ask *a = calloc(1, sizeof *a); if (!a) return; snprintf(a->id, sizeof a->id, "%s", t); if (cb) { a->cb[0] = cb; a->ud[0] = ud; a->nw = 1; }
  char url[160]; snprintf(url, sizeof url, PG_URL, t); if (!net_get(url, on_net, a)) { free(a); return; } a->next = flying; flying = a;
}
const char *parental_cat_name(int cat) { return cat >= 0 && cat < PG_CATS ? T(CAT_NAME[cat]) : ""; }
