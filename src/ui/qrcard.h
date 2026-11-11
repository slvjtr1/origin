/* The code card: a card over the page with a QR to open on the phone (the TMDB key page, an
   addon's outside link). Any key or click closes it. When the phone did what the card was for (the code was
   opened, a key arrived) it shows a tick and goes away by itself. It is an overlay (app_overlay): keys go
   to it first. */
#ifndef SB_QRCARD_H
#define SB_QRCARD_H

typedef struct {
  const char *title, *sub;      /* already translated */
  const char *url;              /* what the code opens; NULL or "" = no network address (the card says so) */
  const char *shown;            /* the address written under the code; NULL = the url's own */
  int (*check)(void *ud);       /* asked every 1.5 s: 1 = done (the tick); NULL = never */
  const char *doneTitle, *doneSub; /* the tick's words (doneSub may be NULL) */
  float doneHold;               /* seconds the tick stays before the card goes */
  void (*onClose)(void *ud);    /* once it is closed, however that happened */
  void *ud;
} QrCardOpts;

void qrcard_show(const QrCardOpts *o);
void qrcard_close(void);

/* the ready-made cards */
/* Settings > Language > Titles and descriptions: the TMDB key page; onClose(hasKey, ud) once it is closed */
void qrcard_show_tmdb(void (*onClose)(int hasKey, void *ud), void *ud);
#endif
