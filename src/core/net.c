#include "platform/plat.h"
#include "core/net.h"
#include "core/update.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>
#if !defined(SB_MAC)
#include <dlfcn.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ---- the curl entry points, resolved at run time on the TV -------------------------------- */
static CURL *(*f_easy_init)(void);
static CURLcode (*f_easy_setopt)(CURL *, CURLoption, ...);
static CURLcode (*f_easy_perform)(CURL *);
static CURLcode (*f_easy_getinfo)(CURL *, CURLINFO, ...);
static void (*f_easy_cleanup)(CURL *);
static void (*f_easy_reset)(CURL *); /* (may be missing on a very old libcurl: then a handle per fetch, as before) */
static struct curl_slist *(*f_slist_append)(struct curl_slist *, const char *);
static void (*f_slist_free_all)(struct curl_slist *);
static CURLcode (*f_global_init)(long);
static void (*f_easy_reset)(CURL *); /* (optional: without it every request opens its own connection, as before) */
static char caPath[1024];
/* one cache for every worker: names looked up, TLS sessions and (Android) open connections, so a request to an
   addon another worker has just reached reuses its connection, or at least its TLS session, not a new handshake */
static CURLSH *(*f_share_init)(void); static CURLSHcode (*f_share_setopt)(CURLSH *, CURLSHoption, ...);
static CURLSH *share; static pthread_mutex_t shMu[CURL_LOCK_DATA_LAST + 1];
static void sh_lock(CURL *h, curl_lock_data d, curl_lock_access a, void *u) { (void)h; (void)a; (void)u; if ((int)d >= 0 && d <= CURL_LOCK_DATA_LAST) pthread_mutex_lock(&shMu[d]); }
static void sh_unlock(CURL *h, curl_lock_data d, void *u) { (void)h; (void)u; if ((int)d >= 0 && d <= CURL_LOCK_DATA_LAST) pthread_mutex_unlock(&shMu[d]); }
static void share_setup(void) {
  if (!f_share_init || !f_share_setopt || !(share = f_share_init())) return;
  for (int i = 0; i <= CURL_LOCK_DATA_LAST; i++) pthread_mutex_init(&shMu[i], NULL);
  f_share_setopt(share, CURLSHOPT_LOCKFUNC, sh_lock); f_share_setopt(share, CURLSHOPT_UNLOCKFUNC, sh_unlock);
  f_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS); f_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
#if defined(SB_MAC)
  f_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT); /* (the curl built in: its shared pool is sound; the TV's own older one keeps its pools per worker) */
#endif
}
/* "Origin/1.0.0 (webOS; native)": the running version, the platform it runs on */
static const char *ua(void) { static char u[96]; if (!u[0]) { const char *d = plat_device_name(); snprintf(u, sizeof u, "Origin/%s (%s; native)", update_current(), strstr(d, "webOS") ? "webOS" : strstr(d, "Android") ? "Android TV" : "Mac"); } return u; }

static int resolve(void) {
#if defined(SB_MAC)
  f_easy_init = curl_easy_init; f_easy_setopt = curl_easy_setopt; f_easy_perform = curl_easy_perform; f_easy_getinfo = curl_easy_getinfo;
  f_easy_cleanup = curl_easy_cleanup; f_slist_append = curl_slist_append; f_slist_free_all = curl_slist_free_all; f_global_init = curl_global_init; f_easy_reset = curl_easy_reset;
  f_share_init = curl_share_init; f_share_setopt = curl_share_setopt;
  return 1;
#else
  static const char *names[] = { "libcurl.so.5", "libcurl.so.4", "libcurl.so", NULL };
  void *h = NULL; for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW);
  if (!h) { fprintf(stderr, "[net] no libcurl: %s\n", dlerror()); return 0; }
#define L(v, n) do { *(void **)(&v) = dlsym(h, n); if (!v) { fprintf(stderr, "[net] missing %s\n", n); return 0; } } while (0)
  L(f_easy_init, "curl_easy_init"); L(f_easy_setopt, "curl_easy_setopt"); L(f_easy_perform, "curl_easy_perform"); L(f_easy_getinfo, "curl_easy_getinfo");
  L(f_easy_cleanup, "curl_easy_cleanup"); *(void **)(&f_easy_reset) = dlsym(h, "curl_easy_reset"); L(f_slist_append, "curl_slist_append"); L(f_slist_free_all, "curl_slist_free_all"); L(f_global_init, "curl_global_init");
#undef L
  *(void **)(&f_easy_reset) = dlsym(h, "curl_easy_reset");
  *(void **)(&f_share_init) = dlsym(h, "curl_share_init"); *(void **)(&f_share_setopt) = dlsym(h, "curl_share_setopt"); /* (optional) */
  return 1;
#endif
}

/* ---- requests ------------------------------------------------------------------------------- */
typedef struct Req {
  unsigned id; char *url, *hdr, *post, *ctype, *method; size_t postLen; NetCb cb; void *ud; long timeoutMs; cJSON *json; /* postLen: a raw body (net_send), else post is text; json: the body already parsed on the worker */
  int status; unsigned char *body; size_t len, cap; int cancelled, done; /* cancelled: written by net_cancel, read by its worker (atomics) */
  size_t held, jsonHeld; int refused; /* held: what its body's buffer counts against the budget; jsonHeld: its parsed tree's estimate */
  int size; double total;  /* net_size: only the headers are read */
  int file;                /* net_file: url is a local path */
  struct Req *next;
} Req;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
#define WORKERS 15  /* 0-4 take the quick requests (catalogs, details, the account); 5-14 the slow ones (stream addons, up to
                       45 s each): details never wait behind a debrid addon thinking */
#define SLOW0 5
/* (inflight has one place per worker: it once had 11 for 15 workers, and the 7th stream addon answering at once wrote
   its request over the finished list and the next variables) */
static Req *queue, *queueTail, *slowQ, *slowTail, *finished, *inflight[WORKERS], *fqueue, *fqueueTail;
static pthread_cond_t slowCv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t fcv = PTHREAD_COND_INITIALIZER;
static unsigned nextId = 1; static int npending;
static int is_cancelled(const Req *r) { return __atomic_load_n(&r->cancelled, __ATOMIC_ACQUIRE); }
static void set_cancelled(Req *r) { __atomic_store_n(&r->cancelled, 1, __ATOMIC_RELEASE); }

/* ---- the memory answers hold ----------------------------------------------------------------------------------------
   Every answer's buffer counts by its capacity from its first byte until its callback has returned (or, for the
   synchronous calls of other modules' workers, until the call returns); a JSON answer parsed on the worker counts its
   tree too, at an estimate, until it is handed over. Over the budget, a worker waits a short while for net_pump to hand
   answers on and free them (it holds nothing the main thread needs meanwhile), then gives that answer up: the caller
   sees a failure, never a stall. A request holding nothing yet may always take its first MB, so one answer is never
   starved by the others (the budget is exceeded by at most that per worker). */
#define NET_FIRST_FREE ((size_t)1 << 20)
#define NET_MEM_WAIT_MS 3000
#define JSON_GROWTH 6       /* a parsed cJSON tree is several times the size of its text */
#define BODY_MAX_HARD ((size_t)32 << 20) /* one answer, whatever the budget: nothing the app reads this way comes near */
static pthread_mutex_t memMu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t memCv = PTHREAD_COND_INITIALIZER;
static size_t memUsed, memPeak, memBudget, memBudgetSet; static unsigned memRefused; static Uint32 lastGiveUp;
#define NET_GIVE_UP_GAP_MS 300 /* waiters past their time give up one at a time: what one frees may be room for the next */
static size_t mem_budget(void) { /* under memMu: a sixteenth of the TV's memory, 32 to 96 MB */
  if (memBudgetSet) return memBudgetSet;
  if (!memBudget) { int t = plat_mem_total_mb(); size_t b = t > 0 ? (size_t)t * 1024 * 1024 / 16 : (size_t)64 << 20; if (b < ((size_t)32 << 20)) b = (size_t)32 << 20; if (b > ((size_t)96 << 20)) b = (size_t)96 << 20; memBudget = b; }
  return memBudget; }
