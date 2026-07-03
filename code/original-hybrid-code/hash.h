 
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
    union {					// state:
        uint8_t b[200];		// 8-bit bytes
        uint64_t q[25];		// 64-bit words
    } st;
	
    int pt, rsiz, mdlen, mode;		// these don't overflow
	
} hash_ctx_t;

// Compression function.
void keccakf_asconp12 (uint64_t st[25], uint8_t mode);

// OpenSSL - like interfece
int hash_init	(hash_ctx_t *c, int mdlen, uint8_t mode);	// mdlen = hash output in bytes
int hash_update	(hash_ctx_t *c, const void *data, size_t len, uint8_t mode);
int hash_final	(void *md, hash_ctx_t *c, uint8_t mode);    // digest goes to md

// compute a sha3 hash (md) of given byte length from "in"
void *hash(const void *in, size_t inlen, void *md, int mdlen, uint8_t mode);
 
#endif