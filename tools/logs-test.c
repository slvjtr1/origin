/* Checks the log-sharing crypto against published vectors, and gives tools/logs-check.js what it needs to
   compare this build's scrubbing and sealing with the web app's own code:
     logs-test vectors                       SHA-256, HMAC (RFC 4231), PBKDF2 (RFC 7914), AES (FIPS-197), GCM test cases
     logs-test gcm KEY IV AAD PT             hex in, "ct tag" out
     logs-test pbkdf2 PW SALT ROUNDS LEN     text in, hex out
     logs-test scrub line|share IN OUT       scrubs IN (one entry per NUL-separated record) into OUT
     logs-test ecdh PRIV PUB                 P-256 shared secret (hex in: 32-byte key, 65-byte point)
     logs-test seal CODE PUBB64 IN OUT       seals IN as the TV does, to that public key
   Built and run by tools/logs-check.sh. */
#include "core/logseal.h"
#include "sbcrypto.h"
#include "aes.h"
#include "uecc/uECC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int fails;
static size_t unhex(const char *h, unsigned char *out) { size_t n = strlen(h) / 2; for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (unsigned char)v; } return n; }
static void hex(const unsigned char *b, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]); out[2 * n] = 0; }
static void check(const char *name, const unsigned char *got, size_t n, const char *wantHex) {
  char g[4096]; hex(got, n, g); int ok = !strcmp(g, wantHex); if (!ok) fails++;
  printf("%-28s %s\n", name, ok ? "ok" : "FAIL"); if (!ok) printf("   got  %s\n   want %s\n", g, wantHex);
}
static void gcm_case(const char *name, const char *k, const char *iv, const char *aad, const char *pt, const char *ct, const char *tag) {
  unsigned char K[32], I[12], A[64], P[128], C[128], T[16], D[128]; size_t kl = unhex(k, K), al = unhex(aad, A), pl = unhex(pt, P); unhex(iv, I);
  sbc_gcm_encrypt(K, (int)kl * 8, I, A, al, P, pl, C, T);
  char nm[64]; snprintf(nm, sizeof nm, "%s ct", name); check(nm, C, pl, ct);
  snprintf(nm, sizeof nm, "%s tag", name); check(nm, T, 16, tag);
  int ok = sbc_gcm_decrypt(K, (int)kl * 8, I, A, al, C, pl, T, D) && !memcmp(D, P, pl); T[0] ^= 1; ok = ok && !sbc_gcm_decrypt(K, (int)kl * 8, I, A, al, C, pl, T, D);
  printf("%-28s %s\n", "  open + tamper check", ok ? "ok" : "FAIL"); if (!ok) fails++;
}
static void vectors(void) {
  unsigned char h[64], k[200]; memset(k, 0x0b, 20);
  sbc_sha256("abc", 3, h); check("sha256 abc", h, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  sbc_sha256("", 0, h); check("sha256 empty", h, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; sbc_sha256(m, strlen(m), h); check("sha256 448 bits", h, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  sbc_hmac_sha256(k, 20, (const unsigned char *)"Hi There", 8, h); check("hmac rfc4231 #1", h, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
  sbc_hmac_sha256((const unsigned char *)"Jefe", 4, (const unsigned char *)"what do ya want for nothing?", 28, h); check("hmac rfc4231 #2", h, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  memset(k, 0xaa, 131); m = "Test Using Larger Than Block-Size Key - Hash Key First"; sbc_hmac_sha256(k, 131, (const unsigned char *)m, strlen(m), h); check("hmac rfc4231 #6", h, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
  sbc_pbkdf2_sha256((const unsigned char *)"passwd", 6, (const unsigned char *)"salt", 4, 1, h, 64);
  check("pbkdf2 rfc7914 c=1", h, 64, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
  sbc_pbkdf2_sha256((const unsigned char *)"Password", 8, (const unsigned char *)"NaCl", 4, 80000, h, 64);
  check("pbkdf2 rfc7914 c=80000", h, 64, "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
  sbc_pbkdf2_sha256((const unsigned char *)"password", 8, (const unsigned char *)"salt", 4, 4096, h, 32);
  check("pbkdf2 password/salt 4096", h, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
  { unsigned char key[32], in[16], out[16]; WORD ks[60]; unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key); unhex("00112233445566778899aabbccddeeff", in);
    aes_key_setup(key, ks, 256); aes_encrypt(in, out, ks, 256); check("aes-256 fips-197 c.3", out, 16, "8ea2b7ca516745bfeafc49904b496089"); }
  gcm_case("gcm #2 (128)", "00000000000000000000000000000000", "000000000000000000000000", "", "00000000000000000000000000000000", "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf");
  gcm_case("gcm #13", "0000000000000000000000000000000000000000000000000000000000000000", "000000000000000000000000", "", "", "", "530f8afbc74536b9a963b4f1c4cb738b");
  gcm_case("gcm #14", "0000000000000000000000000000000000000000000000000000000000000000", "000000000000000000000000", "", "00000000000000000000000000000000", "cea7403d4d606b6e074ec5d3baf39d18", "d0d1c8a799996bf0265b98b5d48ab919");
  gcm_case("gcm #15", "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "",
    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
    "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad", "b094dac5d93471bdec1a502270e3cc6c");
  gcm_case("gcm #16", "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "feedfacedeadbeeffeedfacedeadbeefabaddad2",
    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
    "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662", "76fc6ece0f4e1768cddf8853bb2d551b");
  { char c[5]; int ok = logseal_code(c) && strlen(c) == 4 && strspn(c, "0123456789ABCDEFGHJKMNPQRSTVWXYZ") == 4; printf("%-28s %s (%s)\n", "code alphabet", ok ? "ok" : "FAIL", c); if (!ok) fails++; }
}
static char *slurp(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); } fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc((size_t)l + 1); *n = fread(b, 1, (size_t)l, f); b[*n] = 0; fclose(f); return b; }
int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "vectors")) { vectors(); printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails); return fails ? 1 : 0; }
  if (argc == 6 && !strcmp(argv[1], "gcm")) { unsigned char K[32], I[12], A[256], P[1024], C[1024], T[16]; size_t kl = unhex(argv[2], K), al = unhex(argv[4], A), pl = unhex(argv[5], P); unhex(argv[3], I);
    sbc_gcm_encrypt(K, (int)kl * 8, I, A, al, P, pl, C, T); char a[2100], b[40]; hex(C, pl, a); hex(T, 16, b); printf("%s %s\n", a, b); return 0; }
  if (argc == 6 && !strcmp(argv[1], "pbkdf2")) { unsigned char o[128]; size_t n = (size_t)atoi(argv[5]); sbc_pbkdf2_sha256((unsigned char *)argv[2], strlen(argv[2]), (unsigned char *)argv[3], strlen(argv[3]), (unsigned)atoi(argv[4]), o, n); char h[300]; hex(o, n, h); printf("%s\n", h); return 0; }
  if (argc == 5 && !strcmp(argv[1], "scrub")) { size_t n; char *in = slurp(argv[3], &n); FILE *o = fopen(argv[4], "wb");
    for (size_t i = 0; i < n;) { char *r = !strcmp(argv[2], "line") ? logscrub_line(in + i) : logscrub_share(in + i); fwrite(r, 1, strlen(r) + 1, o); free(r); i += strlen(in + i) + 1; }
    fclose(o); return 0; }
  if (argc == 4 && !strcmp(argv[1], "ecdh")) { unsigned char pr[32], pu[65], sh[32]; unhex(argv[2], pr); unhex(argv[3], pu); char h[70];
    if (!uECC_shared_secret(pu + 1, pr, sh, uECC_secp256r1())) return 1; hex(sh, 32, h); printf("%s\n", h); return 0; }
  if (argc == 6 && !strcmp(argv[1], "seal")) { size_t n, pl = 0; char *in = slurp(argv[4], &n); unsigned char pub[65]; static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t v = 0; int bits = 0; for (const char *q = argv[3]; *q && pl < 65; q++) { const char *k = strchr(A, *q); if (!k || *q == '=') continue; v = v << 6 | (uint32_t)(k - A); bits += 6; if (bits >= 8) { bits -= 8; pub[pl++] = (unsigned char)(v >> bits); } }
    char *s = logseal_seal(argv[2], pub, in, n); if (!s) return 1; FILE *o = fopen(argv[5], "wb"); fwrite(s, 1, strlen(s), o); fclose(o); return 0; }
  fprintf(stderr, "usage: see the top of tools/logs-test.c\n"); return 2;
}