static size_t body_max(void) { pthread_mutex_lock(&memMu); size_t b = mem_budget() * 3 / 4; pthread_mutex_unlock(&memMu); return b < BODY_MAX_HARD ? b : BODY_MAX_HARD; }
void net_mem_set_budget(size_t bytes) { pthread_mutex_lock(&memMu); memBudgetSet = bytes; memPeak = memUsed; pthread_cond_broadcast(&memCv); pthread_mutex_unlock(&memMu); }
void net_mem_stats(size_t *used, size_t *peak, size_t *budget, unsigned *refused) {
  pthread_mutex_lock(&memMu); if (used) *used = memUsed; if (peak) *peak = memPeak; if (budget) *budget = mem_budget(); if (refused) *refused = memRefused; pthread_mutex_unlock(&memMu); }
/* n more bytes for r: 1 taken, 0 refused (no room within the wait, or r was cancelled). wait 0: never waits */
static int mem_take(const Req *r, size_t heldNow, size_t n, int wait) {
  if (!n) return 1;
  pthread_mutex_lock(&memMu);
  Uint32 t0 = SDL_GetTicks();
  for (;;) {
    size_t b = mem_budget();
    if (memUsed + n <= b || heldNow + n <= NET_FIRST_FREE) break;
    int cancelled = r && is_cancelled(r); Uint32 now = SDL_GetTicks();
    if (!wait || cancelled || (now - t0 >= NET_MEM_WAIT_MS && now - lastGiveUp >= NET_GIVE_UP_GAP_MS)) { if (!cancelled && wait) lastGiveUp = now; memRefused++; pthread_mutex_unlock(&memMu); return 0; }
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 100 * 1000000L; if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    pthread_cond_timedwait(&memCv, &memMu, &ts); /* (in short steps: a cancel is seen within 0.1 s) */
  }
  memUsed += n; if (memUsed > memPeak) memPeak = memUsed;
  pthread_mutex_unlock(&memMu); return 1;
}
static void mem_give(size_t n) { if (!n) return; pthread_mutex_lock(&memMu); memUsed = n > memUsed ? 0 : memUsed - n; pthread_cond_broadcast(&memCv); pthread_mutex_unlock(&memMu); }

/* a request that could not even be made (no memory for it): its callback still comes, from net_pump, with 0 */
typedef struct { unsigned id; NetCb cb; void *ud; } Failed;
static Failed failQ[64]; static int nfail;
static unsigned fail_later(NetCb cb, void *ud) { /* under no lock */
  pthread_mutex_lock(&mu); unsigned id = 0;
  if (nfail < (int)(sizeof failQ / sizeof *failQ)) { id = nextId++; if (!id) id = nextId++; failQ[nfail].id = id; failQ[nfail].cb = cb; failQ[nfail].ud = ud; nfail++; npending++; }
  pthread_mutex_unlock(&mu);
  fprintf(stderr, "[net] no memory for a request%s\n", id ? ": it fails" : " (and none to say so)");
  return id;
}
static void req_free(Req *r) { if (!r) return; free(r->url); free(r->hdr); free(r->post); free(r->ctype); free(r->method); free(r->body); free(r); }

/* an address or a redirect never leaves the web (no file://, ftp://, gopher://... whatever an addon answers), and an
   answer kept in memory has an end: nothing the app reads this way is near it, and a TV has little to spare */
#define WEB_ONLY ((long)(CURLPROTO_HTTP | CURLPROTO_HTTPS))
static size_t on_data(char *p, size_t s, size_t n, void *ud) {
  Req *r = ud; size_t add = s * n;
  if (is_cancelled(r)) return 0;
  size_t mx = body_max();
  if (r->len + add > mx) { if (!r->refused) fprintf(stderr, "[net] an answer past %u MB is not read on\n", (unsigned)(mx >> 20)); r->refused = 1; return 0; }
  if (r->len + add + 1 > r->cap) {
    size_t need = r->len + add + 1, nc = need < ((size_t)4 << 20) ? need * 2 : need + need / 2; /* (doubling while small, then half again: less slack in a big one) */
    if (nc > mx + 1) nc = mx + 1; if (nc < need) nc = need;
    if (!mem_take(r, r->held, nc - r->cap, 1)) { if (!r->refused) fprintf(stderr, "[net] no room for an answer of %u KB (the answers in hand hold their budget): given up\n", (unsigned)((r->len + add) >> 10)); r->refused = 1; return 0; }
    unsigned char *nb = realloc(r->body, nc); if (!nb) { mem_give(nc - r->cap); r->refused = 1; return 0; }
    r->held += nc - r->cap; r->body = nb; r->cap = nc; }
  memcpy(r->body + r->len, p, add); r->len += add; r->body[r->len] = 0; return add;
}
static int on_progress(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) { (void)dt; (void)dn; (void)ut; (void)un; return is_cancelled((Req *)ud); }
/* net_size: "Content-Range: bytes 0-0/<total>" (a 200 instead: its Content-Length); each redirect starts over */
static size_t on_header(char *p, size_t s, size_t n, void *ud) {
  Req *r = ud; size_t len = s * n; char h[256]; size_t k = len < sizeof h - 1 ? len : sizeof h - 1; memcpy(h, p, k); h[k] = 0;
  if (!strncmp(h, "HTTP/", 5)) { r->total = 0; r->status = atoi(strchr(h, ' ') ? strchr(h, ' ') + 1 : "0"); }
  else if (!strncasecmp(h, "content-range:", 14)) { char *sl = strrchr(h, '/'); if (sl) r->total = atof(sl + 1); }
  else if (!strncasecmp(h, "content-length:", 15) && r->status == 200) r->total = atof(h + 15);
  return len;
}
static size_t on_size_data(char *p, size_t s, size_t n, void *ud) { (void)p; (void)s; (void)n; (void)ud; return 0; } /* never the body: a 200 would be the whole film */
/* a Tailscale name (*.ts.net, an addon on your own machine): the app's jail on the TV has its own resolver, which never
   asks Tailscale; when it cannot find the name, Tailscale's own resolver (100.100.100.100) is asked and curl given its answer */
