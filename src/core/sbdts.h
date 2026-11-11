/* The sound converter library's interface (libsbdts, loaded on demand by hproxy.c the first time a file's only sound
   is one the TV can't decode, or its picture is Dolby Vision profile 7). A source is a Matroska or MP4 file read
   through the app's own range reader; out come MPEG-TS segments (or one continuous stream) with the video copied (a
   profile 7 picture as single-layer profile 8.1) and the chosen audio track as Dolby Digital, or as it is.
   Calls block: they run on the proxy's threads, one run at a time per source. */
#ifndef SB_DTS_H
#define SB_DTS_H
#include <stddef.h>
#define SBD_API_VERSION 2
#define SBD_NOINDEX 1
#define SBD_DV81 2       /* a Dolby Vision profile 7 picture goes out as profile 8.1: its enhancement layer dropped, each RPU rewritten */
#define SBD_COPYAUDIO 4  /* the chosen audio track goes out as it is (not decoded) */
typedef struct SbdSrc SbdSrc;
typedef int (*sbd_read)(void *ud, long long pos, unsigned char *buf, int len);   /* bytes read, 0 at the end, < 0 failed */
typedef int (*sbd_write)(void *ud, const unsigned char *p, int n);                /* n, or < 0 to stop the run */
typedef struct {
  double dur;                    /* seconds */
  int nseg, indexed;             /* indexed 0: no keyframe index (Matroska without Cues): one continuous stream only */
  int width, height, inCh, outCh, outKbps;
  int dvIn, dvOut;               /* the picture's Dolby Vision profile in the file and as it goes out (0 none) */
  char vcodec[16], acodec[16], path[96];   /* "hevc", "dts", "DTS core decoded, AC3 5.1 640 kb/s" */
} sbd_info;
typedef struct { long long bytes; long vpkts, apkts; double first, last, cpu, wall, readWait; int fresh; size_t peakQueue; long rpuOk, rpuBad, elDropped; } sbd_stats;
typedef struct {
  int         (*api)(void);
  const char *(*version)(void);
  /* opens the file and reads its keyframe index (Matroska: the Cues). audioOrd: which audio track, counted among the
     file's audio tracks from 0, -1 = the first DTS / TrueHD one (with SBD_COPYAUDIO: the first one). flags: SBD_NOINDEX, the file is known to have no Cues; SBD_DV81, SBD_COPYAUDIO. Segments group keyframes to at least segTarget s.
     *stop (may be NULL) breaks the reads off. NULL with the reason in err */
  SbdSrc     *(*open)(sbd_read rd, void *ud, long long size, int audioOrd, int flags, double segTarget, volatile int *stop, char *err, int errn);
  const sbd_info *(*info)(SbdSrc *s);
  double      (*seg_start)(SbdSrc *s, int seg);
  double      (*seg_dur)(SbdSrc *s, int seg);
  /* seg >= 0: segment seg (continued without a seek when it follows the last one run); seg < 0: one stream from the
     keyframe at or before `from` to the end. Written through w as it is made. 0 done, -1 failed (err), -2 stopped */
  int         (*run)(SbdSrc *s, int seg, double from, sbd_write w, void *ud, volatile int *stop, sbd_stats *st, char *err, int errn);
  void        (*close)(SbdSrc *s);
} sbd_api;
#endif
