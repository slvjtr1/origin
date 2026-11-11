/* The subtitles' edge and shadow, as the settings set them: one place reads them and paints them, so the player
   (plain text and the file's own lines) and the settings' preview draw the same line. A line is painted as layers
   of the same text: a soft drop shadow (copies of the line, offset down and to the right, spread round it and
   faint, so they add up to a dark core with a soft edge), then the outline (copies round it in the outline colour),
   then the line itself. Each copy is one quad of a line already in the text cache. */
#ifndef SB_SUBFX_H
#define SB_SUBFX_H
#include <stdint.h>

typedef struct {
  unsigned ser;            /* the store's serial it was read at */
  int outline;             /* the outline on */
  float ow;                /* its width in px (at the size read for) */
  uint32_t ol;             /* its colour */
  int shadow;              /* the drop shadow on */
  float sa, sblur, sdist;  /* its strength (0..1 at the core), softness and distance in px */
  int srings;              /* rings of copies for the softness: 0 sharp, 1 soft, 2 very soft */
  int wt;                  /* the text weight to draw with: bold or not, and the chosen font (TEXT_FACE) */
  float lift;              /* the line raised (or lowered, below 0) by this part of the screen's height */
} SubFx;

/* the height's limits and step (a part of the screen's height; 0 where the player has always put them) */
#define SUBFX_LIFT_MIN (-0.06f)
#define SUBFX_LIFT_MAX 0.30f
#define SUBFX_LIFT_STEP 0.02f
/* the settings now, for a line px tall (the shadow grows with the line; the outline keeps its width, as before) */
void subfx_read(SubFx *fx, int px);
/* paints the layers: fn(ud, dx, dy, colour) draws the line moved by dx, dy in that colour; fill: the line's own colour */
typedef void (*SubPaint)(void *ud, float dx, float dy, uint32_t col);
void subfx_paint(const SubFx *fx, uint32_t fill, SubPaint fn, void *ud);
#endif
