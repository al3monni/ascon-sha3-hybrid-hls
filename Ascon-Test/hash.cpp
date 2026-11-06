#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

// mode == 0 --> Ascon
// mode > 0  --> Sha

// ----- Byte view of the rate word st[0] (B-01) -----

// [OPT] B-01: this helper replaces the byte alias c->st.b[] of the removed union.
//       Ascon-Hash256 has an 8-byte rate = st[0]: every byte the sponge touches
//       (absorb j < 8, padding pt < 8, squeeze i < 8) is lane idx of st[0],
//       bits [8*idx+7 : 8*idx] (little-endian, as in NIST SP 800-232).
//       The word index is the constant 0: no word select at all, only a byte-lane
//       select (shift by a multiple of 8 = decoder + AND/OR, no barrel shifter).
//       Hybrid/SHA-3 need the generic word >> 3 addressing instead (LOG-013).
// [WAS] B-01: st0_get_byte(st, idx), used by the byte-wise squeeze
// [OPT] B-03: removed, the squeeze emits st[0] whole (one AXI4-Stream beat per block);
//       st0_xor_byte() is still needed by the padding (byte position c->pt)

void st0_xor_byte(uint64_t st[5], int idx, uint8_t v) {

    // leaf helper: inlined so the read-modify-write of st[0] is merged into the calling loop

    #pragma HLS INLINE

    st[0] ^= ((uint64_t) v) << (8 * (idx & 7));
}

