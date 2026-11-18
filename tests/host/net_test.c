/* core/net.c against a stand-in libcurl: answers of any size, as slow as asked, many at once; allocations made to fail.
   Checks: every request's callback comes exactly once, from net_pump; the memory the answers hold stays within the
   budget (plus the first MB each worker may always take); a refused or oversized answer is a failure (0), never a
   stall or half an answer; a cancel ends a download that is waiting for room; nothing is left counted at the end. */
#define CURL_DISABLE_TYPECHECK
#include "common.h"
#include "core/net.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <SDL2/SDL.h>

/* ---- the stand-ins net.c links against ---- */
Uint32 SDL_GetTicks(void) { return (Uint32)(host_now() * 1000); }
const char *plat_device_name(void) { return "Host test"; }
int plat_mem_total_mb(void) { return 1024; }
void plat_thread_background(int nice) { (void)nice; }
const char *update_current(void) { return "0.0.0"; }

/* allocations of net.c (failalloc.h) */
static int faCountdown, faAll; static long faFailed;
static int fa_hit(void) { if (__atomic_load_n(&faAll, __ATOMIC_ACQUIRE)) { __atomic_add_fetch(&faFailed, 1, __ATOMIC_RELAXED); return 1; }
  int c = __atomic_load_n(&faCountdown, __ATOMIC_ACQUIRE); if (c <= 0) return 0;
  if (__atomic_sub_fetch(&faCountdown, 1, __ATOMIC_ACQ_REL) == 0) { __atomic_add_fetch(&faFailed, 1, __ATOMIC_RELAXED); return 1; } return 0; }
void *fa_malloc(size_t n) { return fa_hit() ? NULL : malloc(n); }
void *fa_calloc(size_t n, size_t k) { return fa_hit() ? NULL : calloc(n, k); }
void *fa_realloc(void *p, size_t n) { return fa_hit() ? NULL : realloc(p, n); }
char *fa_strdup(const char *s) { return fa_hit() ? NULL : strdup(s); }

/* ---- a libcurl that answers "fake://x?n=<bytes>&ms=<ms between 16 KB pieces>&code=<status>&json=1" ---- */
typedef struct { char url[900]; curl_write_callback wf; void *wd; curl_xferinfo_callback xf; void *xd; long code; } Fake;
static int live, liveMax; static pthread_mutex_t fmu = PTHREAD_MUTEX_INITIALIZER;
CURL *curl_easy_init(void) { return calloc(1, sizeof(Fake)); }
void curl_easy_reset(CURL *h) { memset(h, 0, sizeof(Fake)); }
void curl_easy_cleanup(CURL *h) { free(h); }
CURLcode curl_global_init(long f) { (void)f; return CURLE_OK; }
CURLSH *curl_share_init(void) { return NULL; }
CURLSHcode curl_share_setopt(CURLSH *s, CURLSHoption o, ...) { (void)s; (void)o; return CURLSHE_OK; }
struct curl_slist *curl_slist_append(struct curl_slist *l, const char *s) { struct curl_slist *n = calloc(1, sizeof *n); n->data = strdup(s); if (!l) return n; struct curl_slist *e = l; while (e->next) e = e->next; e->next = n; return l; }
void curl_slist_free_all(struct curl_slist *l) { while (l) { struct curl_slist *n = l->next; free(l->data); free(l); l = n; } }
CURLcode curl_easy_setopt(CURL *h, CURLoption o, ...) {
  Fake *c = h; va_list a; va_start(a, o);
  if (o == CURLOPT_URL) snprintf(c->url, sizeof c->url, "%s", va_arg(a, const char *));
  else if (o == CURLOPT_WRITEFUNCTION) c->wf = va_arg(a, curl_write_callback);
  else if (o == CURLOPT_WRITEDATA) c->wd = va_arg(a, void *);
  else if (o == CURLOPT_XFERINFOFUNCTION) c->xf = va_arg(a, curl_xferinfo_callback);
  else if (o == CURLOPT_XFERINFODATA) c->xd = va_arg(a, void *);
  va_end(a); return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *h, CURLINFO i, ...) { Fake *c = h; va_list a; va_start(a, i); if (i == CURLINFO_RESPONSE_CODE) *va_arg(a, long *) = c->code; va_end(a); return CURLE_OK; }
