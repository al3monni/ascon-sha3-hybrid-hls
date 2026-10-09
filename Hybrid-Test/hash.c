#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

// mode == 0 --> Ascon
// mode > 0  --> Sha

void theta(uint64_t st[]) {

	// theta is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead

    #pragma HLS INLINE

    int i, j;

    uint64_t t;     // t --> temporary variable for swaps or rotations.
    uint64_t bc[5]; // bc[] --> temporary array

	// partitioning bc array means diving it into 5x64b different registers so that read/write ports become free
		
    #pragma HLS ARRAY_PARTITION variable=bc type=complete dim=1

    /*
        When pragmas like ARRAY_PARTITION or ARRAY_RESHAPE are used,
        the HLS tool automatically unrolls any loops consuming this data by default,
        if this improve throughput
    */
    
    // ----- Theta step (mix columns) -----
    
    // Theta C-function

    theta_C: for (i = 0; i < 5; i++) {
        
        #pragma HLS PIPELINE
		
		// we dont specify II=1 because it is not achievable here
		// each iteration issues 5 reads of `st`
		// and the 2-port RAM serves at most 2/cycle
		// keeping the loop ROLLED saves area
		
        // Compute column parities — each column is XORed together.
        
        bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
    }

    // Theta D-function

    theta_D: for (i = 0; i < 5; i++) {

        // #pragma HLS PIPELINE II=1
		
		// pipelining the outer loop push the tool to flatten/unroll the inner loop below and use more area
        
        // Diffuse parity bits into the state — this is the Theta linear layer.
        
        t = bc[(i + 4) % 5] ^ ROTL64(bc[(i + 1) % 5], 1);
        
        theta_D_loop: for (j = 0; j < 25; j += 5) {

			// II=1 IS feasible here because each iteration does one read and one write with no loop-carried dependency
			
            #pragma HLS PIPELINE II=1

            st[j + i] ^= t;
        }
    }
}

void rho_pi(uint64_t st[]) {

	// rho_pi is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead
  
    #pragma HLS INLINE

    // Constants used during Rho (rotation) and Pi (permutation) steps.
    
    const int keccakf_rotc[24] = {
        1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
        27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
    };
    
    const int keccakf_piln[24] = {
        10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
        15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
    };

    // keccakf_rotc[24] and keccakf_piln[24] are compile-time constant tables
	// complete partition turns them into LUT constants so they can be read in the same cycle

    #pragma HLS ARRAY_PARTITION variable=keccakf_rotc type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=keccakf_piln type=complete dim=1

    // Local variables

    int i, j;

    uint64_t t;     // t    --> temporary variable for swaps or rotations.
    uint64_t bc[5]; // bc[] --> temporary array


	// partitioning bc array means diving it into 5x64b different registers so that read/write ports become free

    #pragma HLS ARRAY_PARTITION variable=bc type=complete dim=1

    /*
        When pragmas like ARRAY_PARTITION or ARRAY_RESHAPE are used,
        the HLS tool automatically unrolls any loops consuming this data by default.
        if this improve throughput   
    */

    // ----- Rho and Pi steps -----
        
    t = st[1];
    
    rho_pi_loop: for (i = 0; i < 24; i++) {

		// we dont specify II=1 because it is not achievable here
		// the II is bounded by a loop-carried dependency on t
		// and the 2-port RAM serves at most 2/cycle

        #pragma HLS PIPELINE

        
        // Rotates and rearranges bits in the state (Rho + Pi).
        
        j = keccakf_piln[i];
        bc[0] = st[j];
        st[j] = ROTL64(t, keccakf_rotc[i]);
        t = bc[0];
    }
}

