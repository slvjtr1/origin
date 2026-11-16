/* The app's memory, handed back to the TV when it is no longer needed. The C library keeps freed memory for
   itself unless told otherwise: mem_tune sets it up at start so large blocks go straight back when freed, and
   mem_trim_soon returns the rest (the freed space inside the heaps) on a thread of its own, a moment after
   something big was let go: a film starting (the pages under it), a film closing (its lists, subtitles and the
   torrent's buffers), the app sent to the back. */
#ifndef SB_MEMCTL_H
#define SB_MEMCTL_H
void mem_tune(void);                              /* first thing at start, before anything is allocated */
void mem_trim_soon(double afterSec, const char *why); /* main thread; why: a constant string, for the log */
void mem_pump(void);                              /* main thread, once a frame */
#endif
