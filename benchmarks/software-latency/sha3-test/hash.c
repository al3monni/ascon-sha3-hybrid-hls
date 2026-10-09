#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

/*
 * Single-purpose SHA-3 implementation (Keccak-f[1600], 24 rounds).
 */

void theta(uint64_t st[]) {

    int i, j;
    uint64_t t;       /* t  - temporary variable for swaps/rotations         */
    uint64_t bc[5];   /* bc - temporary working array                        */

    /* ----- Theta step (mix columns) ----- */

    /* Theta C-function: compute column parities. Each column is XORed. */
    theta_C: for (i = 0; i < 5; i++) {
        bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
    }

    /* Theta D-function: diffuse parity bits into the state. */
    theta_D: for (i = 0; i < 5; i++) {
        t = bc[(i + 4) % 5] ^ ROTL64(bc[(i + 1) % 5], 1);
        theta_D_loop: for (j = 0; j < 25; j += 5) {
            st[j + i] ^= t;
        }
    }
}

void rho_pi(uint64_t st[]) {

    /* Constants used during Rho (rotation) and Pi (permutation) steps. */

    const int keccakf_rotc[24] = {
        1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
        27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
    };

    const int keccakf_piln[24] = {
        10, 7,  11, 17, 18, 3,  5,  16, 8,  21, 24, 4,
        15, 23, 19, 13, 12, 2,  20, 14, 22, 9,  6,  1
    };

    int i, j;
    uint64_t t;       /* t  - temporary for swaps/rotations           */
    uint64_t bc[5];   /* bc - temporary working array                 */

    /* ----- Rho and Pi steps: rotate and rearrange state bits. ----- */

    t = st[1];

    rho_pi_loop: for (i = 0; i < 24; i++) {
        j = keccakf_piln[i];
        bc[0] = st[j];
        st[j] = ROTL64(t, keccakf_rotc[i]);
        t = bc[0];
    }
}

void chi(uint64_t st[], int chirounds) {

    int i, j;
    uint64_t bc[5];   /* bc - intermediate Chi copies */

    /* ----- Combined Chi (nonlinear layer) -----
     * Same nonlinear function used in both Keccak and Ascon. Here
     * `chirounds` is fixed at 25 (the full SHA-3 state). */

    top_chi: for (j = 0; j < chirounds; j += 5) {

        bc_cp_chi: for (i = 0; i < 5; i++) {
            bc[i] = st[j + i];
        }

        chi_nlin: for (i = 0; i < 5; i++) {
            /* Nonlinear permutation part. */
            st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
    }
}

void iota(uint64_t st[], int r) {

    /* 24 Keccak round constants; XORed into word 0 every round. */

    const uint64_t keccakf_rndc[24] = {
        0x0000000000000001, 0x0000000000008082, 0x800000000000808a,
        0x8000000080008000, 0x000000000000808b, 0x0000000080000001,
        0x8000000080008081, 0x8000000000008009, 0x000000000000008a,
        0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
        0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
        0x8000000000008003, 0x8000000000008002, 0x8000000000000080,
        0x000000000000800a, 0x800000008000000a, 0x8000000080008081,
        0x8000000000008080, 0x0000000080000001, 0x8000000080008008
    };

    /* ----- Iota step: inject round constant into word 0. ----- */
    st[0] ^= keccakf_rndc[r];
}

void keccakf_asconp12(uint64_t st[25]) {

    /*
     * Single-purpose Keccak-f[1600] permutation: 24 rounds, full 25-word
     * state. The function name is kept from the hybrid version for source
     * compatibility, but here it is pure SHA-3.
     */

    int r;
    int nrounds   = 24;
    int chirounds = 25;

    rounds_loop: for (r = 0; r < nrounds; r++) {
        theta_step:   theta (st);
        rho_pi_step:  rho_pi(st);
        chi_step:     chi   (st, chirounds);
        iota_step:    iota  (st, r);
    }
}

int hash_init(hash_ctx_t *c, int mdlen) {

    /* Initializes a SHA-3 hash context with the requested digest length. */

    int i;

    init_loop: for (i = 0; i < 25; i++) {
        c->st.q[i] = 0;
    }

    /* SHA-3 "capacity" formula: rate = 200 - 2*digest_bytes. */
    c->mdlen = mdlen;
    c->rsiz  = 200 - 2 * mdlen;
    c->pt    = 0;

    return 1;
}

int hash_update(hash_ctx_t *c, const void *data, size_t len) {

    /* Absorbs input data into the state. */

    size_t i;
    int j;

    j = c->pt;

    absorbing_loop: for (i = 0; i < len; i++) {
        /* XOR message bytes into the state one by one (absorb phase). */
        c->st.b[j++] ^= ((const uint8_t *) data)[i];

        /* When the block is full (rsiz), permute it. */
        if (j >= c->rsiz) {
            absorb_perm: keccakf_asconp12(c->st.q);
            j = 0;
        }
    }

    c->pt = j;
    return 1;
}

int hash_final(void *md, hash_ctx_t *c) {

    /* Finalizes the hash and emits the digest. */

    int i;

    /* --- Domain-separation padding (SHA-3 multi-rate padding). --- */
    c->st.b[c->pt]       ^= 0x06;
    c->st.b[c->rsiz - 1] ^= 0x80;

    /* Final permutation after padding. */
    final_perm: keccakf_asconp12(c->st.q);

    /* Output mdlen bytes directly. */
    output_loop: for (i = 0; i < c->mdlen; i++) {
        ((uint8_t *) md)[i] = c->st.b[i];
    }

    return 1;
}

void hash(const void *in, size_t inlen, void *md, int mdlen) {

    /* One-shot wrapper. */

    hash_ctx_t H;

    init_call: hash_init  (&H, mdlen);
    abs_call:  hash_update(&H, in, inlen);
    sqz_call:  hash_final (md, &H);
}

void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen) {

    /* Wrapper kept from the original HLS cosim top-level. */
    hash(in, inlen, md, mdlen);
}
