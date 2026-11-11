/* The TV's own picture and sound processors, tuned while a title plays and put back as they were when it stops.
   The TV draws the video and plays its sound itself, so its processors are the ones that can work on them: nothing is
   done to the frames by the app, the frame rate never changes.
     Upscale (anime and films apart, each Off / C / B / A, A the strongest): Super Resolution, sharpness, gradation,
     noise cleaning, set for drawn pictures or for filmed ones.
     Brightness: the panel's light up, energy saving off.
     See in the dark: the TV's AI brightness for dark areas (it follows the room's light) and a slight lift of black.
     Clear voice: the TV's dialogue enhancer. Even volume: its volume levelling.
   Only the settings this TV has are changed. Needs the Homebrew Channel with root (as the app's reopening after an
   update); without it nothing changes. */
#ifndef SB_TVTUNE_H
#define SB_TVTUNE_H
#include <stddef.h>
const char *tvtune_level(int anime);   /* "off", "C", "B", "A" (Settings) */
void tvtune_start(int anime);          /* a title starts (main thread): tuned on a worker */
void tvtune_stop(void);                /* it stops: put back */
void tvtune_cycle(int anime);          /* the player's button: the next level, applied at once */
int  tvtune_poll(char *msg, size_t cap); /* 1 once with the notice to show (the upscale level the TV took) */
int  tvtune_state(void);               /* -1 not known yet, 0 no root here, 1 the TV takes it */
void tvtune_tick(void);                /* per frame (main thread): the store follows the worker */
void tvtune_quit(void);                /* the app closing: put back at once */
#endif
