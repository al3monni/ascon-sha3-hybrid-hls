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

// state context
typedef struct {

    /*
        [WAS] union { uint8_t b[200]; uint64_t q[25]; } st;
              Two aliases of the same storage, sized for Keccak: Ascon uses only
              5 of the 25 words (20 words of dead storage). Because of the union
              HLS could not split the context: the whole struct (state + counters)
              was mapped into ONE memory (csynth storage report: H_i, 64-bit x 27
              words), so no ARRAY_PARTITION on the state.

        [OPT] B-01: word-only state of exactly the 5 words Ascon uses (320 bits).
              Unlike Hybrid/SHA-3, no generic byte addressing is needed: the rate
              is 8 bytes = st[0], so every byte access (absorb, padding, squeeze)
              is a lane select on st[0] (st0_get_byte() / st0_xor_byte() in hash.c)
              with a constant word index. B-01 adds no partitioning pragma: its
              effect is measured alone.
    */

    uint64_t st[5];                     // Ascon state: 5 x 64-bit words, rate = st[0]

    int pt, rsiz, mdlen;		// these don't overflow

} hash_ctx_t;

// Compression function.
void keccakf_asconp12 (uint64_t st[5]);       // [WAS] uint64_t st[25]

// OpenSSL - like interfece
int hash_init	(hash_ctx_t *c, int mdlen);	// mdlen = hash output in bytes
int hash_update	(hash_ctx_t *c, const void *data, size_t len);
int hash_final	(void *md, hash_ctx_t *c);    // digest goes to md

// compute a sha3 hash (md) of given byte length from "in"
void hash(const void *in, size_t inlen, void *md, int mdlen);

//EDIT added the prototype of the hash_top function wrapper
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen);

#endif