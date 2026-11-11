/* tests/dts/run.sh: the converter library on real files, on the Mac. Writes the playlist and every segment of a file
   (in order, then a few out of order, as after a seek) to a folder, with the CPU time of each segment */
#include "core/sbdts.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
static long rss_kb(void) { struct rusage u; getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
  return u.ru_maxrss / 1024;
#else
  return u.ru_maxrss;
#endif
}
static FILE *in; static long long readBytes;
static int rd(void *ud, long long pos, unsigned char *b, int n) { (void)ud; if (fseeko(in, pos, SEEK_SET)) return -1; size_t k = fread(b, 1, n, in); readBytes += (long long)k; return (int)k; }
static int wr(void *ud, const unsigned char *p, int n) { return fwrite(p, 1, n, ud) == (size_t)n ? n : -1; }
int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "remux_test <lib> <file> <outdir> [order...]\n"); return 2; }
  void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
  const sbd_api *A = ((const sbd_api *(*)(void))dlsym(h, "sbd_get"))();
  long rss0 = rss_kb(); in = fopen(argv[2], "rb"); fseeko(in, 0, SEEK_END); long long size = ftello(in);
  int fl = (getenv("NOINDEX") ? SBD_NOINDEX : 0) | (getenv("DV81") ? SBD_DV81 : 0) | (getenv("COPYAUDIO") ? SBD_COPYAUDIO : 0); /* (the modes, from the environment) */
  char err[200]; SbdSrc *s = A->open(rd, NULL, size, getenv("AUDIO") ? atoi(getenv("AUDIO")) : -1, fl, 2.0, NULL, err, sizeof err); if (!s) { fprintf(stderr, "open: %s\n", err); return 1; }
  const sbd_info *i = A->info(s); printf("opening read %.1f MB\n", readBytes / 1048576.0);
  printf("%s | %s %dx%d, %s %dch -> %dch, %.1f s, %d segments, indexed %d, Dolby Vision %d -> %d | %s\n", A->version(), i->vcodec, i->width, i->height, i->acodec, i->inCh, i->outCh, i->dur, i->nseg, i->indexed, i->dvIn, i->dvOut, i->path);
  char p[1024]; snprintf(p, sizeof p, "%s/index.m3u8", argv[3]); FILE *m = fopen(p, "w"); fprintf(m, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXT-X-PLAYLIST-TYPE:VOD\n");
  for (int k = 0; k < i->nseg; k++) fprintf(m, "#EXTINF:%.3f,\n%d.ts\n", A->seg_dur(s, k), k); fprintf(m, "#EXT-X-ENDLIST\n"); fclose(m);
  int order[4096], n = 0;
  if (argc > 4) for (int a = 4; a < argc && n < 4096; a++) order[n++] = atoi(argv[a]); else for (int k = 0; k < i->nseg && n < 4096; k++) order[n++] = k;
  if (!i->indexed) { order[0] = -1; n = 1; }
  double cpu = 0, mx = 0; size_t peak = 0; long rok = 0, rbad = 0, el = 0;
  for (int k = 0; k < n; k++) { int g = order[k]; snprintf(p, sizeof p, g < 0 ? "%s/all.ts" : "%s/%d.ts", argv[3], g); FILE *o = fopen(p, "wb"); sbd_stats st;
    int r = A->run(s, g, getenv("FROM") ? atof(getenv("FROM")) : 0, wr, o, NULL, &st, err, sizeof err); fclose(o);
    if (r) { fprintf(stderr, "seg %d: %d %s\n", g, r, err); return 1; }
    rok += st.rpuOk; rbad += st.rpuBad; el += st.elDropped; cpu += st.cpu; if (st.cpu > mx) mx = st.cpu; if (st.peakQueue > peak) peak = st.peakQueue;
    if (k < 3 || st.fresh) printf("seg %d%s: %.2f-%.2f s, %lld bytes, %ld v %ld a, cpu %.0f ms\n", g, st.fresh ? " (fresh)" : "", st.first, st.last, st.bytes, st.vpkts, st.apkts, st.cpu * 1000); }
  printf("cpu %.2f s for %.0f s of film (%.1f%%), worst segment %.0f ms, peak queue %.1f MB\n", cpu, i->dur, 100 * cpu / i->dur, mx * 1000, peak / 1048576.0);
  if (i->dvOut) printf("Dolby Vision: %ld RPUs converted, %ld left out, %ld enhancement-layer NAL units dropped\n", rok, rbad, el);
  printf("peak memory: %.1f MB over the %.1f MB before opening\n", (rss_kb() - rss0) / 1024.0, rss0 / 1024.0);
  A->close(s); return 0;
}
