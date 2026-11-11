/* HMAC-SHA256, PBKDF2-HMAC-SHA256 and AES-GCM (96-bit IVs) on top of Brad Conte's public-domain SHA-256
   and AES (sha256.c, aes.c beside this file). No OpenSSL: the TV's libcrypto is not guaranteed.
   Checked against RFC 4231, RFC 7914 and the GCM specification's test cases (tools/crypto-test.c). */
#ifndef SBCRYPTO_H
#define SBCRYPTO_H
#include <stddef.h>
#include <stdint.h>

void sbc_sha256(const void *data, size_t len, uint8_t out[32]);
void sbc_hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]);
void sbc_pbkdf2_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t slen, uint32_t rounds, uint8_t *out, size_t outlen);
/* keyBits 128/192/256; iv is 12 bytes; ct may be pt */
void sbc_gcm_encrypt(const uint8_t *key, int keyBits, const uint8_t iv[12], const uint8_t *aad, size_t aadlen,
                     const uint8_t *pt, size_t len, uint8_t *ct, uint8_t tag[16]);
/* 1 = the tag matched (pt then holds the plain text), 0 = tampered or a wrong key */
int  sbc_gcm_decrypt(const uint8_t *key, int keyBits, const uint8_t iv[12], const uint8_t *aad, size_t aadlen,
                     const uint8_t *ct, size_t len, const uint8_t tag[16], uint8_t *pt);
#endif
