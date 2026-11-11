#ifndef SB_CWMENU_H
#define SB_CWMENU_H
#include "core/stremio.h"
void cwmenu_show(const Item *it);   /* hold OK on any card: its menu */
void cwmenu_fx_lib(int added, const Item *it);      /* the Library chip for an action taken elsewhere (the title page's button) */
#endif
