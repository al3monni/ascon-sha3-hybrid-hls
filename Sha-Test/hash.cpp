#include "hash.h"
#include <stdio.h>
#include <inttypes.h>

// mode == 0 --> Ascon
// mode > 0  --> Sha

// ----- Byte view of the word-only state (B-01) -----

// [OPT] B-01: this helper replaces the byte alias c->st.b[] of the removed union
//       (identical to Hybrid-Test, LOG-013).
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
    
    top_chi: for (j = 0; j < 25 ; j += 5) { // chirounds=25 for sha3

        // pipelining the outer loop push the tool to flatten/unroll the two inner loops below and use more area
        // [EXPLORED] SHA-3 E2: PIPELINE on top_chi -> est. 5.0k LUT, BRAM 6, permutation 2641 cycles;
        //            inner loops only (current choice) -> est. 4.2k LUT, BRAM 18, 3865 cycles.
        //            Area-first target: fewer LUT kept, at the cost of latency and BRAM

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

    // ----- Iota or Diffusion layer -----
    
    // Injects round constant into word 0 (Iota)

    st[0] ^= keccakf_rndc[r]; // Iota
}

void keccakf_asconp12(uint64_t st[25]) {
    
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

    //-----------------------------Local variables and round counts-----------------------------

    int r;

    // (SHA): 24 rounds, 25-word Chi.

    //-----------------------------Rounds-----------------------------

    rounds_loop: for (r = 0; r < 24; r++) { // nrounds=24 for sha3

        // although nrounds is fixed, pipelining or unrolling this loop is the wrong choice
        // pipelining the outer loop push the tool to flatten/unroll all the inner loops 
		// since this function represents the whole permutation of both algorithms
		// this would mean and area explosion and a huge II
		// sequential execution of the rolled body is the area-optimal choice

        // [EXPLORED] #pragma HLS PIPELINE II=1 here (SHA-3 exploration E5 + rounds_loop):
        //            permutation 2641 -> 53 cycles, est. LUT 3.1k -> 8.9k (all steps unrolled).
        //            Rejected for the area-first target, but ~17x better throughput/area:
        //            to be re-evaluated after B-01 (union removal).
        // [EXPLORED] local partitioned copy of st (Ascon E1-E2): est. 28.1k LUT -> dropped.
        
        theta:  theta   (st);
        rho_pi: rho_pi  (st);
        chi:    chi     (st);
        iota:   iota    (st, r);

    } // end of rounds
}

int hash_init(hash_ctx_t *c, int mdlen) {
    
    /*
        Initializes a hash context for either SHA-3 or Ascon.
    */

    // inlining hash_init into the top module allows
    // all keccakf call-sites (init/absorb/squeeze) to live in one scope
    // so that the single-instance allocation cover the whole call tree
    // inlining is area-neutral because the function is called only one time

    #pragma HLS INLINE
    
    int i;

    init_loop: for (i = 0; i < 25; i++) {

        #pragma HLS PIPELINE II=1
		
		// II=1 IS feasible here because each iteration does one st write
		// with no loop-carried dependency and the 2-port RAM sustains it
        
        c->st[i] = 0;                       // [WAS] c->st.q[i] = 0;
    }
    
    // SHA-3 uses the “capacity” formula.

    c->mdlen = mdlen;
    c->rsiz = 200 - 2 * mdlen;
    c->pt = 0;

    return 1;
}

