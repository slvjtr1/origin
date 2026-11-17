/* HTTP for the app: requests run on worker threads (libcurl), answers are handed back on the
   main thread from net_pump(), so screens never touch a lock. On the TV libcurl is the system
   one, opened at run time (it keeps the binary free of a hard dependency on one soname). */
#ifndef SB_NET_H
#define SB_NET_H
#include <stddef.h>

/* status: HTTP status (200..), 0 = network failure (or the answer did not fit in memory), -1 = cancelled. body is
   NUL-terminated for convenience (len excludes the NUL) and is freed after the callback returns.
   Every request function returns its id, or 0 when it could not even be queued; a request that was given an id always
   gets its callback once, on the main thread, from net_pump (never inside the call that made it). */
typedef void (*NetCb)(int status, const unsigned char *body, size_t len, void *ud);

int      net_init(const char *caBundlePath);  /* caBundlePath may be NULL */
unsigned net_get(const char *url, NetCb cb, void *ud);
/* a file's size from one 1-byte range request (body = the byte count as text, status 0 when unknown); the body is never read */
unsigned net_file(const char *path, NetCb cb, void *ud);  /* a local file read (and JSON parsed) off the drawing thread: 200 or 404 */
unsigned net_size(const char *url, NetCb cb, void *ud);
unsigned net_get_hdr(const char *url, const char *extraHeader, NetCb cb, void *ud);  /* several: one per line */
unsigned net_post_json(const char *url, const char *json, NetCb cb, void *ud);
/* a POST with its own headers: one "Name: value" per line ("\n" between them); a User-Agent among them
   replaces the app's. contentType NULL or "" sends none of ours. net_post_hdr_t: a timeout other than 20 s */
unsigned net_post_hdr_t(const char *url, const char *body, const char *contentType, const char *headers, int timeoutMs, NetCb cb, void *ud);
/* a POST / PUT of raw bytes (safe from any thread); headers: one "Name: value" per line, or NULL */
unsigned net_send(const char *method, const char *url, const char *headers, const void *body, size_t len, int timeoutMs, NetCb cb, void *ud);
/* a GET allowed to take longer (or shorter) than the usual 20 s, e.g. stream addons that resolve debrid links */
unsigned net_get_timeout(const char *url, int timeoutMs, NetCb cb, void *ud);
void     net_cancel(unsigned id);
void     net_pump(void);           /* delivers finished requests; call once per frame */
/* inside a callback: the JSON body, already parsed on the network worker (else parsed now); the caller owns the tree */
struct cJSON *net_json(const unsigned char *body);
void     net_json_free(struct cJSON *j);   /* a parsed answer freed off the drawing thread */
int      net_pending(void);
/* what the answers hold now (their buffers, by capacity, and their parsed trees, by estimate, from the first byte until
   the callback has returned), the most they held, the budget, and how many answers were refused for want of room */
void     net_mem_stats(size_t *used, size_t *peak, size_t *budget, unsigned *refused);
void     net_mem_set_budget(size_t bytes);   /* tests: 0 puts the TV's own budget back */
/* synchronous fetch for worker threads of other modules (images); returns malloc'd body or NULL */
unsigned char *net_fetch_sync(const char *url, size_t *len, int *status, int timeoutMs);
/* the same with one extra request header ("Authorization: Bearer ..."), NULL for none */
unsigned char *net_fetch_sync_hdr(const char *url, const char *extraHeader, size_t *len, int *status, int timeoutMs);

/* A GET read as it arrives, for worker threads (it blocks): sink gets each piece, and (p NULL, n 0) now and then
   while nothing comes; returning 0 stops the download. Returns the HTTP status of a finished download, 0 when it
   failed, -1 when the sink stopped it. */
typedef int (*NetSink)(const unsigned char *p, size_t n, void *ud);
int      net_stream_sync(const char *url, int timeoutMs, NetSink sink, void *ud);

/* A GET (head: a HEAD) passed on as it arrives, with extra request headers (one "Name: value" per line): hs gets each
   header line of every hop (a status line starts a hop), sink the body as for net_stream_sync; it gives up after
   stallSec seconds without a byte. Worker threads only. The HTTP status, 0 when it failed, -1 when the sink stopped it */
typedef void (*NetHdrSink)(const char *line, void *ud);
int      net_pipe_sync(const char *url, const char *headers, int head, NetHdrSink hs, NetSink sink, void *ud, int stallSec);

/* Ranged reads on one kept-alive connection (worker threads only: it blocks). net_range_get reads
   [start, start+len) into *out (malloc'd, *got bytes; a short read returns what came) and the file's
   total size (-1 unknown). Returns NR_OK or why it failed. */
enum { NR_OK = 0, NR_NORANGE, NR_HTTP, NR_BUSY, NR_TIMEOUT, NR_NET };
typedef struct NetRange NetRange;
NetRange *net_range_new(void);
int       net_range_get(NetRange *r, const char *url, long long start, long len, unsigned char **out, long *got, long long *total, int *httpStatus, int timeoutMs);
void      net_range_free(NetRange *r);
#endif
