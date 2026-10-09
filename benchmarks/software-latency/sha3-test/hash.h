#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

#ifndef ROTL64
#define ROTL64(x, y) (((x) << (y)) | ((x) >> (64 - (y))))
#endif

#ifndef ROTR64
#define ROTR64(x, y) (((x) >> (y)) | ((x) << (64 - (y))))
#endif

/* state context */
typedef struct {
    union {                 /* state:                            */
        uint8_t  b[200];    /*  - 8-bit  byte view               */
        uint64_t q[25];     /*  - 64-bit word view               */
    } st;

    /*
     * The two views above share the same memory area; we need both
     * because some loops address the state byte-wise (see the absorb
     * loop) and some word-wise.
     */

    int pt, rsiz, mdlen;
} hash_ctx_t;

/* Compression function (SHA-3 / Keccak-f[1600]). */
void keccakf_asconp12(uint64_t st[25]);

/* OpenSSL-like interface. */
int hash_init  (hash_ctx_t *c, int mdlen);                       /* mdlen = digest bytes */
int hash_update(hash_ctx_t *c, const void *data, size_t len);
int hash_final (void *md, hash_ctx_t *c);                        /* digest into md */

/* One-shot helper: compute SHA-3 digest of given byte length from `in`. */
void hash(const void *in, size_t inlen, void *md, int mdlen);

/* Wrapper kept from the HLS top-level signature (fixed-size arrays). */
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen);

#endif
