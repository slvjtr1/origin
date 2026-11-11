/* The guide: the first-start tour and each version's new pages; see welcome.c */
#ifndef SB_WELCOME_H
#define SB_WELCOME_H
void welcome_tick(void);   /* every frame: opens the guide once, when Home is up (the first start, or a new version) */
int  welcome_covers(void);
int  welcome_is_open(void);          /* the guide (or the version's page) is up, coming or going */ /* the guide's page is all that shows: nothing under it needs drawing */
void welcome_show_guide(void);
int  welcome_ready(void);           /* the guide's pictures are in (or none is due) */
void welcome_open_now(void);          /* right after signing in: the guide (or the version's page) now, without its entrance (the loading fades off it) */
void welcome_show_whatsnew(void);      /* the version's page again, now (Settings > About) */          /* the tour again, now, over the page (Settings > About) */
const char *welcome_support_url(void);  /* the address to support the project (whatsnew.json), "" none */
const char *welcome_github_url(void);   /* the project on GitHub (whatsnew.json), "" none */
#endif
