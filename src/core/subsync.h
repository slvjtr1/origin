/* Auto-sync: lines a downloaded subtitle up with a reference that is right for THIS video, the file's own subtitle
   tracks, else another subtitle made for this file (any language: only WHEN lines are spoken matters). The same words
   on both sides vote for one shift; else every offset within +-30 s (50 ms steps) is tried at the usual frame-rate
   ratios and the best overlap wins only when it clearly beats the rest. The answer is new time = old * f + d. The
   matching runs on a thread of its own over bounded copies of both lists; it can be cancelled at any time (the job
   frees itself). */
#ifndef SB_SUBSYNC_H
#define SB_SUBSYNC_H
#include "core/subs.h"
typedef struct SubSync SubSync;
typedef struct { double s, e; const char *t; } SyncLine;
enum { SYNC_RUNNING = 0, SYNC_OK = 1, SYNC_UNSURE = 2, SYNC_NOSUB = 3, SYNC_NOREF = 4 }; /* NOREF: no other subtitle made for this file */
typedef struct { int st; double d, f; float score; int byWords; } SyncResult;
#define SYNC_BEHIND_S 180.0   /* the reference is read around the playhead: this much before it... */
#define SYNC_AHEAD_S 260.0    /* ...and this much after */
/* X: the subtitle's own times (original, unshifted); R: the reference's lines near center (copied, any order) */
SubSync *subsync_start(const Cue *X, int nx, const SyncLine *R, int nr, double center);
/* When the file has no text track: another subtitle made for this file as the reference, looked for, downloaded and
   matched on the job's thread (by the file's hash on OpenSubtitles first, then refs: hm / own first, then by how well
   rel matches the file's name; curUrl, the subtitle being synced, is never its own reference). q: the stream (its
   hash is worked out when it can be read in ranges). Logged as [sync]. */
typedef struct { char url[1024], lang[24], rel[96], from[64]; int hm, own; double fps; } SyncRef;   /* own: the stream carried it; fps: what it was timed for (0 unknown) */
SubSync *subsync_start_ref(const Cue *X, int nx, double center, const SubQuery *q, const char *curUrl, const char *curLang, const SyncRef *refs, int nrefs);
int  subsync_stage(SubSync *j);                   /* 1 while the reference is looked for and read, 0 while matching */
int  subsync_poll(SubSync *j, SyncResult *out);   /* 1 once answered (out filled), 0 while it runs */
void subsync_drop(SubSync *j);                    /* cancels it if still running; the handle is gone after this */
#endif