static size_t dns_skip(const unsigned char *r, size_t i, size_t n) { while (i < n && r[i] && (r[i] & 0xc0) != 0xc0) i += (size_t)r[i] + 1; return i >= n ? n : (r[i] & 0xc0) == 0xc0 ? i + 2 : i + 1; }
static int ts_query(const char *host, char *ip, size_t cap) {
  unsigned char q[300] = { 0x5a, 0x17, 1, 0, 0, 1 }; size_t n = 12;
  for (const char *p = host; *p;) { size_t l = strcspn(p, "."); if (!l || l > 63 || n + l + 6 > sizeof q) return 0; q[n++] = (unsigned char)l; memcpy(q + n, p, l); n += l; p += l; if (*p) p++; }
  q[n++] = 0; q[n++] = 0; q[n++] = 1; q[n++] = 0; q[n++] = 1; /* A, IN */
  int s = socket(AF_INET, SOCK_DGRAM, 0); if (s < 0) return 0;
  struct timeval tv = { 1, 500000 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof tv);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(53); a.sin_addr.s_addr = inet_addr("100.100.100.100");
  unsigned char r[512]; ssize_t got = -1; if (sendto(s, q, n, 0, (struct sockaddr *)&a, sizeof a) == (ssize_t)n) got = recv(s, r, sizeof r, 0); close(s);
  if (got < 12 || r[0] != 0x5a || r[1] != 0x17 || (r[3] & 0x0f)) return 0;
  size_t g = (size_t)got, i = dns_skip(r, 12, g) + 4; int an = r[6] << 8 | r[7];
  for (int k = 0; k < an && i < g; k++) { i = dns_skip(r, i, g); if (i + 10 > g) break; int type = r[i] << 8 | r[i + 1]; size_t dl = (size_t)(r[i + 8] << 8 | r[i + 9]); i += 10; if (i + dl > g) break;
    if (type == 1 && dl == 4) { snprintf(ip, cap, "%u.%u.%u.%u", r[i], r[i + 1], r[i + 2], r[i + 3]); return 1; } i += dl; }
  return 0;
}
static struct curl_slist *ts_pin(CURL *c, const char *url) {
  const char *h = url ? strstr(url, "://") : NULL; if (!h) return NULL; h += 3; char host[256]; size_t l = strcspn(h, ":/?#"); if (l < 8 || l >= sizeof host) return NULL;
  memcpy(host, h, l); host[l] = 0; if (strcasecmp(host + l - 7, ".ts.net") || strchr(host, '@')) return NULL;
  struct addrinfo hint, *res = NULL; memset(&hint, 0, sizeof hint); hint.ai_socktype = SOCK_STREAM; if (!getaddrinfo(host, NULL, &hint, &res)) { freeaddrinfo(res); return NULL; } /* found as it is */
  char ip[48]; if (!ts_query(host, ip, sizeof ip)) return NULL;
  int port = h[l] == ':' ? atoi(h + l + 1) : !strncasecmp(url, "https", 5) ? 443 : 80; char e[320]; snprintf(e, sizeof e, "%s:%d:%s", host, port, ip);
  struct curl_slist *s = f_slist_append(NULL, e); if (s) f_easy_setopt(c, CURLOPT_RESOLVE, s); return s;
}
/* why a request failed, when it is a name that wasn't found (it said "no answer"); a .ts.net name stays out of the logs */
static void log_fail(CURLcode rc, const char *url) {
  if (rc != CURLE_COULDNT_RESOLVE_HOST || !url) return; const char *h = strstr(url, "://"); if (!h) return; h += 3;
  size_t l = strcspn(h, "/?#"); const char *at = memchr(h, '@', l); if (at) { l -= (size_t)(at + 1 - h); h = at + 1; } l = strcspn(h, ":/?#"); if (!l || l > 255) return;
  int ts = l > 7 && !strncasecmp(h + l - 7, ".ts.net", 7); if (ts) fprintf(stderr, "[net] couldn't look up a .ts.net address\n"); else fprintf(stderr, "[net] couldn't look up %.*s\n", (int)l, h); }
/* each worker keeps its handle: curl then keeps the connections it opened (and their TLS sessions and DNS answers),
   and the next request to the same addon or API skips the handshake, the slowest part of a request on a TV */
static CURL *handles[WORKERS];
static void perform(Req *r, int slot) {
  CURL *c = NULL; if (f_easy_reset && slot >= 0 && slot < WORKERS) { c = handles[slot]; if (c) f_easy_reset(c); else c = handles[slot] = f_easy_init(); } else c = f_easy_init();
  if (!c) { r->status = 0; return; } int kept = c == (slot >= 0 && slot < WORKERS ? handles[slot] : NULL);
  struct curl_slist *hs = NULL, *pin = ts_pin(c, r->url);
  f_easy_setopt(c, CURLOPT_URL, r->url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data); f_easy_setopt(c, CURLOPT_WRITEDATA, r);
  f_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress); f_easy_setopt(c, CURLOPT_XFERINFODATA, r); f_easy_setopt(c, CURLOPT_NOPROGRESS, 0L); /* a cancelled request lets go of its worker at once, not when a slow addon finally answers */
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, r->timeoutMs > 0 ? r->timeoutMs : 20000L);
  f_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L); f_easy_setopt(c, CURLOPT_DNS_CACHE_TIMEOUT, 600L); f_easy_setopt(c, CURLOPT_BUFFERSIZE, 256L * 1024);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); if (!r->size) f_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  if (share) f_easy_setopt(c, CURLOPT_SHARE, share);
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  if (r->size) { f_easy_setopt(c, CURLOPT_TIMEOUT_MS, 10000L); f_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header); f_easy_setopt(c, CURLOPT_HEADERDATA, r); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_size_data); hs = f_slist_append(hs, "Range: bytes=0-0"); }
  /* one header per line; one the caller names (User-Agent...) replaces curl's own */
  for (char *h = r->hdr, *nl; h && *h; h = nl ? nl + 1 : NULL) { nl = strchr(h, '\n'); if (nl) *nl = 0; if (*h) hs = f_slist_append(hs, h); if (nl) *nl = '\n'; }
  if (r->post) { if (r->ctype) { if (*r->ctype) { char ct[160]; snprintf(ct, sizeof ct, "Content-Type: %s", r->ctype); hs = f_slist_append(hs, ct); } } else if (!r->postLen) hs = f_slist_append(hs, "Content-Type: application/json");
    f_easy_setopt(c, CURLOPT_POSTFIELDS, r->post); f_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(r->postLen ? r->postLen : strlen(r->post))); }
  if (r->method) f_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r->method);
  if (hs) f_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
  CURLcode rc = f_easy_perform(c);
  long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  r->status = rc == CURLE_OK ? (int)code : 0; log_fail(rc, r->url);
  if (r->size) { char t[32]; int k = snprintf(t, sizeof t, "%.0f", r->total > 0 ? r->total : 0); free(r->body); mem_give(r->held); r->held = 0; r->cap = 0; r->body = (unsigned char *)strdup(t); r->len = r->body ? (size_t)k : 0; r->status = (int)code; } /* the server's answer even without a size (a 404 says why a film will not play); 0 = no answer */
  if (r->refused) r->status = 0; /* cut short for want of room: a failure, never half an answer */
  if (!kept) f_easy_cleanup(c); if (hs) f_slist_free_all(hs); if (pin) f_slist_free_all(pin);
}
static void parse_body(Req *r) {
  if (r->body && r->len > 1 && r->len < 16u * 1024 * 1024 && r->status >= 200 && r->status < 300) { const unsigned char *b = r->body; while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t') b++;
    if (*b == '{' || *b == '[') { size_t est = r->len * JSON_GROWTH;
      if (!mem_take(r, r->held, est, 0)) return; /* no room for the tree now: the callback parses it on the main thread (net_json), after the answers before it are gone */
      r->jsonHeld = est; r->json = cJSON_ParseWithLength((const char *)r->body, r->len);
      if (!r->json) { mem_give(r->jsonHeld); r->jsonHeld = 0; } } }
}
/* disk copies get a thread of their own: behind six stream addons (45 s each) a cached title page would wait as long */
static void *file_worker(void *arg) { plat_thread_background(5);
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&mu);
    while (!fqueue) pthread_cond_wait(&fcv, &mu);
    Req *r = fqueue; fqueue = r->next; if (!fqueue) fqueueTail = NULL; r->next = NULL;
    pthread_mutex_unlock(&mu);
    if (!is_cancelled(r)) { FILE *f = r->url[0] ? fopen(r->url, "rb") : NULL; r->status = 404;
      if (f) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n > 0 && (size_t)n < body_max() && mem_take(r, 0, (size_t)n + 1, 1)) { r->held = (size_t)n + 1; r->cap = r->held;
          if ((r->body = malloc((size_t)n + 1)) && fread(r->body, 1, (size_t)n, f) == (size_t)n) { r->body[n] = 0; r->len = (size_t)n; r->status = 200; } }
        fclose(f); }
      parse_body(r); }
    pthread_mutex_lock(&mu);
    r->done = 1; r->next = finished; finished = r;
    pthread_mutex_unlock(&mu);
  }
  return NULL;
}
unsigned net_file(const char *path, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r); if (r) r->url = strdup(path ? path : ""); if (!r || !r->url) { req_free(r); return fail_later(cb, ud); }
  r->cb = cb; r->ud = ud; r->file = 1;
  pthread_mutex_lock(&mu);
  r->id = nextId++; if (fqueueTail) fqueueTail->next = r; else fqueue = r; fqueueTail = r; npending++;
  pthread_cond_signal(&fcv); pthread_mutex_unlock(&mu);
  return r->id;
}
static void *worker(void *arg) { plat_thread_background(5);
  int slot = (int)(size_t)arg;
  for (;;) {
    pthread_mutex_lock(&mu);
    if (slot >= SLOW0) while (!slowQ) pthread_cond_wait(&slowCv, &mu); else while (!queue) pthread_cond_wait(&cv, &mu);
    Req *r; if (slot >= SLOW0) { r = slowQ; slowQ = r->next; if (!slowQ) slowTail = NULL; } else { r = queue; queue = r->next; if (!queue) queueTail = NULL; }
    r->next = NULL; inflight[slot] = r;
    pthread_mutex_unlock(&mu);
    if (!is_cancelled(r)) { perform(r, slot);
      /* a JSON answer is parsed here, off the drawing thread (catalogs, metadata, streams: tens of ms each on a TV) */
      if (!r->refused && !is_cancelled(r)) parse_body(r); }
    if (r->refused || is_cancelled(r)) { free(r->body); r->body = NULL; r->len = r->cap = 0; mem_give(r->held); r->held = 0; } /* (its callback gets no body: what it held is free for the others now, not when it is handed on) */
    pthread_mutex_lock(&mu);
    inflight[slot] = NULL; r->done = 1; r->next = finished; finished = r;
    pthread_mutex_unlock(&mu);
  }
  return NULL;
}
#if !defined(SB_MAC)
/* OpenSSL 1.0 (the TV's, under its libcurl) is safe across threads only with locks the app hands it: without them
   its shared tables were changed by two requests at once, and a long session ended in a crash inside it (a bad
   pointer in memcmp, called from libcrypto). 1.1 and later lock by themselves (no CRYPTO_num_locks to find): left as
   they are, as is a library someone already gave locks to */
