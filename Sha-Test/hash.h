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
    
    /*
		State st is not partitionable because the union of b[200] and q[25] 
		means addressing the same memory area in two different way, (i.e two alias)
		the  two representation are needed because in the hybrid implementation of ascon and sha
		we need to access the state array in two different way.

		Because of the union, HLS maps the state st into a dual-port RAM with at most 2 accesses/cycle.
		this constitutes the main bottleneck of the whole design
		and the reason why several loops cannot reach II=1
		(any loop touching >2 state words per iteration is port-bound).
		
		Removing the union (keeping only q[25] and computing byte views with shifts/masks)
		is the main logic change to unlock array partitioning on the state.
	
		SHA3 uses all 25 state words:
		- theta and chi reads and writes all 25, rho_pi touches 24.
		Here the single 2-port state RAM limits the most.
	*/
    
    int pt, rsiz, mdlen, mode;		// these don't overflow
	
} hash_ctx_t;

// Compression function.
void keccakf_asconp12 (uint64_t st[25]);

// OpenSSL - like interfece
int hash_init	(hash_ctx_t *c, int mdlen);	// mdlen = hash output in bytes
int hash_update	(hash_ctx_t *c, const void *data, size_t len);
int hash_final	(void *md, hash_ctx_t *c);    // digest goes to md

// compute a sha3 hash (md) of given byte length from "in"
void hash(const void *in, size_t inlen, void *md, int mdlen);

//EDIT added the prototype of the hash_top function wrapper
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen);
 
#endif