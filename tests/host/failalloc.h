/* forced into the module under test (-include): its allocations can be made to fail. The test defines the fa_* calls
   (in a file compiled without this header) and arms them: the Nth allocation from now fails, or all of them do. */
#ifndef SB_FAILALLOC_H
#define SB_FAILALLOC_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
void *fa_malloc(size_t n);
void *fa_calloc(size_t n, size_t k);
void *fa_realloc(void *p, size_t n);
char *fa_strdup(const char *s);
#define malloc(n) fa_malloc(n)
#define calloc(n, k) fa_calloc(n, k)
#define realloc(p, n) fa_realloc(p, n)
#define strdup(s) fa_strdup(s)
#endif
