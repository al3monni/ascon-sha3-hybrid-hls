#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

/*
 * mode == 0 --> Ascon
 * mode  > 0 --> SHA-3
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

    /*
     * ----- Combined Chi (nonlinear layer) -----
     * Same nonlinear function used in both Keccak and Ascon, applied to
     * either 5 rows (Ascon) or 25 rows (SHA-3) depending on `chirounds`.
     */

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

void keccakf_asconp12(uint64_t st[25], uint8_t mode) {

    /*
     * Heart of the design. Implements either:
     *   - the Keccak permutation, or
     *   - the Ascon permutation,
     * depending on `mode`.
     *
     * The state st is always treated as 25 x 64-bit words (Keccak shape).
     * Ascon only uses the first 5 words; the rest are ignored when mode == 0.
     */

    /* Ascon's 12 round constants, applied to the middle word of the state. */
    const uint8_t asconp_rndc[12] = {
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5,
        0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b
    };

    int r, nrounds, chirounds;

    /*
     *  If mode == 0 (Ascon): 12 rounds,  5-word Chi.
     *  If mode  > 0 (SHA-3): 24 rounds, 25-word Chi.
     */
    nrounds   = 12 * (!mode) + (!!mode) * 24;
    chirounds =  5 * (!mode) + (!!mode) * 25;

    rounds_loop: for (r = 0; r < nrounds; r++) {

        if (mode > 0) {                         /* ----- SHA-3 branch ----- */
            theta_step: theta(st);
            rho_pi_step: rho_pi(st);
        } else {                                /* ----- Ascon branch ----- */
            /* Add round constant to word 2, then a simple XOR mixing. */
            st[2] ^= asconp_rndc[r];
            st[0] ^= st[4];
            st[4] ^= st[3];
            st[2] ^= st[1];
        }

        /* ----- Combined Chi (nonlinear layer). ----- */
        chi_step: chi(st, chirounds);

        /* ----- Post-Chi adjustments (Ascon only). -----
         * These linear tweaks transform Keccak's chi output into the
         * linearly equivalent Ascon S-box behaviour. */
        if (!mode) {
            st[1] ^= st[0];
            st[0] ^= st[4];
            st[3] ^= st[2];
            st[2] = ~st[2];
        }

        /* ----- Iota (SHA-3) or Diffusion (Ascon) layer. ----- */
        if (mode > 0) {
            iota_step: iota(st, r);
        } else {
            diffusion_step: diffusion(st);
        }
    }
}

int hash_init(hash_ctx_t *c, int mdlen, uint8_t mode) {

    /* Initializes a hash context for either SHA-3 or Ascon. */

    int i;
    uint64_t ASCON_HASH_IV = 0x0000080100cc0002;   /* Ascon IV (spec) */

    init_loop: for (i = 0; i < 25; i++) {
        c->st.q[i] = 0;
    }

    /* Only Ascon initializes word 0 with its IV. */
    if (!mode) c->st.q[0] = ASCON_HASH_IV;

    /* SHA-3 uses the "capacity" formula; Ascon has a fixed 8-byte rate. */
    c->mdlen = mdlen;
    c->rsiz  = (200 - 2 * mdlen) * (!!mode) + 8 * (!mode);
    c->pt    = 0;
    c->mode  = mode;

    /* Ascon: immediately permute the initial state once (per spec). */
    if (!mode) {
        init_perm: keccakf_asconp12(c->st.q, mode);
    }

    return 1;
}

int hash_update(hash_ctx_t *c, const void *data, size_t len, uint8_t mode) {

    /* Absorbs input data into the state. */

    size_t i;
    int j;

    j = c->pt;

    absorbing_loop: for (i = 0; i < len; i++) {
        /* XOR message bytes into the state one by one (absorb phase). */
        c->st.b[j++] ^= ((const uint8_t *) data)[i];

        /* When the block is full (rsiz), permute it. */
        if (j >= c->rsiz) {
            absorb_perm: keccakf_asconp12(c->st.q, mode);
            j = 0;
        }
    }

    c->pt = j;
    return 1;
}

int hash_final(void *md, hash_ctx_t *c, uint8_t mode) {

    /* Finalizes the hash and squeezes the output bytes. */

    int i;

    /* --- Apply domain-separation padding for each algorithm. --- */

    if (mode) {                                 /* SHA-3 branch */
        c->st.b[c->pt]       ^= 0x06;
        c->st.b[c->rsiz - 1] ^= 0x80;
    } else {                                    /* Ascon branch */
        c->st.b[c->pt] ^= 0x01;
    }

    /* Final permutation after padding. */
    final_perm_0: keccakf_asconp12(c->st.q, mode);

    /* --- Output. --- */

    if (mode) {                                 /* SHA-3 branch */
        /* Output mdlen bytes directly. */
        output_loop: for (i = 0; i < c->mdlen; i++) {
            ((uint8_t *) md)[i] = c->st.b[i];
        }
    } else {                                    /* Ascon branch */
        /* Output 4x8 bytes (32 bytes total), with a permutation between
         * consecutive squeezes (sponge squeezing). */
        squeezing_1: for (i = 0; i < 8; i++) ((uint8_t *) md)[i     ] = c->st.b[i];
        final_perm_1: keccakf_asconp12(c->st.q, mode);

        squeezing_2: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 8 ] = c->st.b[i];
        final_perm_2: keccakf_asconp12(c->st.q, mode);

        squeezing_3: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 16] = c->st.b[i];
        final_perm_3: keccakf_asconp12(c->st.q, mode);

        squeezing_4: for (i = 0; i < 8; i++) ((uint8_t *) md)[i + 24] = c->st.b[i];
    }

    return 1;
}

void hash(const void *in, size_t inlen, void *md, int mdlen, uint8_t mode) {

    /* One-shot wrapper: compute the hash of `inlen` bytes from `in`. */

    hash_ctx_t H;

    hash_init  (&H, mdlen, mode);
    hash_update(&H, in, inlen, mode);
    hash_final (md, &H, mode);
}

void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode) {

    /* Wrapper kept from the original HLS cosim top-level. */
    hash(in, inlen, md, mdlen, mode);
}