static long qnum(const char *u, const char *k, long def) { const char *p = strstr(u, k); return p ? atol(p + strlen(k)) : def; }
CURLcode curl_easy_perform(CURL *h) {
  Fake *c = h; long n = qnum(c->url, "n=", 1000), ms = qnum(c->url, "ms=", 0); int json = strstr(c->url, "json=1") != NULL; c->code = qnum(c->url, "code=", 200);
  pthread_mutex_lock(&fmu); live++; if (live > liveMax) liveMax = live; pthread_mutex_unlock(&fmu);
  static const char piece[] = "{\"name\":\"a stream\",\"url\":\"http://x/y\"},"; char buf[16384]; CURLcode rc = CURLE_OK; long sent = 0;
  if (json) { buf[0] = '['; size_t k = 1; while (k + sizeof piece < sizeof buf) { memcpy(buf + k, piece, sizeof piece - 1); k += sizeof piece - 1; } for (; k < sizeof buf; k++) buf[k] = ' '; }
  else memset(buf, 'x', sizeof buf);
  while (sent < n) {
    if (c->xf && c->xf(c->xd, 0, 0, 0, 0)) { rc = CURLE_ABORTED_BY_CALLBACK; break; }
    size_t k = (size_t)(n - sent < (long)sizeof buf ? n - sent : (long)sizeof buf);
    char *p = buf; char last[16384]; if (json && sent + (long)k >= n) { memcpy(last, buf, k); if (k >= 2) { last[k - 2] = '{'; last[k - 1] = '}'; } size_t z = k; while (z > 1 && last[z - 3] != ',' && z > 3) z--; p = last; } /* (shaped to end as valid JSON: see the json check) */
    if (c->wf && c->wf(p, 1, k, c->wd) != k) { rc = CURLE_WRITE_ERROR; break; }
    sent += (long)k; if (ms) host_sleep_ms((int)ms); }
  pthread_mutex_lock(&fmu); live--; pthread_mutex_unlock(&fmu);
  return rc;
}

/* ---- the callbacks ---- */
typedef struct { int calls, status; size_t len; int json; } Got;
static void on_done(int st, const unsigned char *b, size_t n, void *ud) {
  Got *g = ud; g->calls++; g->status = st; g->len = n; (void)b;
  if (g->json) { cJSON *j = net_json(b); g->json = j ? 2 : -1; net_json_free(j); } }
static void pump_until(Got *g, int n, double secs) { double t0 = host_now(); for (;;) { net_pump(); int all = 1; for (int i = 0; i < n; i++) if (!g[i].calls) all = 0; if (all || host_now() - t0 > secs) return; host_sleep_ms(5); } }
static size_t used_now(void) { size_t u; net_mem_stats(&u, NULL, NULL, NULL); return u; }