static pthread_mutex_t *sslLocks;
static void ssl_lock(int mode, int n, const char *file, int line) { (void)file; (void)line; if (mode & 1) pthread_mutex_lock(&sslLocks[n]); else pthread_mutex_unlock(&sslLocks[n]); }
static unsigned long ssl_id(void) { return (unsigned long)pthread_self(); }
static void (*sslSetNum)(void *, unsigned long);
static void ssl_threadid(void *id) { sslSetNum(id, (unsigned long)pthread_self()); } /* (each thread its own id: with one id for all, OpenSSL took them for one thread and they blocked each other) */
static void ssl_locks(void) {
  static const char *names[] = { "libcrypto.so.1.0.0", "libcrypto.so.1.0.2", "libcrypto.so.10", "libcrypto.so", NULL };
  void *h = NULL; for (int i = 0; names[i] && !h; i++) h = dlopen(names[i], RTLD_NOW | RTLD_NOLOAD); /* the one curl brought in */
  if (!h) { fprintf(stderr, "[net] tls: no OpenSSL 1.0 loaded (nothing to lock)\n"); return; }
  int (*num)(void) = NULL; void (*setLock)(void (*)(int, int, const char *, int)) = NULL; void *(*getLock)(void) = NULL; void (*setId)(unsigned long (*)(void)) = NULL; int (*setTid)(void (*)(void *)) = NULL;
  *(void **)(&num) = dlsym(h, "CRYPTO_num_locks"); *(void **)(&setLock) = dlsym(h, "CRYPTO_set_locking_callback"); *(void **)(&getLock) = dlsym(h, "CRYPTO_get_locking_callback");
  *(void **)(&setId) = dlsym(h, "CRYPTO_set_id_callback"); *(void **)(&setTid) = dlsym(h, "CRYPTO_THREADID_set_callback");
  if (!num || !setLock) { fprintf(stderr, "[net] tls: OpenSSL locks by itself\n"); return; }
  if (getLock && getLock()) { fprintf(stderr, "[net] tls: OpenSSL already has locks\n"); return; }
  int n = num(); if (n <= 0) return; sslLocks = calloc((size_t)n, sizeof *sslLocks); if (!sslLocks) return;
  for (int i = 0; i < n; i++) pthread_mutex_init(&sslLocks[i], NULL);
  *(void **)(&sslSetNum) = dlsym(h, "CRYPTO_THREADID_set_numeric"); /* from the library itself: it is not in the global names */
  if (setTid && sslSetNum) setTid(ssl_threadid); else if (setId) setId(ssl_id); else { fprintf(stderr, "[net] tls: no thread id call: left as it was\n"); return; }
  setLock(ssl_lock); fprintf(stderr, "[net] tls: OpenSSL 1.0 given %d locks for the request threads\n", n);
}
#endif
int net_init(const char *ca) {
  (void)ua(); /* made once here, before any worker thread reads it */
  if (!resolve()) return 0;
#if !defined(SB_MAC)
  ssl_locks(); /* before the first request and the worker threads */
#endif
  f_global_init(CURL_GLOBAL_ALL);
  share_setup();
  if (ca) snprintf(caPath, sizeof caPath, "%s", ca);
  int quick = 0, slow = 0;
  for (int i = 0; i < WORKERS; i++) { pthread_t t; if (pthread_create(&t, NULL, worker, (void *)(size_t)i)) continue; pthread_detach(t); if (i < SLOW0) quick++; else slow++; }
  { pthread_t t; if (!pthread_create(&t, NULL, file_worker, NULL)) pthread_detach(t); else fprintf(stderr, "[net] no thread for disk reads\n"); }
  if (quick < SLOW0 || slow < WORKERS - SLOW0) fprintf(stderr, "[net] only %d + %d of %d request threads started\n", quick, slow, WORKERS);
  { size_t b; net_mem_stats(NULL, NULL, &b, NULL); fprintf(stderr, "[net] answers may hold %u MB at once (one at most %u MB)\n", (unsigned)(b >> 20), (unsigned)(body_max() >> 20)); }
  return quick > 0 && slow > 0;
}
static void push_req(Req *r) { /* under mu */
  if (r->timeoutMs > 20000) { if (slowTail) slowTail->next = r; else slowQ = r; slowTail = r; pthread_cond_signal(&slowCv); }
  else { if (queueTail) queueTail->next = r; else queue = r; queueTail = r; pthread_cond_signal(&cv); }
}
/* one that is waited on as the user types (search suggestions): ahead of everything queued */
unsigned net_get_first(const char *url, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r);
  if (r) r->url = strdup(url ? url : "");
  if (!r || !r->url) { req_free(r); return fail_later(cb, ud); }
  r->cb = cb; r->ud = ud;
  pthread_mutex_lock(&mu); r->id = nextId++; r->next = queue; queue = r; if (!queueTail) queueTail = r; npending++; pthread_cond_signal(&cv); pthread_mutex_unlock(&mu); return r->id; }
