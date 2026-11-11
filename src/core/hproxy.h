/* Streams that need their own request headers (an addon's behaviorHints.proxyHeaders): the TV's media pipeline is
   given a local address instead, and a small proxy on 127.0.0.1 (this device only) fetches the real one with those
   headers on every request (ranges included), its response headers replaced where the addon says so. The subtitle
   reader, the size and hash reads go through the same address, so every request for that stream carries them. */
#ifndef SB_HPROXY_H
#define SB_HPROXY_H
#include <stddef.h>
/* the local address for url with reqHdrs / respHdrs ("Name: value" lines); 0 when the proxy cannot start (then url) */
int hproxy_url(const char *url, const char *reqHdrs, const char *respHdrs, char *out, size_t cap);
/* a small text the app made (a manifest), given a local address ending in name; the last four are kept. 0: no proxy */
/* a YouTube picture file and its sound file (each one piece, its header ending at initEnd, its index at ix0..ix1) as
   one local HLS address; 0 when it cannot be made */
int hproxy_yt(const char *vurl, long vInitEnd, long vIx0, long vIx1, const char *aurl, long aInitEnd, long aIx0, long aIx1, const char *codecs, int w, int h, double bw, char *out, size_t cap);
int hproxy_text(const char *body, const char *type, const char *name, char *out, size_t cap);

/* A file whose only sound the TV can't decode, or whose picture is Dolby Vision profile 7, converted on this device
   (libsbdts: video copied, a profile 7 picture as 8.1, the sound as Dolby Digital or as it is) and served as local HLS: /h/<id>/index.m3u8 and /h/<id>/<n>.ts, or, for a file without a keyframe index,
   one stream /h/<id>/all.ts?t=<seconds>. One conversion at a time; calls for the main thread (the work is on threads) */
typedef struct { int indexed, outCh, dvIn, dvOut; double dur; char vcodec[16], path[96]; } HlsConvInfo;
enum { HC_NOINDEX = 1, HC_DV81 = 2, HC_COPYAUDIO = 4 };          /* the file is known to have no Cues; profile 7 as 8.1; the audio track as it is */
int  hls_conv_available(void);                                  /* the converter library is installed */
void hls_conv_start(const char *url, int audioOrd, int flags);  /* audioOrd among the file's audio tracks (-1 the first DTS / TrueHD) */
int  hls_conv_poll(char *url, size_t cap, char *why, size_t whyCap, HlsConvInfo *info); /* 0 opening, 1 ready (url to load), -1 failed (why) */
void hls_conv_stop(void);
#endif
