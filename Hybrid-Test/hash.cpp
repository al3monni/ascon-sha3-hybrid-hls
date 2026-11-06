#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

// mode == 0 --> Ascon
// mode > 0  --> Sha

// ----- Byte view of the word-only state (B-01) -----

// [OPT] B-01: this helper replaces the byte alias c->st.b[] of the removed union.
//       Byte idx lives in word idx >> 3, lane idx & 7, bits [8*lane+7 : 8*lane]
//       (little-endian lanes: the same mapping the union had on LE hosts and in HLS).
//       The shift is always a multiple of 8, so in hardware it is a byte-lane select
//       (decoder + AND/OR), not a generic barrel shifter.
// [WAS] B-01: st_get_byte(st, idx), used by the byte-wise squeeze
// [OPT] B-03: removed, the squeeze emits whole 64-bit words (one AXI4-Stream beat each);
//       st_xor_byte() is still needed by the padding (byte position c->pt)

void st_xor_byte(uint64_t st[25], int idx, uint8_t v) {

    // leaf helper: inlined so the read-modify-write is merged into the calling loop

    #pragma HLS INLINE

    st[idx >> 3] ^= ((uint64_t) v) << (8 * (idx & 7));
}

void theta(uint64_t st[]) {

	// theta is a leaf function and inlining keeps exactly one copy inside that datapath with zero call overhead

    #pragma HLS INLINE

    int i, j;

    uint64_t t;     // t --> temporary variable for swaps or rotations.
    uint64_t bc[5]; // bc[] --> temporary array

	// partitioning bc array means diving it into 5x64b different registers so that read/write ports become free
		
    #pragma HLS ARRAY_PARTITION variable=bc type=complete dim=1

    /*
        [FIX comment] ARRAY_PARTITION does NOT unroll the consuming loops by itself:
        it only removes the 2-port limit, so that an UNROLL (or a PIPELINE on the
        parent loop) can actually perform the accesses in parallel.
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
        [FIX comment] ARRAY_PARTITION does NOT unroll the consuming loops by itself:
        it only removes the 2-port limit, so that an UNROLL (or a PIPELINE on the
        parent loop) can actually perform the accesses in parallel.
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
        [FIX comment] ARRAY_PARTITION does NOT unroll the consuming loops by itself:
        it only removes the 2-port limit, so that an UNROLL (or a PIPELINE on the
        parent loop) can actually perform the accesses in parallel.
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
        [FIX comment] ARRAY_PARTITION does NOT unroll the consuming loops by itself:
        it only removes the 2-port limit, so that an UNROLL (or a PIPELINE on the
        parent loop) can actually perform the accesses in parallel.
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

    // [EXPLORED] without inline off each call site (init/update/final) gets its own
    //            permutation copy: Ascon est. 4.7k -> 9.5k LUT (ASCON-SHA_PERFORMANCE, E9)
    // [EXPLORED] #pragma HLS ALLOCATION instances=keccakf_asconp12 limit=1 function
    //            not needed: post-route reports already show a single instance
    
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

        // [EXPLORED] #pragma HLS PIPELINE II=1 here (SHA-3 exploration E5 + rounds_loop):
        //            permutation 2641 -> 53 cycles, est. LUT 3.1k -> 8.9k (all steps unrolled).
        //            Rejected for the area-first target, but ~17x better throughput/area:
        //            to be re-evaluated after B-01 (union removal).
        // [EXPLORED] local partitioned copy of st (Ascon E1-E2): est. 28.1k LUT -> dropped.
		

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
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;
    uint64_t ASCON_HASH_IV= 0x0000080100cc0002; // Ascon
    
    init_loop: for (i = 0; i < 25; i++) {

        #pragma HLS PIPELINE II=1
		
		// II=1 IS feasible here because each iteration does one st write
		// with no loop-carried dependency and the 2-port RAM sustains it

        c->st[i] = 0;                       // [WAS] c->st.q[i] = 0;
    }
    
    // Ascon - Only Ascon initializes word 0 with the IV.
    if (!mode) c->st[0] = ASCON_HASH_IV;    // [WAS] c->st.q[0]
        
    // SHA-3 uses the “capacity” formula.
    // Ascon has a fixed 8-byte rate.
    
    c->mdlen = mdlen;
    c->rsiz = (200 - 2 * mdlen)* (!!mode) + 8 * (!mode);
    c->pt = 0;
    c->mode = mode;
 
    // Ascon - Immediately permute the initial state once (per spec).
    if (!mode) init_perm: keccakf_asconp12(c->st, mode);   // [WAS] c->st.q
    
    return 1;
}

int hash_absorb(hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen, uint8_t mode) {
    
    /*
        Absorbs the message, read from the AXI4-Stream `in`, into the state.

        [WAS] hash_update(c, const void *data, size_t len, mode): one byte per iteration,
              st_xor_byte() at byte position j, permutation when j reached rsiz
        [OPT] B-03: one 64-bit beat per iteration, XORed into word w of the block;
              permutation when w reaches the rate in words (rsiz / 8: 17, 9 or 1)

        Length: inlen (AXI4-Lite) says how many bytes are hashed, TLAST says where the stream
        ends. The loop always consumes the stream up to TLAST, so a wrong DMA length never
        stalls the IP nor leaks beats into the next message:
          - TLAST later than expected -> the extra beats are read and discarded
          - TLAST earlier than expected -> the loop stops at TLAST
        both cases return 1 (length mismatch, read by software from ap_return).
        inlen = 0 (empty message): no beat is read, software does not start the DMA.
    */

    // inlining hash_absorb into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE

    int nbeats = (inlen + 7) >> 3;      // beats carrying the message: ceil(inlen / 8)
    int rem    = inlen & 7;             // valid bytes in the last beat (0 = full beat)
    int rw     = c->rsiz >> 3;          // rate in words (rsiz is a multiple of 8)
    int w      = 0;                     // word index inside the current block
    int b      = 0;                     // beats read so far
    bool last  = (nbeats == 0);         // empty message: nothing to read

    absorbing_loop: while (!last) {

        // pipelining this loop results in a resource explosion because it calls the whole permutation fuction
        // [WAS] LOOP_TRIPCOUNT min=0 max=256 (bytes of the in[256] buffer)
        // [OPT] B-03: trip count in beats; 32 beats = 256 bytes keeps the reports comparable
        //       (the stream itself has no length bound any more)

        #pragma HLS LOOP_TRIPCOUNT min=0 max=32

        axis64_t beat = in.read();      // blocks until the DMA presents a beat (TVALID)
        last = beat.last;

        if (b < nbeats) {               // beats beyond inlen are only drained

            uint64_t d = (uint64_t) beat.data;

            if (b == nbeats - 1 && rem != 0) {

                // last, partial beat: only the first rem bytes belong to the message
                // (the mask is derived from inlen, TKEEP is not needed)

                d &= (((uint64_t) 1) << (8 * rem)) - 1;
                c->st[w] ^= d;
                c->pt = 8 * w + rem;    // padding byte right after the message

            } else {

                // full word: XOR into the state, permute when the block (rate) is full

                c->st[w] ^= d;
                w++;

                if (w == rw) {

                    absorb_perm: keccakf_asconp12(c->st, mode);
                    w = 0;
                }

                c->pt = 8 * w;          // padding at the start of the next word
            }
        }

        b++;
    }

    return (b == nbeats) ? 0 : 1;       // 1: TLAST did not match inlen
}