static unsigned enqueue_x(const char *url, const char *hdr, const char *post, const char *ctype, long timeoutMs, int size, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r);
  if (r) { r->url = strdup(url ? url : ""); r->hdr = hdr ? strdup(hdr) : NULL; r->post = post ? strdup(post) : NULL; r->ctype = ctype ? strdup(ctype) : NULL; }
  if (!r || !r->url || (hdr && !r->hdr) || (post && !r->post) || (ctype && !r->ctype)) { req_free(r); return fail_later(cb, ud); }
  r->cb = cb; r->ud = ud; r->timeoutMs = timeoutMs; r->size = size;
  pthread_mutex_lock(&mu);
  r->id = nextId++; push_req(r); npending++;
  pthread_mutex_unlock(&mu);
  return r->id;
}
static unsigned enqueue_c(const char *url, const char *hdr, const char *post, const char *ctype, long timeoutMs, NetCb cb, void *ud) { return enqueue_x(url, hdr, post, ctype, timeoutMs, 0, cb, ud); }
static unsigned enqueue_t(const char *url, const char *hdr, const char *post, long timeoutMs, NetCb cb, void *ud) { return enqueue_x(url, hdr, post, NULL, timeoutMs, 0, cb, ud); }
static unsigned enqueue(const char *url, const char *hdr, const char *post, NetCb cb, void *ud) { return enqueue_t(url, hdr, post, 0, cb, ud); }
unsigned net_get_timeout(const char *url, int timeoutMs, NetCb cb, void *ud) { return enqueue_t(url, NULL, NULL, timeoutMs, cb, ud); }
unsigned net_get(const char *url, NetCb cb, void *ud) { return enqueue(url, NULL, NULL, cb, ud); }
unsigned net_get_hdr(const char *url, const char *hdr, NetCb cb, void *ud) { return enqueue(url, hdr, NULL, cb, ud); }
unsigned net_size(const char *url, NetCb cb, void *ud) { return enqueue_x(url, NULL, NULL, NULL, 10000, 1, cb, ud); }
unsigned net_post_json(const char *url, const char *json, NetCb cb, void *ud) { return enqueue(url, NULL, json, cb, ud); }
unsigned net_post_hdr_t(const char *url, const char *body, const char *contentType, const char *headers, int timeoutMs, NetCb cb, void *ud) {
  return enqueue_c(url, headers, body ? body : "", contentType ? contentType : "", timeoutMs, cb, ud);
}
unsigned net_send(const char *method, const char *url, const char *headers, const void *body, size_t len, int timeoutMs, NetCb cb, void *ud) {
  Req *r = calloc(1, sizeof *r);
  if (r) { r->url = strdup(url ? url : ""); r->hdr = headers ? strdup(headers) : NULL; r->method = strcmp(method, "POST") ? strdup(method) : NULL; r->post = malloc(len + 1); }
  if (!r || !r->url || (headers && !r->hdr) || (strcmp(method, "POST") && !r->method) || !r->post) { req_free(r); return fail_later(cb, ud); }
  r->cb = cb; r->ud = ud; r->timeoutMs = timeoutMs;
  if (len) memcpy(r->post, body, len); r->post[len] = 0; r->postLen = len;
  pthread_mutex_lock(&mu);
  r->id = nextId++; push_req(r); npending++;
  pthread_mutex_unlock(&mu);
  return r->id;
}
void net_cancel(unsigned id) {
  pthread_mutex_lock(&mu);
  for (Req *r = queue; r; r = r->next) if (r->id == id) set_cancelled(r);
  for (Req *r = slowQ; r; r = r->next) if (r->id == id) set_cancelled(r);
  for (Req *r = fqueue; r; r = r->next) if (r->id == id) set_cancelled(r);
  for (int i = 0; i < WORKERS; i++) if (inflight[i] && inflight[i]->id == id) set_cancelled(inflight[i]);
  for (Req *r = finished; r; r = r->next) if (r->id == id) set_cancelled(r);
  pthread_mutex_unlock(&mu);
  pthread_mutex_lock(&memMu); pthread_cond_broadcast(&memCv); pthread_mutex_unlock(&memMu); /* (a worker waiting for room sees it at once) */
}
int net_pending(void) { return npending; }
/* parsed answers are freed on a thread of their own: deleting a catalog's tree (thousands of small blocks, made by a
   worker) took 10-25 ms of a frame on the TV */
typedef struct JF { cJSON *j; struct JF *next; } JF;
static JF *jfHead; static pthread_mutex_t jfMu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t jfCv = PTHREAD_COND_INITIALIZER; static int jfThread;
static void *jf_run(void *a) { plat_thread_background(15); (void)a; for (;;) { pthread_mutex_lock(&jfMu); while (!jfHead) pthread_cond_wait(&jfCv, &jfMu); JF *f = jfHead; jfHead = NULL; pthread_mutex_unlock(&jfMu);
    while (f) { JF *n = f->next; cJSON_Delete(f->j); free(f); f = n; } } return NULL; }
void net_json_free(cJSON *j) {
  if (!j) return;
  pthread_mutex_lock(&jfMu);
  if (!jfThread) { pthread_t t; if (pthread_create(&t, NULL, jf_run, NULL) == 0) { pthread_detach(t); jfThread = 1; } }
  if (!jfThread) { pthread_mutex_unlock(&jfMu); cJSON_Delete(j); return; }
  JF *f = malloc(sizeof *f); if (!f) { pthread_mutex_unlock(&jfMu); cJSON_Delete(j); return; } /* (no room for the note: freed here) */
  f->j = j; f->next = jfHead; jfHead = f; pthread_cond_signal(&jfCv); pthread_mutex_unlock(&jfMu);
}
static const unsigned char *curBody; static cJSON *curJson;
cJSON *net_json(const unsigned char *body) {
  if (body && body == curBody && curJson) { cJSON *j = curJson; curJson = NULL; return j; } /* the tree the worker made: the caller owns it */
  return body ? cJSON_Parse((const char *)body) : NULL;
}
void net_pump(void) {
  Failed fl[64]; int nf = 0;
  pthread_mutex_lock(&mu); Req *list = finished; finished = NULL; if (nfail) { nf = nfail; memcpy(fl, failQ, (size_t)nf * sizeof *fl); nfail = 0; } pthread_mutex_unlock(&mu);
  for (int i = 0; i < nf; i++) { if (fl[i].cb) fl[i].cb(0, (const unsigned char *)"", 0, fl[i].ud); pthread_mutex_lock(&mu); npending--; pthread_mutex_unlock(&mu); }
  /* finished is LIFO; deliver in completion order */
  Req *rev = NULL; while (list) { Req *n = list->next; list->next = rev; rev = list; list = n; }
  while (rev) {
    Req *r = rev; rev = r->next;
    curBody = r->body; curJson = r->json; r->json = NULL;
    { static int slowLog = -1; if (slowLog < 0) slowLog = getenv("SB_SLOWLOG") != NULL; Uint32 tc0 = slowLog ? SDL_GetTicks() : 0;
      if (r->cb) { if (is_cancelled(r)) r->cb(-1, NULL, 0, r->ud); else r->cb(r->status, r->body ? r->body : (const unsigned char *)"", r->len, r->ud); }
      if (slowLog && SDL_GetTicks() - tc0 > 6) fprintf(stderr, "[netcb] %u ms for %.120s (%zu bytes)\n", SDL_GetTicks() - tc0, r->url, r->len); } /* development: which answers cost a frame */
    if (curJson) net_json_free(curJson); curJson = NULL; curBody = NULL; /* the callback did not want the tree */
    pthread_mutex_lock(&mu); npending--; pthread_mutex_unlock(&mu);
    size_t held = r->held + r->jsonHeld; /* (the tree was handed to the callback or to the freeing thread: either way it is not ours to count now) */
    req_free(r); mem_give(held);
  }
}
unsigned char *net_fetch_sync(const char *url, size_t *len, int *status, int timeoutMs) { return net_fetch_sync_hdr(url, NULL, len, status, timeoutMs); }
static long fetchN, fetchNew; /* fetches, and the new connections they made (the rest reused one) */
void net_fetch_stats(long *n, long *fresh) { *n = __atomic_load_n(&fetchN, __ATOMIC_RELAXED); *fresh = __atomic_load_n(&fetchNew, __ATOMIC_RELAXED); }
unsigned char *net_fetch_sync_hdr(const char *url, const char *hdr, size_t *len, int *status, int timeoutMs) {
  Req r; memset(&r, 0, sizeof r); r.url = (char *)url;
  if (!f_easy_init) { *status = 0; *len = 0; return NULL; }
  CURL *c = f_easy_init(); if (!c) { *status = 0; *len = 0; return NULL; }
  struct curl_slist *hs = hdr ? f_slist_append(NULL, hdr) : NULL, *pin = ts_pin(c, url); if (hs) f_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &r);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); f_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0, nc = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_getinfo(c, CURLINFO_NUM_CONNECTS, &nc);
  __atomic_add_fetch(&fetchN, 1, __ATOMIC_RELAXED); if (nc > 0) __atomic_add_fetch(&fetchNew, 1, __ATOMIC_RELAXED);
  f_easy_cleanup(c); if (hs) f_slist_free_all(hs); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  mem_give(r.held); /* (counted while it came; the caller's from here) */
  *status = rc == CURLE_OK && !r.refused ? (int)code : 0; *len = r.len;
  if (*status != 200 && *status != 206) { free(r.body); *len = 0; return NULL; }
  return r.body;
}

