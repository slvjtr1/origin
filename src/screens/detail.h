#ifndef SB_DETAIL_H
#define SB_DETAIL_H
#include "core/app.h"
extern Screen detailScreen;
void detail_open_streams(const char *vid);
void detail_refresh_watched(void);   /* after a watched change elsewhere */
void detail_paint_lib(void);
const char *detail_id(void);
/* a title's trailer in the player (the title page's button, the card menu) */
void detail_trailer(const char *type, const char *id, const char *name);
int detail_page_btn_key(int k); /* the reviews risen into the page: Left / Right / OK on the buttons held at the top */
#endif
