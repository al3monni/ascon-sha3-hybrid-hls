#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

// [OPT] B-03: AXI4-Stream ports. hls::stream + ap_axiu (TDATA/TKEEP/TSTRB/TLAST) are C++
//       Vitis HLS types: they are the reason the Hybrid sources moved to C++.
#include "hls_stream.h"
#include "ap_axi_sdata.h"

// One AXI4-Stream beat = one 64-bit state word: data (TDATA), keep/strb (byte valid), last (TLAST).
// Byte order (as the AXI DMA packs memory): message byte 8*k + n in TDATA[8n+7 : 8n] of beat k,
// i.e. little-endian, the same lane order as the state words (FIPS 202, SP 800-232).
typedef ap_axiu<64, 0, 0, 0> axis64_t;
 
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
              Two aliases of the same storage (byte view for absorb/pad/squeeze,
              word view for the permutation). Because of the union HLS could not
              split the context: the whole struct (state + counters) was mapped
              into ONE memory (csynth storage report: H_i, 64-bit x 27 words), so
              at most 2 state accesses per cycle and no ARRAY_PARTITION on the state.

        [OPT] B-01: word-only state. Byte accesses are rebuilt with shift/mask by
              st_get_byte() / st_xor_byte() in hash.c (little-endian lanes, as in
              FIPS 202 and SP 800-232, so the mapping is the same for both
              algorithms and independent of `mode`). The state is now an ordinary
              array that HLS can disaggregate from the counters and partition.
              B-01 adds no partitioning pragma: its effect is measured alone.
    */

    uint64_t st[25];                    // state: 25 x 64-bit words (Ascon uses st[0..4])

    int pt, rsiz, mdlen, mode;		// these don't overflow
	
} hash_ctx_t;

// Compression function.
void keccakf_asconp12 (uint64_t st[25], uint8_t mode);

// Sponge phases
// [WAS] OpenSSL-like pointer API: hash_update(c, const void *data, len), hash_final(void *md, c)
//       and the software wrapper hash(in, inlen, md, mdlen, mode)
// [OPT] B-03: stream-only datapath, no pointer API left (one code path, the hardware one)
int  hash_init   (hash_ctx_t *c, int mdlen, uint8_t mode);                              // mdlen = digest bytes
int  hash_absorb (hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen, uint8_t mode);   // 0 = OK, 1 = length mismatch
void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out, uint8_t mode);             // padding + digest beats

// Top-level IP
// [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode);
//       array ports (ap_memory) wrapping hash(); the 256-byte bound limited the message length
// [OPT] B-03: AXI4-Stream in/out (64-bit), AXI4-Lite inlen/mdlen/mode + control (ap_ctrl_hs);
//       returns 0 = OK, 1 = TLAST did not match inlen (read via AXI4-Lite ap_return)
int hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen, int mdlen, uint8_t mode);
 
#endif