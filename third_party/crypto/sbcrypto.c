#include "sbcrypto.h"
#include "sha256.h"
#include "aes.h"
#include <string.h>

void sbc_sha256(const void *data, size_t len, uint8_t out[32]) { SHA256_CTX c; sha256_init(&c); sha256_update(&c, data, len); sha256_final(&c, out); }

/* the key's inner and outer pads absorbed once: each HMAC after that costs two short hashes */
typedef struct { SHA256_CTX in, out; } Hmac;
static void hmac_init(Hmac *h, const uint8_t *key, size_t klen) {
  uint8_t k[64] = { 0 }, pad[64];
  if (klen > 64) sbc_sha256(key, klen, k); else memcpy(k, key, klen);
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36; sha256_init(&h->in); sha256_update(&h->in, pad, 64);
  for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c; sha256_init(&h->out); sha256_update(&h->out, pad, 64);
}
static void hmac_run(const Hmac *h, const uint8_t *m1, size_t l1, const uint8_t *m2, size_t l2, uint8_t out[32]) {
  SHA256_CTX c = h->in; uint8_t ih[32];
  sha256_update(&c, m1, l1); if (l2) sha256_update(&c, m2, l2); sha256_final(&c, ih);
  c = h->out; sha256_update(&c, ih, 32); sha256_final(&c, out);
}
void sbc_hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) { Hmac h; hmac_init(&h, key, klen); hmac_run(&h, msg, mlen, NULL, 0, out); }

void sbc_pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t rounds, uint8_t *out, size_t outlen) {
  Hmac h; hmac_init(&h, pw, pwlen);
  for (uint32_t blk = 1; outlen; blk++) {
    uint8_t be[4] = { (uint8_t)(blk >> 24), (uint8_t)(blk >> 16), (uint8_t)(blk >> 8), (uint8_t)blk }, u[32], t[32];
    hmac_run(&h, salt, slen, be, 4, u); memcpy(t, u, 32);
    for (uint32_t r = 1; r < rounds; r++) { hmac_run(&h, u, 32, NULL, 0, u); for (int i = 0; i < 32; i++) t[i] ^= u[i]; }
    size_t n = outlen < 32 ? outlen : 32; memcpy(out, t, n); out += n; outlen -= n;
  }
}

/* GHASH: multiplication in GF(2^128) with the specification's bit order, one bit at a time (the logs are small) */
static void gf_mul(uint8_t x[16], const uint8_t h[16]) {
  uint8_t z[16] = { 0 }, v[16]; memcpy(v, h, 16);
  for (int i = 0; i < 128; i++) {
    if (x[i >> 3] & (0x80 >> (i & 7))) for (int j = 0; j < 16; j++) z[j] ^= v[j];
    int lsb = v[15] & 1; for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | (v[j - 1] << 7)); v[0] >>= 1;
    if (lsb) v[0] ^= 0xe1;
  }
  memcpy(x, z, 16);
}
static void ghash_blocks(uint8_t y[16], const uint8_t h[16], const uint8_t *d, size_t len) {
  while (len) { size_t n = len < 16 ? len : 16; for (size_t i = 0; i < n; i++) y[i] ^= d[i]; gf_mul(y, h); d += n; len -= n; }
}
static void gcm_core(const uint8_t *key, int keyBits, const uint8_t iv[12], const uint8_t *aad, size_t aadlen,
                     const uint8_t *in, size_t len, uint8_t *out, int enc, uint8_t tag[16]) {
  WORD ks[60]; uint8_t h[16] = { 0 }, j0[16], ctr[16], ks16[16], y[16] = { 0 };
  aes_key_setup(key, ks, keyBits); aes_encrypt(h, h, ks, keyBits);
  memcpy(j0, iv, 12); j0[12] = j0[13] = j0[14] = 0; j0[15] = 1; memcpy(ctr, j0, 16);
  ghash_blocks(y, h, aad, aadlen);
  for (size_t off = 0; off < len; off += 16) {
    for (int i = 15; i >= 12; i--) if (++ctr[i]) break; /* inc32 */
    aes_encrypt(ctr, ks16, ks, keyBits);
    size_t n = len - off < 16 ? len - off : 16;
    if (!enc) ghash_blocks(y, h, in + off, n);
    for (size_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks16[i];
    if (enc) ghash_blocks(y, h, out + off, n);
  }
  uint8_t lens[16]; uint64_t a = (uint64_t)aadlen * 8, c = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++) { lens[i] = (uint8_t)(a >> (56 - 8 * i)); lens[8 + i] = (uint8_t)(c >> (56 - 8 * i)); }
  ghash_blocks(y, h, lens, 16);
  aes_encrypt(j0, ks16, ks, keyBits); for (int i = 0; i < 16; i++) tag[i] = y[i] ^ ks16[i];
}
void sbc_gcm_encrypt(const uint8_t *key, int keyBits, const uint8_t iv[12], const uint8_t *aad, size_t aadlen, const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]) {
  gcm_core(key, keyBits, iv, aad, aadlen, pt, len, ct, 1, tag);
}
int sbc_gcm_decrypt(const uint8_t *key, int keyBits, const uint8_t iv[12], const uint8_t *aad, size_t aadlen, const uint8_t *ct, size_t len, const uint8_t tag[16], uint8_t *pt) {
  uint8_t t[16], d = 0; gcm_core(key, keyBits, iv, aad, aadlen, ct, len, pt, 0, t);
  for (int i = 0; i < 16; i++) d |= t[i] ^ tag[i];
  if (d) { memset(pt, 0, len); return 0; }
  return 1;
}