int main(void) {
  CHECK(net_init(NULL));
  { /* plain answers, in order, once each */
    Got g[3] = { 0 }; for (int i = 0; i < 3; i++) CHECK(net_get("fake://a?n=2000", on_done, &g[i]) != 0);
    pump_until(g, 3, 5); for (int i = 0; i < 3; i++) { CHECK(g[i].calls == 1); CHECK(g[i].status == 200); CHECK(g[i].len == 2000); }
    CHECK(used_now() == 0); CHECK(net_pending() == 0); }
  { /* 10 slow stream addons at once: every slow worker busy (it once wrote past its table from the 7th on) */
    Got g[10] = { 0 }; liveMax = 0;
    for (int i = 0; i < 10; i++) CHECK(net_get_timeout("fake://s?n=300000&ms=20", 45000, on_done, &g[i]) != 0);
    Got q[5] = { 0 }; for (int i = 0; i < 5; i++) net_get("fake://q?n=100", on_done, &q[i]); /* quick ones beside them */
    pump_until(q, 5, 10); pump_until(g, 10, 20);
    int ok = 1; for (int i = 0; i < 10; i++) if (g[i].calls != 1 || g[i].status != 200 || g[i].len != 300000) ok = 0;
    for (int i = 0; i < 5; i++) if (q[i].calls != 1 || q[i].status != 200) ok = 0;
    CHECKF(ok, "every answer once and whole"); CHECKF(liveMax >= 10, "%d at once", liveMax);
    host_sleep_ms(50); net_pump(); for (int i = 0; i < 10; i++) CHECK(g[i].calls == 1);
    CHECK(used_now() == 0); CHECK(net_pending() == 0); }
  { /* an answer past the most one may be: a failure, never half of it */
    Got g = { 0 }; net_get("fake://big?n=40000000", on_done, &g); pump_until(&g, 1, 20);
    CHECK(g.calls == 1); CHECKF(g.status == 0, "status %d", g.status); CHECK(used_now() == 0); }
  { /* a small budget, many answers kept unread: they wait for room, then the ones that find none fail; the budget holds */
    net_mem_set_budget(8u << 20); size_t peak0; unsigned ref0; net_mem_stats(NULL, &peak0, NULL, &ref0);
    Got g[10] = { 0 }; for (int i = 0; i < 10; i++) net_get_timeout("fake://m?n=3000000&ms=1", 45000, on_done, &g[i]);
    host_sleep_ms(4500); /* nobody hands answers on meanwhile: the workers wait their 3 s, then give up */
    size_t peak; unsigned ref; net_mem_stats(NULL, &peak, NULL, &ref);
    CHECKF(peak <= (8u << 20) + 10u * (1u << 20) + (6u << 20), "peak %u KB", (unsigned)(peak >> 10)); /* budget + first MB per worker + one doubling's slack */
    CHECKF(ref > ref0, "some refused (%u)", ref - ref0);
    pump_until(g, 10, 30); int okN = 0, failN = 0; for (int i = 0; i < 10; i++) { CHECK(g[i].calls == 1); if (g[i].status == 200 && g[i].len == 3000000) okN++; else if (g[i].status == 0) failN++; }
    CHECKF(okN + failN == 10 && failN > 0 && okN > 0, "%d whole, %d failed", okN, failN);
    CHECK(used_now() == 0); net_mem_set_budget(0); }
  { /* a cancel while the download waits for room: it ends within the wait, as cancelled */
    net_mem_set_budget(2u << 20); Got hold = { 0 }, g = { 0 };
    net_get_timeout("fake://h?n=1800000", 45000, on_done, &hold); host_sleep_ms(300); /* it holds the budget, unread */
    unsigned id = net_get_timeout("fake://c?n=3000000&ms=1", 45000, on_done, &g); host_sleep_ms(200);
    double t0 = host_now(); net_cancel(id); pump_until(&g, 1, 5);
    CHECK(g.calls == 1); CHECKF(g.status == -1, "status %d", g.status); CHECKF(host_now() - t0 < 1.5, "%.2f s", host_now() - t0);
    pump_until(&hold, 1, 5); CHECK(hold.calls == 1); CHECK(used_now() == 0); net_mem_set_budget(0); }
  { /* JSON parsed on the worker: counted while it waits, handed over in the callback */
    Got g = { 0 }; g.json = 1; net_get("fake://j?n=40000&json=1", on_done, &g); pump_until(&g, 1, 5);
    CHECK(g.calls == 1); CHECK(g.status == 200); CHECKF(g.json == 2 || g.json == -1, "json %d", g.json); CHECK(used_now() == 0); }
  { /* no memory at all for a request: its callback still comes, once, with 0 */
    Got g = { 0 }; __atomic_store_n(&faAll, 1, __ATOMIC_RELEASE); unsigned id = net_get("fake://a?n=10", on_done, &g); __atomic_store_n(&faAll, 0, __ATOMIC_RELEASE);
    CHECK(id != 0); pump_until(&g, 1, 2); CHECK(g.calls == 1); CHECK(g.status == 0); CHECK(net_pending() == 0); }
  for (int k = 1; k <= 2; k++) {
    Got g = { 0 }; __atomic_store_n(&faCountdown, k, __ATOMIC_RELEASE);
    net_get_first("fake://priority", on_done, &g); pump_until(&g, 1, 5);
    CHECK(g.calls == 1 && g.status == 0); CHECK(net_pending() == 0);
  }
  { /* each allocation in turn made to fail: one callback each, nothing stuck, nothing counted */
    int bad = 0; for (int k = 1; k <= 12; k++) { Got g = { 0 }; __atomic_store_n(&faCountdown, k, __ATOMIC_RELEASE);
      net_get_hdr("fake://f?n=70000", "X-Test: 1", on_done, &g); pump_until(&g, 1, 5); __atomic_store_n(&faCountdown, 0, __ATOMIC_RELEASE);
      host_sleep_ms(20); net_pump(); if (g.calls != 1) { bad++; printf("  allocation %d: %d callback(s)\n", k, g.calls); } }
    CHECK(!bad); CHECKF(faFailed > 5, "%ld failed", faFailed); CHECK(net_pending() == 0); CHECK(used_now() == 0); }
  { /* a sync fetch (the pictures' workers): counted while it comes, then the caller's */
    size_t len = 0; int st = 0; unsigned char *b = net_fetch_sync("fake://p?n=50000", &len, &st, 5000);
    CHECK(b && len == 50000 && st == 200); free(b); CHECK(used_now() == 0); }
  return host_done("net");
}
