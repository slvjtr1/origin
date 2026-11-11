/* Stats for nerds: the corner panel of memory, storage and frame numbers (Settings > About) */
#ifndef SB_NERDS_H
#define SB_NERDS_H
int  nerds_on(void);
void nerds_frame(float cpuMs, int calls, int quads); /* once a frame, from the loop */
void nerds_draw(void);                                /* over everything, when on */
#endif