int hash_absorb(hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen) {
    
    /*
        Absorbs the message, read from the AXI4-Stream `in`, into the state.
        Same logic as Hybrid-Test (LOG-014), without the mode argument.

        [WAS] hash_update(c, const void *data, size_t len): one byte per iteration,
              st_xor_byte() at byte position j, permutation when j reached rsiz
        [OPT] B-03: one 64-bit beat per iteration, XORed into word w of the block;
              permutation when w reaches the rate in words (rsiz / 8: 17 or 9)

        Length: inlen (AXI4-Lite) says how many bytes are hashed, TLAST says where the stream
        ends. The loop always consumes the stream up to TLAST (no stall, no beat left for the
        next message); a mismatch returns 1. inlen = 0: no beat is read.
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

        #pragma HLS LOOP_TRIPCOUNT min=0 max=32

        axis64_t beat = in.read();      // blocks until the DMA presents a beat (TVALID)
        last = beat.last;

        if (b < nbeats) {               // beats beyond inlen are only drained

            uint64_t d = (uint64_t) beat.data;

            if (b == nbeats - 1 && rem != 0) {

                // last, partial beat: only the first rem bytes belong to the message

                d &= (((uint64_t) 1) << (8 * rem)) - 1;
                c->st[w] ^= d;
                c->pt = 8 * w + rem;    // padding byte right after the message

            } else {

                // full word: XOR into the state, permute when the block (rate) is full

                c->st[w] ^= d;
                w++;

                if (w == rw) {

                    absorb_perm: keccakf_asconp12(c->st);
                    w = 0;
                }

                c->pt = 8 * w;          // padding at the start of the next word
            }
        }

        b++;
    }

    return (b == nbeats) ? 0 : 1;       // 1: TLAST did not match inlen
}

void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out) {
    
    /*
        Finalizes the hash and writes the digest to the AXI4-Stream `out`.

        [WAS] hash_final(void *md, c): byte-wise output loop (st_get_byte)
        [OPT] B-03: one 64-bit state word per beat, TLAST on the last beat
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
    
    // --- Apply domain-separation padding ---
    
    // [WAS] c->st.b[c->pt] ^= 0x06;  c->st.b[c->rsiz - 1] ^= 0x80;
    // [OPT] B-01: same XORs through the byte helper; if pt == rsiz - 1 both hit
    //       the same byte (0x86), exactly as before

    st_xor_byte(c->st, c->pt,       0x06);
    st_xor_byte(c->st, c->rsiz - 1, 0x80);
    
    // Final permutation after padding.
    
    final_perm: keccakf_asconp12(c->st);  // [WAS] c->st.q
    
    // --- Output ---
    
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
}

int hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen, int mdlen) {

    /*
        This is the top-level interface (the IP), same structure as Hybrid-Test (LOG-014).

        [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen)
              wrapper with bounded arrays (ap_memory ports) around the software function hash()
        [OPT] B-03: hash_top IS the accelerator; it owns the context and calls the sponge phases.
              Data on AXI4-Stream (AXI DMA: MM2S -> in, out -> S2MM), inlen / mdlen and block
              control (ap_start/ap_done/ap_return) on AXI4-Lite. No mode register: standalone.
    */

    // [WAS] commented: mode=axis on the arrays gave wrong digests in cosim (array semantics)
    // [OPT] B-03: axis on hls::stream ports, s_axilite on the scalars and on return

    #pragma HLS INTERFACE mode=axis      port=in
    #pragma HLS INTERFACE mode=axis      port=out
    #pragma HLS INTERFACE mode=s_axilite port=inlen
    #pragma HLS INTERFACE mode=s_axilite port=mdlen
    #pragma HLS INTERFACE mode=s_axilite port=return

    hash_ctx_t H;

    // [WAS] partitioning disabled because the union on st made the state non-partitionable
    // [OPT] B-01: the state is now a plain uint64_t st[25] and CAN be partitioned;
    //       left disabled on purpose so that B-01 (union removal) is measured alone.
    //       Candidates for the next step: type=cyclic factor=5 / type=complete.

    //#pragma HLS ARRAY_PARTITION variable=H.st dim=1 type=cyclic factor=5

    int err;

    init_call:  hash_init   (&H, mdlen);
    abs_call:   err = hash_absorb (&H, in, inlen);
    sqz_call:   hash_squeeze(&H, out);

    return err;
}
