#ifndef HASH_H
#define HASH_H

#include <stddef.h>
#include <stdint.h>

// [OPT] B-03: AXI4-Stream ports, same interface as Hybrid-Test (LOG-014).
//       hls::stream + ap_axiu (TDATA/TKEEP/TSTRB/TLAST) are C++ Vitis HLS types.
#include "hls_stream.h"
#include "ap_axi_sdata.h"

// One AXI4-Stream beat = one 64-bit word: data (TDATA), keep/strb (byte valid), last (TLAST).
// Byte order (as the AXI DMA packs memory): message byte 8*k + n in TDATA[8n+7 : 8n] of beat k,
// i.e. little-endian, the same lane order as the rate word st[0] (SP 800-232):
// with an 8-byte rate, one beat is exactly one Ascon block.
typedef ap_axiu<64, 0, 0, 0> axis64_t;

// [OPT] B-04: two control versions of the same core, selected at compile time
//       (Tcl flow: CTRL=lite | ib, see scripts/hls/run_point.tcl), as in Hybrid-Test:
//
//   default (lite)  b03 interface: inlen and block control (ap_ctrl_hs) on AXI4-Lite,
//                   ap_return = status (0 = OK, 1 = TLAST did not match inlen)
//   HASH_INBAND (ib) no AXI4-Lite (uniss_dma driver of the supervisors): ap_ctrl_none,
//                   parameters in the FIRST 64-bit beat of `in` (header), status in ONE extra
//                   beat after the digest (TLAST moves from the last digest beat to it)
//
// Header beat (TDATA, little-endian in memory = bytes 0..7 of the MM2S buffer), common to the
// three variants so that one driver serves all of them:
//   [31:0] inlen (message bytes) | [47:32] mdlen (digest bytes) | [55:48] mode | [63:56] 0
//   used here: inlen only (mdlen / mode ignored: fixed 32-byte Ascon-Hash256)
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

// Ascon-Hash256 digest length (fixed by SP 800-232)
#define ASCON_HASH_BYTES 32

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
              Two aliases of the same storage, sized for Keccak: Ascon uses only
              5 of the 25 words (20 words of dead storage). Because of the union
              HLS could not split the context: the whole struct (state + counters)
              was mapped into ONE memory (csynth storage report: H_i, 64-bit x 27
              words), so no ARRAY_PARTITION on the state.

        [OPT] B-01: word-only state of exactly the 5 words Ascon uses (320 bits).
              Unlike Hybrid/SHA-3, no generic byte addressing is needed: the rate
              is 8 bytes = st[0], so every byte access (absorb, padding, squeeze)
              is a lane select on st[0] (st0_get_byte() / st0_xor_byte() in hash.c)
              with a constant word index. B-01 adds no partitioning pragma: its
              effect is measured alone.
    */

    uint64_t st[5];                     // Ascon state: 5 x 64-bit words, rate = st[0]

    // [WAS] int pt, rsiz, mdlen;
    // [OPT] B-03: mdlen removed, Ascon-Hash256 always outputs 32 bytes (ASCON_HASH_BYTES);
    //       the field was set by hash_init but never read
    int pt, rsiz;		// these don't overflow

} hash_ctx_t;

// Compression function.
void keccakf_asconp12 (uint64_t st[5]);       // [WAS] uint64_t st[25]

// Sponge phases
// [WAS] OpenSSL-like pointer API: hash_init(c, mdlen), hash_update(c, const void *data, len),
//       hash_final(void *md, c) and the software wrapper hash(in, inlen, md, mdlen)
// [OPT] B-03: stream-only datapath, no pointer API left (same structure as Hybrid-Test)
int  hash_init   (hash_ctx_t *c);                                           // IV + initial permutation
// [WAS] B-03: hash_absorb(c, in, inlen)
// [OPT] B-04: + ended = the stream has no beat left to read when the absorb starts
//       (lite: empty message, no DMA transfer; ib: TLAST already seen on the header beat)
int  hash_absorb (hash_ctx_t *c, hls::stream<axis64_t> &in, int inlen, bool ended); // 0 = OK, 1 = length mismatch
void hash_squeeze(hash_ctx_t *c, hls::stream<axis64_t> &out);               // padding + 4 digest beats

// Top-level IP
// [WAS] void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen);
//       array ports (ap_memory) wrapping hash(); mdlen was not used by Ascon
// [OPT] B-03: AXI4-Stream in/out (64-bit), AXI4-Lite inlen + control (ap_ctrl_hs);
//       no mode / mdlen registers (standalone, fixed 32-byte digest);
//       returns 0 = OK, 1 = TLAST did not match inlen
// [OPT] B-04: HASH_INBAND -> streams only (header beat in, digest + status beat out)
#ifdef HASH_INBAND
void hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out);
#else
int  hash_top(hls::stream<axis64_t> &in, hls::stream<axis64_t> &out, int inlen);
#endif

#endif