/* ---- a download read as it arrives (worker threads only: it blocks) ---------------------------- */
typedef struct { NetSink sink; void *ud; int stopped; } StreamCtx;
static size_t stream_data(char *p, size_t s, size_t n, void *ud) { StreamCtx *x = ud; if (!x->sink((const unsigned char *)p, s * n, x->ud)) { x->stopped = 1; return 0; } return s * n; }
static int stream_poll(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)a; (void)b; (void)c; (void)d; StreamCtx *x = ud; if (!x->sink(NULL, 0, x->ud)) { x->stopped = 1; return 1; } return 0; } /* a stalled transfer can still be stopped */
int net_stream_sync(const char *url, int timeoutMs, NetSink sink, void *ud) {
  if (!f_easy_init && !resolve()) return 0;
  CURL *c = f_easy_init(); if (!c) return 0;
  StreamCtx x = { sink, ud, 0 };
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, stream_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &x); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, stream_poll); f_easy_setopt(c, CURLOPT_XFERINFODATA, &x); f_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, ua()); /* no Accept-Encoding: the bytes counted are the bytes that crossed the line */
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_cleanup(c); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  if (x.stopped) return -1;
  return rc == CURLE_OK ? (int)code : 0;
}

/* ---- a GET passed through as it arrives, with the caller's headers (the stream proxy: worker threads only) ---------- */
typedef struct { NetHdrSink hs; NetSink sink; void *ud; int stopped; } PipeCtx;
static size_t pipe_hdr(char *p, size_t s, size_t n, void *ud) { PipeCtx *x = ud; size_t l = s * n; char line[1024]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0; x->hs(line, x->ud); return l; }
static size_t pipe_data(char *p, size_t s, size_t n, void *ud) { PipeCtx *x = ud; if (!x->sink((const unsigned char *)p, s * n, x->ud)) { x->stopped = 1; return 0; } return s * n; }
static int pipe_poll(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)a; (void)b; (void)c; (void)d; PipeCtx *x = ud; if (!x->sink(NULL, 0, x->ud)) { x->stopped = 1; return 1; } return 0; }
int net_pipe_sync(const char *url, const char *headers, int head, NetHdrSink hs, NetSink sink, void *ud, int stallSec) {
  if (!f_easy_init && !resolve()) return 0;
  CURL *c = f_easy_init(); if (!c) return 0;
  PipeCtx x = { hs, sink, ud, 0 }; struct curl_slist *sl = NULL;
  char *hd = headers ? strdup(headers) : NULL; if (headers && !hd) { f_easy_cleanup(c); return 0; }
  for (char *h = hd, *nl; h && *h; h = nl ? nl + 1 : NULL) { nl = strchr(h, '\n'); if (nl) *nl = 0; if (*h) sl = f_slist_append(sl, h); }
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_HEADERFUNCTION, pipe_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &x); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_WRITEFUNCTION, pipe_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &x);
  f_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, pipe_poll); f_easy_setopt(c, CURLOPT_XFERINFODATA, &x); f_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_MAXREDIRS, 8L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L); f_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L); f_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)(stallSec > 0 ? stallSec : 60));
  f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) Origin"); /* a User-Agent among the headers replaces it */
  f_easy_setopt(c, CURLOPT_BUFFERSIZE, 512L * 1024); /* the video arrives in big reads (curl's own 16 KB: many more wake-ups for the same bytes) */
  f_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L); f_easy_setopt(c, CURLOPT_TCP_KEEPIDLE, 30L); f_easy_setopt(c, CURLOPT_TCP_KEEPINTVL, 15L);
  if (head) f_easy_setopt(c, CURLOPT_NOBODY, 1L);
  if (sl) f_easy_setopt(c, CURLOPT_HTTPHEADER, sl);
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); long code = 0; f_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code); f_easy_cleanup(c); if (sl) f_slist_free_all(sl); free(hd); if (pin) f_slist_free_all(pin); log_fail(rc, url);
  if (x.stopped) return -1;
  return rc == CURLE_OK ? (int)code : 0;
}

