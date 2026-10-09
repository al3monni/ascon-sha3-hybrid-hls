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

    int pt, rsiz, mdlen, mode;
} hash_ctx_t;

/* Compression function. */
void keccakf_asconp12(uint64_t st[25], uint8_t mode);

/* OpenSSL-like interface. */
int hash_init  (hash_ctx_t *c, int mdlen, uint8_t mode);  /* mdlen = digest bytes */
int hash_update(hash_ctx_t *c, const void *data, size_t len, uint8_t mode);
int hash_final (void *md, hash_ctx_t *c, uint8_t mode);   /* digest into md */

/* One-shot helper: compute the hash of `inlen` bytes from `in`. */
void hash(const void *in, size_t inlen, void *md, int mdlen, uint8_t mode);

/* Wrapper kept from the HLS top-level signature (fixed-size arrays). */
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode);

#endif
