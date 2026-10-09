#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

/*
 * Single-purpose Ascon-Hash implementation (Ascon-p[12], 12 rounds).
 */

void chi(uint64_t st[], int chirounds) {

    int i, j;
    uint64_t bc[5];   /* bc - intermediate Chi copies */

    /* ----- Combined Chi (nonlinear layer) -----
     * Same nonlinear function used in Keccak and Ascon. Here `chirounds`
     * is fixed at 5 (the first row of the state, which is all Ascon uses). */

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

void diffusion(uint64_t st[]) {

    int i;
    uint64_t bc[5];   /* intermediate copy of st[0..4] */

    /*
     * Ascon's linear diffusion layer: a combination of bit rotations and
     * XORs applied to the first five state words.
     */

    bc_cp_diff: for (i = 0; i < 5; i++) {
        bc[i] = st[i];
    }

    st[0] = bc[0] ^ ROTR64(bc[0], 19) ^ ROTR64(bc[0], 28);
    st[1] = bc[1] ^ ROTR64(bc[1], 61) ^ ROTR64(bc[1], 39);
    st[2] = bc[2] ^ ROTR64(bc[2],  1) ^ ROTR64(bc[2],  6);
    st[3] = bc[3] ^ ROTR64(bc[3], 10) ^ ROTR64(bc[3], 17);
    st[4] = bc[4] ^ ROTR64(bc[4],  7) ^ ROTR64(bc[4], 41);
}

void keccakf_asconp12(uint64_t st[25]) {

    /*
     * Single-purpose Ascon-p[12] permutation: 12 rounds, 5-word state.
     * The function name is kept from the hybrid version for source
     * compatibility, but here it is pure Ascon.
     */

    /* Ascon's 12 round constants, applied to the middle word of the state. */
    const uint8_t asconp_rndc[12] = {
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5,
        0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b
    };

    int r;
    int nrounds   = 12;
    int chirounds =  5;

    rounds_loop: for (r = 0; r < nrounds; r++) {

        /* Add round constant to word 2, then a simple XOR mixing. */
        st[2] ^= asconp_rndc[r];
        st[0] ^= st[4];
        st[4] ^= st[3];
        st[2] ^= st[1];

        /* ----- Combined Chi (nonlinear layer). ----- */
        chi_step: chi(st, chirounds);

        /* ----- Post-Chi adjustments -----
         * Linear tweaks that convert Keccak's chi output into Ascon's
         * linearly equivalent S-box behaviour. */
        st[1] ^= st[0];
        st[0] ^= st[4];
        st[3] ^= st[2];
        st[2] = ~st[2];

        /* ----- Linear diffusion layer (bit rotations + XORs). ----- */
        diffusion_step: diffusion(st);
    }
}

int hash_init(hash_ctx_t *c, int mdlen) {

    /* Initializes an Ascon-Hash context. */

    int i;
    uint64_t ASCON_HASH_IV = 0x0000080100cc0002;   /* Ascon IV (spec) */

    init_loop: for (i = 0; i < 25; i++) {
        c->st.q[i] = 0;
    }

    /* Word 0 is seeded with the Ascon IV. */
    c->st.q[0] = ASCON_HASH_IV;

    /* Ascon has a fixed 8-byte rate. */
    c->mdlen = mdlen;
    c->rsiz  = 8;
    c->pt    = 0;

    /* Immediately permute the initial state once (per spec). */
    init_perm: keccakf_asconp12(c->st.q);

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

    /* Finalizes the hash and squeezes the output bytes. */

    int i;

    /* Ascon padding: a single 0x01 byte at the current position. */
    c->st.b[c->pt] ^= 0x01;

    /* Final permutation after padding. */
    final_perm_0: keccakf_asconp12(c->st.q);

    /* Output 4x8 bytes (32 bytes total), with a permutation between
     * consecutive squeezes (sponge squeezing). */
    squeezing_1: for (i = 0; i < 8; i++) ((uint8_t *) md)[i     ] = c->st.b[i];
    final_perm_1: keccakf_asconp12(c->st.q);

    squeezing_2: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 8 ] = c->st.b[i];
    final_perm_2: keccakf_asconp12(c->st.q);

    squeezing_3: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 16] = c->st.b[i];
    final_perm_3: keccakf_asconp12(c->st.q);

    squeezing_4: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 24] = c->st.b[i];

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