/* ---- ranged reads ---------------------------------------------------------------------------- */
struct NetRange { CURL *c; char *orig, *eff; int noDirect; }; /* eff: where orig led (its redirects followed once), asked directly after */
typedef struct { unsigned char *buf; long len, got, extra; int status, isRange; long long reqStart, crStart, total; int norange; } RangeCtx;
static size_t range_hdr(char *p, size_t s, size_t n, void *ud) {
  RangeCtx *r = ud; size_t l = s * n; char line[512]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0;
  int code; if (sscanf(line, "HTTP/%*s %d", &code) == 1) { r->status = code; r->crStart = -1; } /* each redirect hop starts again */
  else if (!strncasecmp(line, "content-range:", 14)) { long long a, b, t; char star[4];
    if (sscanf(line + 14, " bytes %lld-%lld/%lld", &a, &b, &t) == 3) { r->crStart = a; r->total = t; }
    else if (sscanf(line + 14, " bytes %lld-%lld/%1s", &a, &b, star) == 3) r->crStart = a; }
  else if (!strncasecmp(line, "content-length:", 15) && r->status == 200) { long long cl = atoll(line + 15); if (cl > 0) r->total = cl; }
  return l;
}
static size_t range_data(char *p, size_t s, size_t n, void *ud) {
  RangeCtx *r = ud; size_t add = s * n;
  if (r->status >= 300 && r->status < 400) return add;                    /* a redirect's body */
  if (r->status == 200 && r->isRange) { r->norange = 1; return 0; }       /* the server ignored the Range */
  if (r->status == 206 && r->crStart >= 0 && r->crStart != r->reqStart) { r->norange = 1; return 0; } /* another part than asked */
  long need = r->len - r->got, take = (long)add < need ? (long)add : need; if (take < 0) take = 0;
  if (take) { memcpy(r->buf + r->got, p, take); r->got += take; }
  if (r->status == 206) { r->extra += (long)add - take; return r->extra > 65536 ? 0 : add; } /* the part asked for: read to its end, so the connection is kept for the next one (stopping it short closed it: a new TLS handshake for every read, seconds each on a TV); a server sending far past it is stopped */
  return r->got >= r->len ? 0 : add;                                      /* a 200 for the whole file: stopped once it has what was asked */
}
NetRange *net_range_new(void) { if (!f_easy_init && !resolve()) return NULL; NetRange *r = calloc(1, sizeof *r); r->c = f_easy_init(); if (!r->c) { free(r); return NULL; } return r; }
void net_range_free(NetRange *r) { if (!r) return; if (r->c) f_easy_cleanup(r->c); free(r->orig); free(r->eff); free(r); }
static int range_once(NetRange *h, const char *url, long long start, long len, unsigned char **out, long *got, long long *total, int *httpStatus, int timeoutMs) {
  *out = NULL; *got = 0; if (total) *total = -1; if (httpStatus) *httpStatus = 0;
  if (!h || len <= 0) return NR_NET;
  RangeCtx r; memset(&r, 0, sizeof r); r.buf = malloc(len); if (!r.buf) return NR_NET; r.len = len; r.total = -1; r.crStart = -1; r.reqStart = start;
  r.isRange = start > 0; /* asked from past the start: a 200 means no Range support */
  char rg[64]; snprintf(rg, sizeof rg, "%lld-%lld", start, start + len - 1);
  CURL *c = h->c;
  f_easy_setopt(c, CURLOPT_URL, url); f_easy_setopt(c, CURLOPT_RANGE, rg); struct curl_slist *pin = ts_pin(c, url);
  f_easy_setopt(c, CURLOPT_HEADERFUNCTION, range_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &r);
  f_easy_setopt(c, CURLOPT_WRITEFUNCTION, range_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &r);
  f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, WEB_ONLY); f_easy_setopt(c, CURLOPT_MAXREDIRS, 5L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
  f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) Origin"); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
  if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
  CURLcode rc = f_easy_perform(c); if (pin) { f_easy_setopt(c, CURLOPT_RESOLVE, (struct curl_slist *)NULL); f_slist_free_all(pin); } /* (a kept-alive handle: no list left behind it) */ log_fail(rc, url);
  if (httpStatus) *httpStatus = r.status;
  if (total) *total = r.total;
  if (r.norange) { free(r.buf); return NR_NORANGE; }
  if (r.got > 0 && r.status == 0 && !strncmp(url, "file://", 7)) r.status = 206; /* (a file on the phone: no status, its range read as asked) */
  if (r.got > 0 && (r.status == 200 || r.status == 206)) { *out = r.buf; *got = r.got; return NR_OK; } /* full, or a server that cut the response: what came */
  free(r.buf);
  if (rc == CURLE_OPERATION_TIMEDOUT) return NR_TIMEOUT;
  if (r.status == 429 || r.status >= 500) return NR_BUSY;
  if (r.status >= 400) return NR_HTTP;
  return NR_NET;
}
/* a stream's address that redirects (an addon's or a debrid's link) is followed once; its reads then go straight to
   where it led (each one through the redirect cost seconds: a file's subtitles read hundreds of small parts), and back
   through the original when that stops answering (a link's token expired) */
/* where a link led, shared by every handle (a few links): a new connection to the same stream reads straight there
   instead of paying the redirect again (a debrid link: seconds each) */
static struct { char *orig, *eff; } LED[8]; static int ledNext; static pthread_mutex_t ledMu = PTHREAD_MUTEX_INITIALIZER;
static void led_put(const char *o, const char *e) { pthread_mutex_lock(&ledMu); int k = -1; for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, o)) k = i; if (k < 0) { k = ledNext; ledNext = (ledNext + 1) % 8; }
  char *a = strdup(o), *b = strdup(e); if (a && b) { free(LED[k].orig); free(LED[k].eff); LED[k].orig = a; LED[k].eff = b; } else { free(a); free(b); } pthread_mutex_unlock(&ledMu); }
static void led_drop(const char *o) { pthread_mutex_lock(&ledMu); for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, o)) { free(LED[i].orig); free(LED[i].eff); LED[i].orig = LED[i].eff = NULL; } pthread_mutex_unlock(&ledMu); }
int net_led_known(const char *url) { pthread_mutex_lock(&ledMu); int k = 0; for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, url)) k = 1; pthread_mutex_unlock(&ledMu); return k; }
static void led_get(NetRange *h, const char *url) { if (h->eff && h->orig && !strcmp(h->orig, url)) return; pthread_mutex_lock(&ledMu);
  for (int i = 0; i < 8; i++) if (LED[i].orig && !strcmp(LED[i].orig, url)) { char *a = strdup(LED[i].orig), *b = strdup(LED[i].eff); if (a && b) { free(h->orig); free(h->eff); h->orig = a; h->eff = b; } else { free(a); free(b); } break; }
  pthread_mutex_unlock(&ledMu); }
/* many byte ranges in one request (multipart/byteranges): parts[i] (malloc'd, or NULL when the answer lacked it) and
   plen[i]. NR_NORANGE: the server doesn't answer several ranges at once (a 200, one part, an error for the form) */
typedef struct { unsigned char *buf; long len, cap, max; int status; char ctype[200]; } MultiCtx;
static size_t multi_hdr(char *p, size_t s, size_t n, void *ud) { MultiCtx *m = ud; size_t l = s * n; char line[512]; size_t k = l < sizeof line - 1 ? l : sizeof line - 1; memcpy(line, p, k); line[k] = 0;
  int code; if (sscanf(line, "HTTP/%*s %d", &code) == 1) { m->status = code; m->ctype[0] = 0; m->len = 0; }
  else if (!strncasecmp(line, "content-type:", 13)) { char *v = line + 13; while (*v == ' ') v++; size_t vl = strcspn(v, "\r\n"); if (vl >= sizeof m->ctype) vl = sizeof m->ctype - 1; memcpy(m->ctype, v, vl); m->ctype[vl] = 0; }
  return l; }
static size_t multi_data(char *p, size_t s, size_t n, void *ud) { MultiCtx *m = ud; size_t add = s * n; if (m->status != 206) return add; if (m->len + (long)add > m->max) return 0;
  if (m->len + (long)add > m->cap) { long nc = (m->len + (long)add) * 2; if (nc > m->max) nc = m->max; unsigned char *q = realloc(m->buf, nc); if (!q) return 0; m->buf = q; m->cap = nc; } memcpy(m->buf + m->len, p, add); m->len += add; return add; }
