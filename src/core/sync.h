/* Keeping this TV in step with the account: the library and the addon list a moment after start, the
   library again when the app comes back to the front and every few minutes while the remote rests, and the
   board on screen rebuilt in place whenever something changed. */
#ifndef SB_SYNC_H
#define SB_SYNC_H
void sync_init(void);   /* at start, before the first screen */
void sync_tick(void);   /* once a frame */
#endif
