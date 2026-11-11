/* Colours of a picture: pixels grouped into hue/lightness/saturation boxes, the best swatch of
   each kind picked (vibrant, dark, light, muted, dark muted). The dynamic accent comes from it. */
#ifndef SB_PALETTE_H
#define SB_PALETTE_H
typedef struct { float h, s, l; int n; int ok; } Swatch;
typedef struct { Swatch vibrant, dark, light, muted, darkMuted; int n; } Palette;
/* rgba pixels; alpha = ignore transparent pixels (logos); stride = sample every n-th pixel */
void palette_of(const unsigned char *px, int w, int h, int alpha, int stride, Palette *out);
void rgb2hsl(float r, float g, float b, float *h, float *s, float *l);
unsigned hsl2rgb(float h, float s, float l);  /* 0xffRRGGBB */
#endif
