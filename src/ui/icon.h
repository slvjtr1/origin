/* Vector icons: SVG paths (assets/icons.json) rasterised at the size they are drawn, kept as
   alpha textures. Pixel-sharp at any size. */
#ifndef SB_ICON_H
#define SB_ICON_H
#include <stdint.h>
int  icon_init(const char *assetDir);
/* draws icon `name` fitted in a size x size box at (x, y) */
void icon_draw(const char *name, float x, float y, float size, uint32_t color);
/* the same, turned by `deg` degrees around its centre */
void icon_draw_rot(const char *name, float x, float y, float size, uint32_t color, float deg);
int  icon_exists(const char *name);
#endif
