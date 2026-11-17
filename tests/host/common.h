/* tests/host: what the host tests share. Each test is one program; run.sh builds and runs them all. */
#ifndef SB_HOST_COMMON_H
#define SB_HOST_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define CHECKF(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static double host_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void host_sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }
static int host_done(const char *name) { printf("%s: %d check(s), %d failed\n", name, checks, fails); return fails ? 1 : 0; }
#endif
