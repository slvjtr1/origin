/* tests/host: the C library calls memctl.c makes, as the test provides them (any computer runs the worker) */
#ifndef SB_MEMCTL_TEST_SHIM_H
#define SB_MEMCTL_TEST_SHIM_H
#define M_ARENA_MAX -8
struct mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
int mallopt(int param, int value);
int malloc_trim(size_t pad);
struct mallinfo mallinfo(void);
#endif
