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

// [OPT] B-04: two control versions of the same core, selected at compile time
//       (Tcl flow: CTRL=lite | ib, see scripts/hls/run_point.tcl):
//
//   default (lite)  b03 interface: inlen / mdlen / mode and block control (ap_ctrl_hs) on
//                   AXI4-Lite, ap_return = status (0 = OK, 1 = TLAST did not match inlen)
//   HASH_INBAND (ib) no AXI4-Lite (suggested by the supervisors for the uniss_dma driver,
//                   which only drives the AXI DMA): ap_ctrl_none (free-running IP, like VPA),
//                   parameters in the FIRST 64-bit beat of `in` (header), status in ONE extra
//                   beat after the digest (TLAST moves from the last digest beat to it)
//
// Header beat (TDATA, little-endian in memory = bytes 0..7 of the MM2S buffer):
//   [31:0] inlen (message bytes) | [47:32] mdlen (digest bytes) | [55:48] mode | [63:56] 0
//   TLAST on the header only when no message beat follows (inlen = 0).
// Status beat (TDATA): 0 = OK, 1 = TLAST did not match inlen (same meaning as ap_return).
#define HDR_INLEN(h)  ((int)     (uint32_t)  ((h)        & 0xFFFFFFFFULL))
#define HDR_MDLEN(h)  ((int)     (uint32_t) (((h) >> 32) & 0xFFFFULL))
#define HDR_MODE(h)   ((uint8_t) (uint32_t) (((h) >> 48) & 0xFFULL))

#ifdef HASH_INBAND
#define HASH_DIGEST_TLAST 0     // ib: TLAST on the status beat
#else
#define HASH_DIGEST_TLAST 1     // lite: TLAST on the last digest beat (b03)
#endif

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
// [WAS] B-03: hash_absorb(c, in, inlen, mode)
// [OPT] B-04: + ended = the stream has no beat left to read when the absorb starts
//       (lite: empty message, no DMA transfer; ib: TLAST already seen on the header beat)
int  hash_absorb (hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen, uint8_t mode, bool ended); // 0 = OK, 1 = length mismatch
void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out, uint8_t mode);             // padding + digest beats

// Top-level IP
// [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode);
//       array ports (ap_memory) wrapping hash(); the 256-byte bound limited the message length
// [OPT] B-03: AXI4-Stream in/out (64-bit), AXI4-Lite inlen/mdlen/mode + control (ap_ctrl_hs);
//       returns 0 = OK, 1 = TLAST did not match inlen (read via AXI4-Lite ap_return)
// [OPT] B-04: HASH_INBAND -> streams only (header beat in, digest + status beat out)
#ifdef HASH_INBAND
void hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out);
#else
int  hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen, int mdlen, uint8_t mode);
#endif
 
#endif