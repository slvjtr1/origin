/* The app's memory, handed back to the TV when it is no longer needed. The C library keeps freed memory inside its
   heaps unless told otherwise: mem_trim_soon asks for it back (malloc_trim) a moment after something big was let go
   (a film covering the pages, a film closing, the app sent to the back), on one worker thread of its own.

   The trim is not free: while it walks a heap that heap's lock is held, and a thread allocating from it waits (the
   drawing thread included). So each trim is measured (how long it took, what it gave back, the longest frame drawn
   meanwhile, in the log), trims are spaced, and one that gave back next to nothing makes the next wait longer.

   The only allocator setting changed is the number of heaps (mem_tune): every thread allocating at once otherwise
   gets a heap of its own (up to 8 on the TV's 4 cores), each keeping its own freed space. SB_MALLOC_DEFAULT=1 leaves
   the C library as it is, for comparing the two on a TV. On Android and the previews this whole module does nothing. */
#ifndef SB_MEMCTL_H
#define SB_MEMCTL_H
void mem_tune(void);                              /* first thing at start, before any thread starts */
void mem_trim_soon(double afterSec, const char *why); /* main thread; why is copied */
void mem_pump(void);                              /* main thread, once a frame: starts a trim that is due, logs one that ended */
void mem_frame(double ms);                        /* main thread, once a frame: how long it took (a trim running meanwhile is charged for it) */
int  mem_trimming(void);                          /* a trim is running now (any thread) */
void mem_shutdown(void);                          /* at exit: the worker ends (a trim under way finishes first) */
/* one line in the log of where the memory is, at a moment worth knowing it (a film starting, a tray opening, the app
   sent to the back): the app, the TV, the answers in hand. At most 4 a second (main thread) */
void mem_note(const char *what);
#endif
