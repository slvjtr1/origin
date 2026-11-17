/* Whose the torrent buffer's folders are (core/p2p.c). One folder is live: the engine's window is in it now. A folder
   the window moved out of is retired: it is removed once the engine has stopped (its threads ended: nothing can write
   there any more) or by the next start's sweep, unless the window has come back to it by then (a drive pulled out and
   plugged in again): set live, it leaves the retired list. A folder is renamed under the lock before it is emptied, so
   the live one is never what is removed, and a folder the engine might still write to is never touched. */
#ifndef SB_P2PDIRS_H
#define SB_P2PDIRS_H
void p2pdirs_set_live(const char *dir);     /* the engine's window is in dir now ("" none) */
void p2pdirs_retire(const char *dir);       /* the window left dir: removed at the engine's stop (unless live again by then) */
int  p2pdirs_stopped(const char *dir);      /* the engine with its window in dir has stopped: dir and the retired ones go; how many */
void p2pdirs_sweep_old(const char *root);   /* left-overs renamed by an earlier run (<root>.old-*), next to root */
void p2pdirs_rm_tree(const char *dir);      /* everything inside dir (not dir itself) */
int  p2pdirs_live(char *out, unsigned cap); /* tests: the live folder (1 when there is one) */
#endif
