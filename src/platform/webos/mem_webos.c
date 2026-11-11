#include "platform/mem.h"
#include <stdio.h>

/* a small read of a kernel file (no disk): fine once every few seconds on the main thread */
int plat_mem_free_mb(void) {
  FILE *f = fopen("/proc/meminfo", "r"); if (!f) return -1;
  char line[128]; long kb = -1; while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %ld kB", &kb) == 1) break;
  fclose(f); return kb < 0 ? -1 : (int)(kb / 1024);
}
void plat_mem_self(int *rssMb, int *swapMb) {
  *rssMb = *swapMb = -1; FILE *f = fopen("/proc/self/status", "r"); if (!f) return;
  char line[128]; long kb; while (fgets(line, sizeof line, f)) { if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) *rssMb = (int)(kb / 1024); else if (sscanf(line, "VmSwap: %ld kB", &kb) == 1) *swapMb = (int)(kb / 1024); }
  fclose(f);
}
