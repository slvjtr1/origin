/* The sleep timer: the player's moon button. Off, 15, 30, 45, 60 or 90 minutes, or the end of what plays. A minute
   before, a pill at the top counts down (OK: 15 minutes more); then the film pauses with its place kept and the screen
   goes dark. Any key brings the film back, paused. */
#ifndef SB_SLEEP_H
#define SB_SLEEP_H
#include <stddef.h>

int  sleep_count(int withEnd);            /* how many choices the button cycles through (withEnd: "when this ends" is one) */
int  sleep_index(void);                   /* the choice now, 0 off */
void sleep_cycle(int withEnd);            /* the button pressed: the next choice */
void sleep_label(char *out, size_t cap);  /* "Sleep timer off", "Sleep in 28 min", "Sleep when this ends" */
int  sleep_armed(void);                   /* a choice is set */
/* once a frame while the player is open: 1 when it is time to sleep (the caller pauses the film); atEnd: what plays is
   at its end (the credits, the next-up card, or it ended) */
int  sleep_tick(int atEnd);
int  sleep_asleep(void);                  /* the dark screen is up */
int  sleep_key(int k);                    /* a key: 1 when it was the timer's (it woke the screen, or OK on the countdown) */
void sleep_player_closed(void);           /* the player went away: the dark screen with it, "when this ends" too */
void sleep_draw(void);                    /* the countdown pill and the dark screen, over the player */
#endif