void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out, uint8_t mode) {
    
    /*
        Finalizes the hash and writes the digest to the AXI4-Stream `out`.

        [WAS] hash_final(void *md, c, mode): byte-wise output loops (st_get_byte)
        [OPT] B-03: one 64-bit state word per beat, TLAST on the last beat
              (the S2MM channel of the AXI DMA closes the transfer on TLAST)
    */

    // inlining hash_squeeze into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;
    axis64_t beat;

    // every field of the output beat is driven (TUSER/TID/TDEST have zero width)
    beat.data = 0; beat.keep = 0xFF; beat.strb = 0xFF; beat.last = 0;
    
    // --- Apply domain-separation padding for each algorithm ---
    
    if(mode) { //SHA-3 Branch
        
        // [WAS] c->st.b[c->pt] ^= 0x06;  c->st.b[c->rsiz - 1] ^= 0x80;
        // [OPT] B-01: same XORs through the byte helper; if pt == rsiz - 1 both hit
        //       the same byte (0x86), exactly as before

        st_xor_byte(c->st, c->pt,       0x06);
        st_xor_byte(c->st, c->rsiz - 1, 0x80);
    }
    // Ascon Branch
    else st_xor_byte(c->st, c->pt, 0x01);    // [WAS] c->st.b[c->pt] ^= 0x01;
    
    // Final permutation after padding.
    
    final_perm_0: keccakf_asconp12(c->st, mode);  // [WAS] c->st.q
    
    // --- Output ---
    
    if(mode) { //SHA-3 Branch
    
        // [WAS] output_loop over mdlen bytes (st_get_byte)
        // [OPT] B-03: digest = first mdlen bytes of the state = words 0 .. nout-1;
        //       nout = ceil(mdlen / 8): 4 beats (SHA3-256), 8 beats (SHA3-512);
        //       a partial last word (e.g. SHA3-224, 28 bytes) is marked with TKEEP

        int nout = (c->mdlen + 7) >> 3;
        int orem = c->mdlen & 7;
        
        output_loop: for (i = 0; i < nout; i++) {

			// II=1 is feasible here because each iteration does one state read and one stream write
			// mdlen is a runtime variable so the loop stays rolled, we only add the loop trip count

            #pragma HLS PIPELINE II=1
            #pragma HLS LOOP_TRIPCOUNT min=4 max=8

            beat.data = c->st[i];
            beat.keep = (i == nout - 1 && orem != 0) ? (ap_uint<8>) ((1 << orem) - 1) : (ap_uint<8>) 0xFF;
            beat.strb = beat.keep;
            beat.last = (i == nout - 1);
            out.write(beat);
        }

    } else { // Ascon Branch
    
        // Output 4 x 8 bytes (32 bytes total), applying the permutation between squeezes.
        // [WAS] squeezing_1..4: 8-iteration byte loops on st[0..7 bytes]
        // [OPT] B-03: each 8-byte block is exactly the rate word st[0] -> one beat;
        //       the three permutation call-sites are kept as before

        beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 1

        final_perm_1: keccakf_asconp12(c->st, mode);

        beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 2

        final_perm_2: keccakf_asconp12(c->st, mode);

        beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 3

        final_perm_3: keccakf_asconp12(c->st, mode);

        beat.data = c->st[0]; beat.last = 1; out.write(beat);      // squeeze 4, TLAST
    }
}

int hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen, int mdlen, uint8_t mode) {

    /*
        This is the top-level interface (the IP).

        [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode)
              wrapper with bounded arrays (ap_memory ports) around the software function hash(),
              added because hash() took void* pointers that cannot be synthesized as ports
        [OPT] B-03: hash_top IS the accelerator; it owns the context and calls the sponge phases.
              Data on AXI4-Stream (connected to an AXI DMA: MM2S -> in, out -> S2MM),
              scalars and block control (ap_start/ap_done/ap_return) on AXI4-Lite.

        Software sequence: write inlen, mdlen, mode -> ap_start -> DMA S2MM (digest) and
        MM2S (message, ceil(inlen/8) beats) -> wait ap_done -> ap_return: 0 = OK, 1 = length mismatch.
    */

    // [WAS] commented: mode=axis on the arrays gave wrong digests in cosim (array semantics)
    // [OPT] B-03: axis on hls::stream ports, s_axilite on the scalars and on return

    #pragma HLS INTERFACE mode=axis      port=in
    #pragma HLS INTERFACE mode=axis      port=out
    #pragma HLS INTERFACE mode=s_axilite port=inlen
    #pragma HLS INTERFACE mode=s_axilite port=mdlen
    #pragma HLS INTERFACE mode=s_axilite port=mode
    #pragma HLS INTERFACE mode=s_axilite port=return

    hash_ctx_t H;

    // [WAS] partitioning disabled because the union on st made the state non-partitionable
    // [OPT] B-01: the state is now a plain uint64_t st[25] and CAN be partitioned;
    //       left disabled on purpose so that B-01 (union removal) is measured alone.
    //       Candidates for the next step: type=cyclic factor=5 / type=complete.

    //#pragma HLS ARRAY_PARTITION variable=H.st dim=1 type=cyclic factor=5

    int err;

    hash_init   (&H, mdlen, mode);
    err = hash_absorb (&H, in, inlen, mode);
    hash_squeeze(&H, out, mode);

    return err;
}
