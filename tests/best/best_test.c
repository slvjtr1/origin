/* The best-stream scoring on captured addon answers: prints every stream's score and the pick as JSON, for
   tests/best/compare.js to hold against the web app's own functions.
   best_test <fixture.json> <maxRes> <hdr> <dv> <av1> <dts> <truehd> <mbps> */
#include "core/best.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* what the scoring needs from the rest of the app */
static char tbuf[4][256]; static int tn;
const char *T(const char *en) { return en; }
const char *T1(const char *en, const char *k, const char *v) {
  char *o = tbuf[tn++ & 3], key[40]; snprintf(key, sizeof key, "{%s}", k); const char *p = strstr(en, key);
  if (!p) snprintf(o, 256, "%s", en); else snprintf(o, 256, "%.*s%s%s", (int)(p - en), en, v, p + strlen(key)); return o; }
const TvCaps *best_caps(void) { return NULL; }
double best_net_mbps(void) { return 0; }

static char *read_all(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(n + 1); b[fread(b, 1, n, f)] = 0; fclose(f); return b; }
static const char *jstr(const cJSON *o, const char *k) { const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsString(v) ? v->valuestring : NULL; }
static void jout(const char *s) { putchar('"'); for (; *s; s++) { if (*s == '"' || *s == '\\') printf("\\%c", *s); else if ((unsigned char)*s < 0x20) printf("\\u%04x", *s); else putchar(*s); } putchar('"'); }

int main(int argc, char **argv) {
  if (argc < 9) { fprintf(stderr, "usage: best_test fixture maxRes hdr dv av1 dts truehd mbps\n"); return 2; }
  char *txt = read_all(argv[1]); cJSON *j = txt ? cJSON_Parse(txt) : NULL; if (!j) { fprintf(stderr, "bad fixture\n"); return 2; }
  TvCaps c = { atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]) }; double mbps = atof(argv[8]);
  double dur = cJSON_GetObjectItemCaseSensitive(j, "dur")->valuedouble;
  Stream *all = calloc(2000, sizeof *all); int n = 0; const cJSON *ad, *s;
  /* the streams as the app reads an addon's answer (stremio.c on_streams), without its 40-per-addon cap */
  cJSON_ArrayForEach(ad, cJSON_GetObjectItemCaseSensitive(j, "addons")) cJSON_ArrayForEach(s, cJSON_GetObjectItemCaseSensitive(ad, "streams")) {
    Stream *o = &all[n]; const char *url = jstr(s, "url"), *ih = jstr(s, "infoHash");
    if (url) snprintf(o->url, sizeof o->url, "%s", url); else if (ih) { snprintf(o->url, sizeof o->url, "p2p://%s/0?", ih); o->p2p = 1; } else continue;
    snprintf(o->addon, sizeof o->addon, "%s", jstr(ad, "name")); snprintf(o->name, sizeof o->name, "%s", jstr(s, "name") ? jstr(s, "name") : "");
    const char *t = jstr(s, "title") ? jstr(s, "title") : jstr(s, "description"); snprintf(o->title, sizeof o->title, "%s", t ? t : "");
    const cJSON *bh = cJSON_GetObjectItemCaseSensitive(s, "behaviorHints"); const char *fn = bh ? jstr(bh, "filename") : NULL; snprintf(o->filename, sizeof o->filename, "%s", fn ? fn : "");
    n++; }
  printf("{\"rates\":[");
  for (int i = 0; i < n; i++) { BestRate r; if (i) putchar(',');
    if (!best_rate(&all[i], &c, mbps, dur, &r)) { printf("null"); continue; }
    printf("[%.6f,%.6f,%.6f,", r.sc, r.need, r.cap); jout(r.sum); putchar(']'); }
  const Stream **l = malloc(n * sizeof *l); for (int i = 0; i < n; i++) l[i] = &all[i];
  BestRate r; const Stream *b = best_pick(l, n, dur, &c, mbps, &r);
  printf("],\"pick\":%d", b ? (int)(b - all) : -1);
  if (b) { printf(",\"sum\":"); jout(r.sum); char info[2048]; best_stream_info(b, info, sizeof info); printf(",\"info\":"); jout(info); }
  printf("}\n");
  free(l); free(all); cJSON_Delete(j); free(txt); return 0;
}
