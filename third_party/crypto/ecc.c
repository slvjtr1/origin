/* micro-ecc (uecc/, BSD-2-Clause) built for P-256 only, in portable C (no per-architecture assembly). */
#define uECC_PLATFORM 0          /* uECC_arch_other */
#define uECC_SUPPORTS_secp160r1 0
#define uECC_SUPPORTS_secp192r1 0
#define uECC_SUPPORTS_secp224r1 0
#define uECC_SUPPORTS_secp256r1 1
#define uECC_SUPPORTS_secp256k1 0
#define uECC_SUPPORT_COMPRESSED_POINT 0
#include "uecc/uECC.c"