void chi(uint64_t st[], int chirounds) {

    // chi is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead

    #pragma HLS INLINE

    int i, j;
    
    uint64_t bc[5]; // bc[] --> temporary array for column parity or intermediate Chi copies.

    // partitioning bc array means diving it into 5x64b different registers so that read/write ports become free
	
    #pragma HLS ARRAY_PARTITION variable=bc type=complete dim=1
 
    /*
        When pragmas like ARRAY_PARTITION or ARRAY_RESHAPE are used,
        the HLS tool automatically unrolls any loops consuming this data by default.
        if this improve throughput   
    */

    // ----- Combined Chi (nonlinear layer) -----
    
    top_chi: for (j = 0; j < chirounds ; j += 5) {

        // chirounds is a runtime variable and the loop cannot be unrolled
		// pipelining the outer loop push the tool to flatten/unroll the two inner loops below and use more area
		// the only reasonable pragma is to specify the trip count 
		
        #pragma HLS LOOP_TRIPCOUNT min=1 max=5

        // Applies the Chi transformation, which is the same nonlinear function used in both Keccak and Ascon.

        bc_cp_chi: for (i = 0; i < 5; i++) {
            
            // II=1 IS feasible here because each iteration does one read and one write per iteration
			// with no loop-carried dependency and the 2-port RAM sustains it
			
            #pragma HLS PIPELINE II=1

            bc[i] = st[j + i];
        }
        
        chi_nlin: for (i = 0; i < 5; i++) {

			// II=1 IS feasible here because each iteration does one read and one write per iteration
			// with no loop-carried dependency and the 2-port RAM sustains it
			
            #pragma HLS PIPELINE II=1

            // Nonlinear permutation part
            
            st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
    }
}

void iota(uint64_t st[], int r) {

    // iota is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead

    #pragma HLS INLINE

    // 24 Keccak round constants used during the Iota step. These are XORed into A[0,0] each round.
    
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

	// keccakf_rndc[24] is a compile-time constant tables and
	// complete partition turns it into LUT constants
	
    #pragma HLS ARRAY_PARTITION variable=keccakf_rndc type=complete dim=1

    // Injects round constant into word 0 (Iota)

    st[0] ^= keccakf_rndc[r];

}

void diffusion(uint64_t st[]) {

	// diffusion is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead

    #pragma HLS INLINE

    int i, j;

    uint64_t bc[5];     // bc[] --> temporary array for column parity or intermediate Chi copies.

	// partitioning bc array means diving it into 5x64b different registers so that read/write ports become free
	// and the linear layer collapses to pure XOR/rotate combinational logic.
	
    #pragma HLS ARRAY_PARTITION variable=bc type=complete dim=1
	
	/*
        When pragmas like ARRAY_PARTITION or ARRAY_RESHAPE are used,
        the HLS tool automatically unrolls any loops consuming this data by default.
        if this improve throughput   
    */

    // Applies its linear diffusion layer, a combination of bit rotations and XORs.
	
    bc_cp_diff: for (i=0; i<5; i++){

		// II=1 is feasible here because each iteration does one read and one write per iteration
		// with no loop-carried dependency and the 2-port RAM sustains it
		
		#pragma HLS PIPELINE II=1

        bc[i]=st[i]; //create the intermediate copy of st
    }

    //linear operations on intermediate copies

    st[0] = bc[0] ^ ROTR64(bc[0], 19)   ^ ROTR64(bc[0], 28);
    st[1] = bc[1] ^ ROTR64(bc[1], 61)   ^ ROTR64(bc[1], 39);
    st[2] = bc[2] ^ ROTR64(bc[2], 1)    ^ ROTR64(bc[2], 6);
    st[3] = bc[3] ^ ROTR64(bc[3], 10)   ^ ROTR64(bc[3], 17);
    st[4] = bc[4] ^ ROTR64(bc[4], 7)    ^ ROTR64(bc[4], 41);

    // No reasonable pragma can be applied here
}

void keccakf_asconp12(uint64_t st[25], uint8_t mode) {
    
    /*

        This is the heart of the design. It implements either:
            - the Keccak permutation or
            - Ascon permutation,
        depending on the mode.

        The state st is always treated as 25×64-bit words (like Keccak).
        Ascon uses only 5 words; the rest are ignored when mode == 0.

    */

    #pragma HLS inline off
    
    // because it is reused multiple times and we want to save area
    // with inline off the tool will build one permutation datapath
    
    //-----------------------------Constants-----------------------------
    
    // 24 Keccak round constants used during the Iota step. These are XORed into A[0,0] each round.
    
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
    
    //Ascon’s 12 round constants, applied to the middle word of the state.
    
    const uint8_t asconp_rndc[12] = {0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b};
 
    // keccakf_rndc[24] and ascon_rndc[24] are compile-time constant tables
	// complete partition turns them into LUT constants so they can be read in the same cycle

    #pragma HLS ARRAY_PARTITION variable=keccakf_rndc type=complete dim=1
    #pragma HLS ARRAY_PARTITION variable=asconp_rndc  type=complete dim=1


    //-----------------------------Local variables and round counts-----------------------------

    // bc[] --> temporary array for column parity or intermediate Chi copies.
    // t    --> temporary variable for swaps or rotations.

    int i, j, r, nrounds, chirounds;
    
    //uint64_t t, bc[5];   // re-defined into each function module EDIT

    // If mode = 0 (Ascon): 12 rounds, 5-word Chi.
    // If mode > 0 (SHA): 24 rounds, 25-word Chi.

    nrounds = 12 * (!mode) + (!!mode) * 24;     
    chirounds =  5 * (!mode) + (!!mode) * 25;      
 
    //-----------------------------Rounds-----------------------------
    
    // Each round applies different transformations depending on the mode.

    rounds_loop: for (r = 0; r < nrounds; r++) {

        // nrounds is a runtime variable and the loop cannot be unrolled
		// pipelining the outer loop push the tool to flatten/unroll all the inner loops 
		// since this function represents the whole permutation of both algorithms
		// this would mean and area explosion and a huge II
		// sequential execution of the rolled body is the area-optimal choice
		

        #pragma HLS LOOP_TRIPCOUNT min=12 max=24
		
		// the only reasonable pragma is to specify the trip count 

        if (mode > 0) { // SHA-3 Branch

            theta:  theta   (st);
            rho_pi: rho_pi  (st);

        } else { // Ascon branch

            // Add the round constant (asconp_rndc[r]) to word 2.
            // Then performs simple XOR mixing (a linear diffusion step).

            st[2] ^=  asconp_rndc[r]; 
            st[0] ^= st[4];
            st[4] ^= st[3];
            st[2] ^= st[1];
        }

        // ----- Combined Chi (nonlinear layer) -----

        chi: chi(st, chirounds);

        // ----- Post-Chi adjustments (Ascon only) -----

        // These linear tweaks transform Keccak’s χ (Chi) output into Ascon’s linearly equivalent S-box behavior.

        if (!mode) {
            st[1] ^= st[0];
            st[0] ^= st[4];
            st[3] ^= st[2];
            st[2] = ~st[2];
        }

        // ----- Iota or Diffusion layer -----
        
        // SHA-3 Branch
        if (mode > 0) iota: iota(st, r);
        // Ascon Branch
        else diffusion: diffusion(st);

    } // end of rounds  
}

int hash_init(hash_ctx_t *c, int mdlen, uint8_t mode) {
    
    /*
        Initializes a hash context for either SHA-3 or Ascon.
    */

    // inlining hash_init into the top module allows
    // all keccakf call-sites (init/update/final) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;
    uint64_t ASCON_HASH_IV= 0x0000080100cc0002; // Ascon
    
    init_loop: for (i = 0; i < 25; i++) {

        #pragma HLS PIPELINE II=1
		
		// II=1 IS feasible here because each iteration does one st write
		// with no loop-carried dependency and the 2-port RAM sustains it

        c->st.q[i] = 0;
    }
    
    // Ascon - Only Ascon initializes word 0 with the IV.
    if (!mode) c->st.q[0] = ASCON_HASH_IV;
        
    // SHA-3 uses the “capacity” formula.
    // Ascon has a fixed 8-byte rate.
    
    c->mdlen = mdlen;
    c->rsiz = (200 - 2 * mdlen)* (!!mode) + 8 * (!mode);
    c->pt = 0;
    c->mode = mode;
 
    // Ascon - Immediately permute the initial state once (per spec).
    if (!mode) init_perm: keccakf_asconp12(c->st.q, mode);
    
    return 1;
}

int hash_update(hash_ctx_t *c, const void *data, size_t len, uint8_t mode) {
    
    /*
        Absorbs input data into the state.  
    */

    // inlining hash_update into the top module allows
    // all keccakf call-sites (init/update/final) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    size_t i;
    int j;

    j = c->pt;
    
    absorbing_loop: for (i = 0; i < len; i++) {

        // pipelining this loop results in a resource explosion because it calls the whole permutation fuction
        // the only reasonable pragma is to specify the trip count
        // the upper bound is 256 because it is the size of the input buffer that determines it
		
        #pragma HLS LOOP_TRIPCOUNT min=0 max=256
				

        // XORs message bytes into the state one by one (absorb phase).
        
        c->st.b[j++] ^= ((const uint8_t *) data)[i];
        
        // When the block is full (rsiz), permute it.
        
        if (j >= c->rsiz) {
            
            absorb_perm: keccakf_asconp12(c->st.q, mode);
            j = 0;
        }
    }
    
    c->pt = j;

    return 1;
}

int hash_final(void *md, hash_ctx_t *c, uint8_t mode) {
    
    /*
        Finalizes the hash and squeezes output bytes.
    */

    // inlining hash_final into the top module allows
    // all keccakf call-sites (init/update/final) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;
    
    // --- Apply domain-separation padding for each algorithm ---
    
    if(mode) { //SHA-3 Branch
        
        c->st.b[c->pt]          ^= 0x06;
        c->st.b[c->rsiz - 1]    ^= 0x80;
    }
    // Ascon Branch
    else c->st.b[c->pt] ^= 0x01;
    
    // Final permutation after padding.
    
    final_perm_0: keccakf_asconp12(c->st.q,mode);
    
    // --- Output ---
    
    if(mode) { //SHA-3 Branch
    
        // Output mdlen bytes directly.
        
        output_loop: for (i = 0; i < c->mdlen; i++) {

			// II=1 is feasible here because each iteration does one read and one write
			// mdlen is a runtime variable (32  SHA3-256 and 64 for SHA3-512) so the loop stays rolled
			// we only add the loop trip count

            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=32 max=64
            
            ((uint8_t *) md)[i] = c->st.b[i];
        }

    } else { // Ascon Branch
    
        // Output 4×8 bytes blocks (32 bytes total), applying permutation between squeezes (like sponge squeezing).

       squeezing_1: for (i = 0; i < 8; i++) {

            // II=1 is feasible here because each iteration does one read and one write

            #pragma HLS pipeline II=1

            ((uint8_t *) md)[i] = c->st.b[i];
        }

        final_perm_1: keccakf_asconp12(c->st.q, mode);
        
        squeezing_2: for (i = 0; i < 8; i++) {

            // II=1 is feasible here because each iteration does one read and one write

            #pragma HLS pipeline II=1

            ((uint8_t *) md)[i+8] = c->st.b[i];
        }

        final_perm_2: keccakf_asconp12(c->st.q, mode);
        
        squeezing_3: for (i = 0; i < 8; i++) {

            // II=1 is feasible here because each iteration does one read and one write

            #pragma HLS pipeline II=1

            ((uint8_t *) md)[i+16] = c->st.b[i];
        }

        final_perm_3: keccakf_asconp12(c->st.q, mode);
        
        squeezing_4: for (i = 0; i < 8; i++)  {

            // II=1 is feasible here because each iteration does one read and one write

            #pragma HLS pipeline II=1

            ((uint8_t *) md)[i+24] = c->st.b[i];
        }
    }

    return 1;
}

void hash(const void *in, size_t inlen, void *md, int mdlen, uint8_t mode){
    
    /*
        Function wrapper, compute the hash of given byte length from "in"
    */

    // inlining hash into hash_top allw the whole body (the state RAM + the single permutation instance + all phases)
    // lives in the same top scope where the interface/allocation directives apply

    #pragma HLS INLINE
    
    hash_ctx_t H;

    // These partitioning must stay disable because the union on st represents a constraint for partitioning, see hash.h
    
    //#pragma HLS ARRAY_PARTITION variable=H.st.q dim=1 type=cyclic factor=5
    //#pragma HLS ARRAY_PARTITION variable=H.st.b dim=1 type=cyclic factor=5

    hash_init   (&H, mdlen, mode);
    hash_update (&H, in, inlen, mode);
    hash_final  (md, &H, mode);

    //return md;
}

void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode) {

    /*
        This is the top-level interface
    */

    // This wrapper is added because hash() takes void* pointers which cannot be synthesized as top-level ports.
    // HLS needs a concrete element type and a bounded size to build an interface.
    // It exposes typed, size-bounded ports (in[256], md[64]) and forwards to the unchanged hash() core

    /*
    #pragma HLS INTERFACE mode=axis      port=in
    #pragma HLS INTERFACE mode=axis      port=md
    #pragma HLS INTERFACE mode=s_axilite port=inlen
    #pragma HLS INTERFACE mode=s_axilite port=mdlen
    #pragma HLS INTERFACE mode=s_axilite port=mode
    #pragma HLS INTERFACE mode=s_axilite port=return
    */

    // For data ports (in, md) the interface AXI4-Stream allows sequential byte flows, accessed once in order.
    // For control scalars (inlen, mdlen, mode) we use AXI4-Lite, a scalar on a stream would be an anti-pattern.

    // see hls::stream with TLAST for a fully robust stream design

    hash(in, inlen, md, mdlen, mode);
}