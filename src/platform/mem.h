/* The memory the device can still give (Linux's MemAvailable), for work that holds a lot of it while the
   user browses (the background trailer keeps a whole video pipeline open). */
#ifndef SB_PLAT_MEM_H
#define SB_PLAT_MEM_H
int plat_mem_free_mb(void);   /* -1 when unknown */
void plat_mem_self(int *rssMb, int *swapMb); /* this process: resident and swapped out (-1 when unknown) */
#endif
