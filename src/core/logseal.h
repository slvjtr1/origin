/* The pieces of "Share logs" that touch no state: taking secrets out of log text, the code, and sealing a
   bundle to the developer's public key so that only the matching private key opens it (the code only finds it). */
#ifndef SB_LOGSEAL_H
#define SB_LOGSEAL_H
#include <stddef.h>

/* one app log line as it is kept: query strings, key-like runs and emails out (malloc'd) */
char *logscrub_line(const char *line);
/* the whole bundle before it leaves the TV: account / addon / debrid tokens, link queries and token-like
   path parts, emails, IP addresses, info hashes shortened (malloc'd) */
char *logscrub_share(const char *text);
/* 4 characters from a 32-letter alphabet, from the system's random source; 0 when it cannot be read */
int   logseal_code(char out[5]);
/* the page left on the relay, base64 of SBL3 | one-time key (65) | iv (12) | tag (16) | AES-256-GCM(gzip(text)):
   sealed to the developer's P-256 public key (65 bytes, uncompressed), key = sha256("stremioorigin-logs-v1|" + code +
   "|" + one-time key + shared secret), AAD = "SBL3|" + code. The gzip keeps the newest part under 40 000 bytes
   (the page holds about 64 KB). malloc'd, NULL on failure */
/* a base64 text to bytes; the count */
size_t logseal_unb64(const char *s, unsigned char *out, size_t cap);
char *logseal_seal(const char *code, const unsigned char devPub[65], const char *text, size_t len);
#endif