void chi(uint64_t st[]) {

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
    
    top_chi: for (j = 0; j < 5 ; j += 5) { // chirounds=5 for ascon

        // pipelining the outer loop push the tool to flatten/unroll the two inner loops below and use more area
        // moreover, there is nothing to pipeline since the loop will execute exactly once

        // Applies the Chi transformation, which is the same nonlinear function used in both Keccak and Ascon.

        bc_cp_chi: for (i = 0; i < 5; i++){
            
            // II=1 IS feasible here because each iteration does one read and one write per iteration
			// with no loop-carried dependency and the 2-port RAM sustains it
			
            #pragma HLS PIPELINE II=1

            bc[i] = st[j + i];
        }
        
        chi_nlin: for (i = 0; i < 5; i++){

            // II=1 IS feasible here because each iteration does one read and one write per iteration
			// with no loop-carried dependency and the 2-port RAM sustains it
			
            #pragma HLS PIPELINE II=1
            // Nonlinear permutation part
            
            st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
    }
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

void keccakf_asconp12(uint64_t st[5]) {      // [WAS] uint64_t st[25]
    
    /*

        This is the heart of the design. It implements either:
            - the Keccak permutation or
            - Ascon permutation,
        depending on the mode.

        [WAS] The state st is always treated as 25×64-bit words (like Keccak).
              Ascon uses only 5 words; the rest are ignored when mode == 0.
        [OPT] B-01: the state is exactly the 5 Ascon words (st[0..4]).

    */

    #pragma HLS inline off
    
    // because it is reused multiple times and we want to save area
    // with inline off the tool will build one permutation datapath

    // [EXPLORED] without inline off each call site (init/update/final) gets its own
    //            permutation copy: Ascon est. 4.7k -> 9.5k LUT (ASCON-SHA_PERFORMANCE, E9)
    // [EXPLORED] #pragma HLS ALLOCATION instances=keccakf_asconp12 limit=1 function
    //            not needed: post-route reports already show a single instance
    
    //-----------------------------Constants-----------------------------
    
    //Ascon’s 12 round constants, applied to the middle word of the state.
    
    const uint8_t asconp_rndc[12] = {0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b};

    // ascon_rndc[12] is compile-time constant table
	// complete partition turns it into LUT constants so it can be read in the same cycle

    #pragma HLS ARRAY_PARTITION variable=asconp_rndc  type=complete dim=1
 
    //-----------------------------Local variables and round counts-----------------------------

    int i, j, r;

    // (Ascon): 12 rounds, 5-word Chi.

    //-----------------------------Rounds-----------------------------

    rounds_loop: for (r = 0; r < 12; r++) { // nrounds=12 for ascon

        // although nrounds is fixed, pipelining or unrolling this loop is the wrong choice
        // pipelining the outer loop push the tool to flatten/unroll all the inner loops 
		// since this function represents the whole permutation of both algorithms
		// this would mean and area explosion and a huge II
		// sequential execution of the rolled body is the area-optimal choice
        // [NOTE] Ascon exception: the round body works on 5 words only, so the pipeline
        //        below IS kept (Ascon NE0): it does not explode like the 25-word Keccak round

        // [EXPLORED] local partitioned copy of the state (stl[25] + ARRAY_PARTITION complete
        //            + unrolled copy loops, Ascon E1-E2): permutation in 7 cycles but est.
        //            28.1k LUT vs 4.7k -> dropped. B-01 (union removal) is the area-aware
        //            way to reach the same goal.

        #pragma HLS pipeline II=1

        // Add the round constant (asconp_rndc[r]) to word 2.
        // Then performs simple XOR mixing (a linear diffusion step).

        st[2] ^=  asconp_rndc[r]; 
		st[0] ^= st[4];
		st[4] ^= st[3];
		st[2] ^= st[1];

        // ----- Combined Chi (nonlinear layer) -----
        
        chi: chi(st);
        
        // ----- Post-Chi adjustments (Ascon only) -----

		// These linear tweaks transform Keccak’s χ (Chi) output into Ascon’s linearly equivalent S-box behavior.
		st[1] ^= st[0];
		st[0] ^= st[4];
		st[3] ^= st[2];
		st[2] = ~st[2];
        
        // ----- Diffusion layer -----
        
		// Applies its linear diffusion layer, a combination of bit rotations and XORs.
	
		diffusion: diffusion(st);
        
    } // end of rounds
}

int hash_init(hash_ctx_t *c) {
    
    /*
        Initializes a hash context for either SHA-3 or Ascon.
        [WAS] hash_init(c, mdlen)
        [OPT] B-03: no mdlen, Ascon-Hash256 has a fixed 32-byte digest
    */

    // inlining hash_init into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;
    uint64_t ASCON_HASH_IV= 0x0000080100cc0002; // Ascon

    // [WAS] for (i = 0; i < 25; i++): Ascon only uses st[0..4], zeroing all 25 words was wasteful
    // [OPT] B-01: the state has 5 words (absorbs the first item of backlog B-07)
    
    init_loop: for (i = 0; i < 5; i++) {
        
        #pragma HLS PIPELINE II=1
		
		// II=1 IS feasible here because each iteration does one st write
		// with no loop-carried dependency and the 2-port RAM sustains it

        c->st[i] = 0;                       // [WAS] c->st.q[i] = 0;
    }
    
	// Only Ascon initializes word 0 with the IV.
	
	c->st[0] = ASCON_HASH_IV;               // [WAS] c->st.q[0]

    // Ascon has a fixed 8-byte rate.
    
    // [WAS] c->mdlen = mdlen;   (never read)
    c->rsiz = 8;
    c->pt = 0;

	//Immediately permute the initial state once (per spec).
	
	init_perm: keccakf_asconp12(c->st);     // [WAS] c->st.q

    return 1;
}

int hash_absorb(hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen) {
    
    /*
        Absorbs the message, read from the AXI4-Stream `in`, into the state.
        Same stream logic as Hybrid-Test (LOG-014), specialized to the Ascon rate.

        [WAS] hash_update(c, const void *data, size_t len): one byte per iteration,
              st0_xor_byte() at lane j, permutation when j reached rsiz = 8
        [OPT] B-03: rate = 8 bytes = one beat: each full beat is XORed into st[0]
              and followed by the permutation; no word index is needed

        Length: inlen (AXI4-Lite) says how many bytes are hashed, TLAST says where the stream
        ends. The loop always consumes the stream up to TLAST (no stall, no beat left for the
        next message); a mismatch returns 1. inlen = 0: no beat is read.
    */

    // inlining hash_absorb into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE

    int nbeats = (inlen + 7) >> 3;      // beats = Ascon blocks carrying the message: ceil(inlen / 8)
    int rem    = inlen & 7;             // valid bytes in the last beat (0 = full beat)
    int b      = 0;                     // beats read so far
    bool last  = (nbeats == 0);         // empty message: nothing to read

    absorbing_loop: while (!last) {

        // pipelining this loop results in a resource explosion because it calls the whole permutation fuction
        // [WAS] LOOP_TRIPCOUNT min=0 max=256 (bytes of the in[256] buffer)
        // [OPT] B-03: trip count in beats; 32 beats = 256 bytes keeps the reports comparable

        #pragma HLS LOOP_TRIPCOUNT min=0 max=32

        axis64_t beat = in.read();      // blocks until the DMA presents a beat (TVALID)
        last = beat.last;

        if (b < nbeats) {               // beats beyond inlen are only drained

            uint64_t d = (uint64_t) beat.data;

            if (b == nbeats - 1 && rem != 0) {

                // last, partial block: only the first rem bytes belong to the message

                d &= (((uint64_t) 1) << (8 * rem)) - 1;
                c->st[0] ^= d;
                c->pt = rem;            // padding byte right after the message

            } else {

                // full block: XOR into the rate word and permute

                c->st[0] ^= d;

                absorb_perm: keccakf_asconp12(c->st);

                c->pt = 0;              // padding at the start of the next block
            }
        }

        b++;
    }

    return (b == nbeats) ? 0 : 1;       // 1: TLAST did not match inlen
}

void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out) {
    
    /*
        Finalizes the hash and writes the digest to the AXI4-Stream `out`.

        [WAS] hash_final(void *md, c): squeezing_1..4, 8-iteration byte loops (st0_get_byte)
        [OPT] B-03: each 8-byte digest block is the rate word st[0] -> one beat;
              4 beats (32 bytes), TLAST on the 4th; the three permutation call-sites are kept
    */

    // inlining hash_squeeze into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    axis64_t beat;

    // every field of the output beat is driven (TUSER/TID/TDEST have zero width)
    beat.data = 0; beat.keep = 0xFF; beat.strb = 0xFF; beat.last = 0;
    
    // --- Apply domain-separation padding ---

    st0_xor_byte(c->st, c->pt, 0x01);       // [WAS] c->st.b[c->pt] ^= 0x01;

    // Final permutation after padding.
    
    final_perm_0: keccakf_asconp12(c->st);  // [WAS] c->st.q
    
    // --- Output ---

	// Output 4 x 8 bytes (32 bytes total), applying the permutation between squeezes.

	// [EXPLORED] #pragma HLS UNROLL on squeezing_1..4 (Ascon E10-E12): hash_final becomes
	//            a separate module with a SECOND permutation copy, est. 4.7k -> 6.6k LUT
	// [WAS] squeezing_1..4: 8-iteration byte loops, PIPELINE II=1
	// [OPT] B-03: one beat per block, no loop left

    beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 1

	final_perm_1: keccakf_asconp12(c->st);  // [WAS] c->st.q

    beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 2

	final_perm_2: keccakf_asconp12(c->st);  // [WAS] c->st.q

    beat.data = c->st[0]; beat.last = 0; out.write(beat);      // squeeze 3

	final_perm_3: keccakf_asconp12(c->st);  // [WAS] c->st.q

    beat.data = c->st[0]; beat.last = 1; out.write(beat);      // squeeze 4, TLAST
}

int hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen) {

    /*
        This is the top-level interface (the IP), same structure as Hybrid-Test (LOG-014).

        [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen)
              wrapper with bounded arrays (ap_memory ports) around the software function hash()
        [OPT] B-03: hash_top IS the accelerator; it owns the context and calls the sponge phases.
              Data on AXI4-Stream (AXI DMA: MM2S -> in, out -> S2MM), inlen and block control
              (ap_start/ap_done/ap_return) on AXI4-Lite. No mode / mdlen: standalone, 32-byte digest.
    */

    // [WAS] commented: mode=axis on the arrays gave wrong digests in cosim (array semantics)
    // [OPT] B-03: axis on hls::stream ports, s_axilite on the scalar and on return

    #pragma HLS INTERFACE mode=axis      port=in
    #pragma HLS INTERFACE mode=axis      port=out
    #pragma HLS INTERFACE mode=s_axilite port=inlen
    #pragma HLS INTERFACE mode=s_axilite port=return

    hash_ctx_t H;

    // [WAS] partitioning disabled because the union on st made the state non-partitionable
    // [OPT] B-01: the state is now a plain uint64_t st[5] and CAN be partitioned;
    //       left disabled on purpose so that B-01 (union removal) is measured alone.
    //       Candidate for the next step: type=complete (5 x 64-bit registers).

    //#pragma HLS ARRAY_PARTITION variable=H.st dim=1 type=complete

    int err;

    init_call:  hash_init   (&H);
    abs_call:   err = hash_absorb (&H, in, inlen);
    sqz_call:   hash_squeeze(&H, out);

    return err;
}