static unsigned char *mem_find(unsigned char *h, long hn, const char *nd, long nn) { for (long i = 0; i + nn <= hn; i++) if (h[i] == (unsigned char)nd[0] && !memcmp(h + i, nd, nn)) return h + i; return NULL; }
int net_range_multi(NetRange *h, const char *url, const long long *st, const long *ln, int n, unsigned char **parts, long *plen, int *httpStatus, int timeoutMs) {
  for (int i = 0; i < n; i++) { parts[i] = NULL; plen[i] = 0; } if (!h || n <= 0) return NR_NET; if (!h->noDirect) led_get(h, url);
  int direct = h->eff && h->orig && !strcmp(h->orig, url); char *rg = malloc((size_t)n * 48 + 8); if (!rg) return NR_NET; rg[0] = 0; long want = 0;
  for (int i = 0, o = 0; i < n; i++) { o += snprintf(rg + o, 48, "%s%lld-%lld", i ? "," : "", st[i], st[i] + ln[i] - 1); want += ln[i]; }
  for (int pass = 0; pass < 2; pass++) {
    MultiCtx m; memset(&m, 0, sizeof m); m.max = want + n * 1024L + 65536; CURL *c = h->c; /* (no reset: the handle keeps its shared TLS and DNS caches) */
    f_easy_setopt(c, CURLOPT_URL, direct ? h->eff : url); f_easy_setopt(c, CURLOPT_RANGE, rg); struct curl_slist *pin = ts_pin(c, url);
    f_easy_setopt(c, CURLOPT_HEADERFUNCTION, multi_hdr); f_easy_setopt(c, CURLOPT_HEADERDATA, &m); f_easy_setopt(c, CURLOPT_WRITEFUNCTION, multi_data); f_easy_setopt(c, CURLOPT_WRITEDATA, &m);
    f_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); f_easy_setopt(c, CURLOPT_MAXREDIRS, 5L); f_easy_setopt(c, CURLOPT_NOSIGNAL, 1L); f_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L); f_easy_setopt(c, CURLOPT_TIMEOUT_MS, (long)timeoutMs);
    f_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Web0S; Linux) StremioOrigin"); f_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L); if (caPath[0]) f_easy_setopt(c, CURLOPT_CAINFO, caPath);
    CURLcode rc = f_easy_perform(c); if (pin) { f_easy_setopt(c, CURLOPT_RESOLVE, (struct curl_slist *)NULL); f_slist_free_all(pin); }
    if (httpStatus) *httpStatus = m.status;
    if (direct && !pass && (rc != CURLE_OK || (m.status >= 400 && m.status != 416 && m.status != 429))) { free(m.buf); led_drop(url); free(h->eff); h->eff = NULL; direct = 0; continue; } /* (where the link led stopped answering: through the link again) */
    if (m.status == 429 || m.status == 503) { free(m.buf); free(rg); return NR_BUSY; }
    if (rc == CURLE_OPERATION_TIMEDOUT) { free(m.buf); free(rg); return NR_TIMEOUT; }
    if (rc != CURLE_OK && !(rc == CURLE_WRITE_ERROR && m.status != 206)) { free(m.buf); free(rg); return NR_NET; }
    const char *bd = strcasestr(m.ctype, "boundary="); if (m.status != 206 || !strcasestr(m.ctype, "multipart/byteranges") || !bd) { free(m.buf); free(rg); return m.status == 206 || m.status == 200 || m.status == 400 || m.status == 416 || m.status == 501 ? NR_NORANGE : m.status >= 400 ? NR_HTTP : NR_NET; }
    char sep[96]; bd += 9; if (*bd == '"') bd++; size_t bl = strcspn(bd, "\";\r\n "); if (bl > 80) bl = 80; snprintf(sep, sizeof sep, "--%.*s", (int)bl, bd);
    unsigned char *q = m.buf, *end = m.buf + m.len; long sl = (long)strlen(sep);
    while ((q = mem_find(q, end - q, sep, sl))) { q += sl; if (q + 2 <= end && q[0] == '-' && q[1] == '-') break;
      unsigned char *he = mem_find(q, end - q, "\r\n\r\n", 4); if (!he) break; long long a = -1, b = -1;
      { char hd[1024]; long hl = he - q < (long)sizeof hd - 1 ? he - q : (long)sizeof hd - 1; memcpy(hd, q, hl); hd[hl] = 0; char *cr = strcasestr(hd, "content-range:"); if (cr) sscanf(cr + 14, " bytes %lld-%lld", &a, &b); }
      unsigned char *d = he + 4; if (a < 0 || b < a || d + (b - a + 1) > end) { q = d; continue; }
      for (int i = 0; i < n; i++) if (!parts[i] && st[i] >= a && st[i] <= b) { long take = (long)(b - st[i] + 1); if (take > ln[i]) take = ln[i]; parts[i] = malloc(take); if (parts[i]) { memcpy(parts[i], d + (st[i] - a), take); plen[i] = take; } } /* (a server may join neighbouring ranges into one part) */
      q = d + (b - a + 1); }
    free(m.buf); free(rg); int any = 0; for (int i = 0; i < n; i++) if (parts[i]) any = 1; return any ? NR_OK : NR_NORANGE; }
  free(rg); return NR_NET; }
int net_range_get(NetRange *h, const char *url, long long start, long len, unsigned char **out, long *got, long long *total, int *httpStatus, int timeoutMs) {
  if (!h) return range_once(h, url, start, len, out, got, total, httpStatus, timeoutMs);
  if (!h->noDirect) led_get(h, url); /* (learnt by another connection: straight there) */
  if (h->noDirect && h->orig && strcmp(h->orig, url)) h->noDirect = 0; /* (another stream on this handle: learnt afresh) */
  int direct = h->eff && h->orig && !strcmp(h->orig, url); struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
  int rc = range_once(h, direct ? h->eff : url, start, len, out, got, total, httpStatus, timeoutMs);
  if (direct && (rc == NR_HTTP || rc == NR_NET)) { fprintf(stderr, "[range] where the link led stopped answering (%d, HTTP %d): through the link again\n", rc, httpStatus ? *httpStatus : 0); char *was = h->eff; h->eff = NULL; direct = 0; led_drop(url); rc = range_once(h, url, start, len, out, got, total, httpStatus, timeoutMs);
    if (rc == NR_OK || rc == NR_NORANGE) { char *e = NULL; f_easy_getinfo(h->c, CURLINFO_EFFECTIVE_URL, &e); if (e && was && !strcmp(e, was)) { h->noDirect = 1; fprintf(stderr, "[range] where the link leads answers only through it: read through the link from now on\n"); } } free(was); } /* (the same place again: not an expired token, it needs the link (a cookie, a referrer); never tried directly again for this stream) */
  clock_gettime(CLOCK_MONOTONIC, &t1); double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6; static double lastSaid;
  if ((ms > 1500 || rc != NR_OK) && t1.tv_sec - lastSaid >= 1) { lastSaid = t1.tv_sec; long nc = 0; double tc = 0, ts = 0, tf = 0; f_easy_getinfo(h->c, CURLINFO_NUM_CONNECTS, &nc); f_easy_getinfo(h->c, CURLINFO_CONNECT_TIME, &tc); f_easy_getinfo(h->c, CURLINFO_APPCONNECT_TIME, &ts); f_easy_getinfo(h->c, CURLINFO_STARTTRANSFER_TIME, &tf);
    fprintf(stderr, "[range] %.0f ms for %ld B at %lld, %s: %d (HTTP %d) · new connections %ld, connect %.0f ms, tls %.0f ms, first byte %.0f ms\n", ms, len, start, direct ? "direct" : "through the link", rc, httpStatus ? *httpStatus : 0, nc, tc * 1e3, ts * 1e3, tf * 1e3); }
  if (!direct && !h->noDirect && (rc == NR_OK || rc == NR_NORANGE)) { char *e = NULL; f_easy_getinfo(h->c, CURLINFO_EFFECTIVE_URL, &e);
    if (e && *e && strcmp(e, url)) { char *o = strdup(url), *f = strdup(e); if (o && f) { free(h->orig); free(h->eff); h->orig = o; h->eff = f; led_put(o, f); fprintf(stderr, "[range] the link redirects: read straight from where it led (%.0f ms through it)\n", ms); } else { free(o); free(f); } } }
  return rc;
}
