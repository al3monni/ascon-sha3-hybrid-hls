#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hash.h"

// read a hex string, return byte length or -1 on error.

static int test_hexdigit(char ch){
    if (ch >= '0' && ch <= '9')
        return  ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return  ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return  ch - 'a' + 10;
    return -1;
}

static int test_readhex(uint8_t *buf, const char *str, int maxbytes){
    int i, h, l;

    for (i = 0; i < maxbytes; i++) {
		
        h = test_hexdigit(str[2 * i]);
		
        if (h < 0)
            return i;
		
        l = test_hexdigit(str[2 * i + 1]);
		
        if (l < 0)
            return i;
		
        buf[i] = (h << 4) + l;
    }

    return i;
}

// ---------------------------------------------------------------------------
// [OPT] B-03: AXI4-Stream helpers (the testbench plays the role of the AXI DMA),
//       same as Hybrid-Test; hash_top has neither mode nor mdlen
// ---------------------------------------------------------------------------

// Message bytes -> beats, packed as the AXI DMA MM2S channel does: byte 8k+n in
// TDATA[8n+7:8n] of beat k, TKEEP marks the valid bytes of the last beat, TLAST on the last beat.
// beats = ceil(len/8) for a correct transfer; fewer / more beats emulate a wrong DMA length
// (beats beyond the message carry a filler pattern with TKEEP = 0xFF).

static void tb_send(hls::stream<axis64_t> &in, const uint8_t *msg, int len, int beats){

    int k, n, nb;

    for (k = 0; k < beats; k++) {

        axis64_t beat;
        uint64_t d = 0;

        for (n = 0, nb = 0; n < 8; n++)
            if (8 * k + n < len) { d |= ((uint64_t) msg[8 * k + n]) << (8 * n); nb++; }

        if (nb == 0) d = 0xA5A5A5A5A5A5A5A5ULL;            // filler beat (wrong DMA length)

        beat.data = d;
        beat.keep = (nb == 0 || nb == 8) ? 0xFF : ((1 << nb) - 1);
        beat.strb = beat.keep;
        beat.last = (k == beats - 1);
        in.write(beat);
    }
}

// Digest beats -> bytes (what the S2MM channel writes to memory). Checks the stream protocol:
// exactly ceil(mdlen/8) beats, TLAST only on the last one, TKEEP consistent with mdlen.
// Returns the number of protocol errors.

static int tb_recv(hls::stream<axis64_t> &out, uint8_t *md, int mdlen){

    int k, n, errs = 0, nout = (mdlen + 7) >> 3;

    for (k = 0; k < nout; k++) {

        if (out.empty()) { fprintf(stderr, "  digest: only %d of %d beats\n", k, nout); return errs + 1; }

        axis64_t beat = out.read();
        uint64_t d = (uint64_t) beat.data;
        int nb = (k == nout - 1 && (mdlen & 7)) ? (mdlen & 7) : 8;

        for (n = 0; n < nb; n++) md[8 * k + n] = (uint8_t) (d >> (8 * n));

        if ((int) beat.last != (k == nout - 1))             { fprintf(stderr, "  digest beat %d: TLAST = %d\n", k, (int) beat.last); errs++; }
        if ((int) beat.keep != (nb == 8 ? 0xFF : (1 << nb) - 1)) { fprintf(stderr, "  digest beat %d: TKEEP = 0x%02X\n", k, (int) beat.keep); errs++; }
    }

    if (!out.empty()) { fprintf(stderr, "  digest: extra beats after TLAST\n"); errs++; }

    return errs;
}

// One transaction of the IP. beats < 0 -> correct transfer (ceil(len/8) beats).
// *ret = hash_top return value (0 = OK, 1 = length mismatch). Returns the protocol errors,
// including input beats left in the stream (the IP must always consume up to TLAST).

static int tb_hash(const uint8_t *msg, int len, uint8_t *md, int mdlen, int beats, int *ret){

    hls::stream<axis64_t> in, out;
    int errs;

    tb_send(in, msg, len, beats < 0 ? (len + 7) >> 3 : beats);
    *ret = hash_top(in, out, len);          // mdlen only checks the digest beats (32 bytes)
    errs = tb_recv(out, md, mdlen);

    if (!in.empty()) { fprintf(stderr, "  message: input beats left after TLAST\n"); errs++; }

    return errs;
}

// returns zero on success, nonzero + stderr messages on failure
// [WAS] use_top selected hash_top (RTL in cosim) or the C function hash()
// [OPT] B-03: hash() no longer exists; every KAT is one transaction of the IP through the streams

int test_hash(void){
	
    // message / digest pairs, lifted from ShortMsgKAT_SHA3-xxx.txt files
    // in the official package: https://github.com/gvanas/KeccakCodePackage

    const char *testvec[][2] = {
       
		{	//ascon 256
			"000102030405060708090A0B0C0D0E0F10111213141516",
			"B4F88D121EDDF6D1FEA9AEF15F68A0F3A16D3D2CDD9817225809C20452B04C61"
		},
       
		{	//ascon 256
			"",
			"0B3BE5850F2F6B98CAF29F8FDEA89B64A1FA70AA249B8F839BD53BAA304D92B2"
		}
    };
	
    int i, fails, msg_len, hash_len, ret;
    uint8_t digest[64], buf[64], msg[256];

    fails = 0;
    for (i = 0; i < 2; i++) {

        memset(digest, 0, sizeof(digest));
        memset(buf, 0, sizeof(buf));
        memset(msg, 0, sizeof(msg));

        msg_len = test_readhex(msg, testvec[i][0], sizeof(msg));
        hash_len = test_readhex(digest, testvec[i][1], sizeof(digest));

        // [WAS] hash_top(msg, msg_len, buf, hash_len) with arrays, or hash() in pure C
        // [OPT] B-03: stream transaction; the protocol and the return value are checked too
        //       (the empty-message KAT is inlen = 0: no beat is sent, as with the DMA)

        if (tb_hash(msg, msg_len, buf, hash_len, -1, &ret) != 0 || ret != 0) {

            fprintf(stderr, "[%d] HASH-%d, len %d: stream protocol error or ap_return = %d.\n", i, hash_len * 8, msg_len, ret);
            fails++;
        }
		
        if (memcmp(digest, buf, hash_len) != 0) {
			
            fprintf(stderr, "[%d] HASH-%d, len %d test FAILED.\n", i, hash_len * 8, msg_len);
            fails++;
        }
    }

    return fails;
}

// ---------------------------------------------------------------------------
// [OPT] B-03: robustness to a wrong DMA length (TLAST != inlen), same tests as Hybrid-Test
// The IP must consume the stream exactly up to TLAST (no stall, no beat left for the next
// message) and return 1. Run by default in csim; in cosim only with -argv robust.
// ---------------------------------------------------------------------------

static int test_robust(void){

    uint8_t msg[256], md[64];
    int i, ret, fails = 0;

    for (i = 0; i < 256; i++) msg[i] = (uint8_t)(i * 167u + 13u);

    // Ascon-Hash256, 30-byte message = 4 beats (last one partial)
    const int len = 30, mdlen = ASCON_HASH_BYTES;

    // TLAST one beat early: the IP stops at TLAST and reports the mismatch
    if (tb_hash(msg, len, md, mdlen, 3, &ret) != 0 || ret != 1) { fprintf(stderr, "robust: early TLAST, ap_return = %d\n", ret); fails++; }

    // TLAST two beats late: the extra beats are drained and the mismatch reported
    if (tb_hash(msg, len, md, mdlen, 6, &ret) != 0 || ret != 1) { fprintf(stderr, "robust: late TLAST, ap_return = %d\n", ret); fails++; }

    // correct length right after: no state leaks from the previous transactions
    if (tb_hash(msg, len, md, mdlen, -1, &ret) != 0 || ret != 0) { fprintf(stderr, "robust: exact length, ap_return = %d\n", ret); fails++; }

    if (fails == 0) printf("Robustness tests OK (early / late TLAST detected, stream drained).\n");

    return fails;
}

// ---------------------------------------------------------------------------
// [OPT] Throughput mode (cosim_design -argv <algo>)
// One algorithm per cosim run, exactly two RTL transactions: L(len0), L(len1).
// cosim report: min = L(len0), max = L(len1), avg = check (L0+L1)/2.
// len1 - len0 = k * rate  ->  c = (lat_max - lat_min) / k  [cycles per block].
// [WAS] reference digest from the C function hash()
// [OPT] B-03: reference digests from tests/equivalence/golden.txt (pyascon),
//       same message: msg[i] = (i*167 + 13) mod 256.
// Same argv names as Hybrid-Test (no mode: standalone variant).
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;   // -argv value
    int mdlen;          // digest bytes
    int rate;           // r [bytes]
    int len0, len1;     // message lengths, len1 - len0 = k * rate, len1 <= 256
    const char *d0, *d1;    // [OPT] B-03: expected digests of L(len0), L(len1) (golden.txt)
} tp_cfg_t;

static const tp_cfg_t TP_CFG[] = {
    { "ascon",     32,   8,   0, 256,    // k = 32
      "0B3BE5850F2F6B98CAF29F8FDEA89B64A1FA70AA249B8F839BD53BAA304D92B2",
      "5D2C237C7E5830EA1CFE4E9AB186C524AD598991291716BEF5CD6AA7A920510C" },
};
#define TP_NCFG ((int)(sizeof(TP_CFG) / sizeof(TP_CFG[0])))

static int test_throughput(const char *algo){

    const tp_cfg_t *c = NULL;
    int i, j, ret, fails = 0;
    uint8_t msg[256], ref[64], out[64];

    for (i = 0; i < TP_NCFG; i++)
        if (strcmp(TP_CFG[i].name, algo) == 0) c = &TP_CFG[i];

    if (c == NULL) {
        fprintf(stderr, "Unknown algo '%s'. Valid:", algo);
        for (i = 0; i < TP_NCFG; i++) fprintf(stderr, " %s", TP_CFG[i].name);
        fprintf(stderr, "\n");
        return 1;
    }

    // [WAS] 1) the C reference hash() had to pass the KAT first (pure C, no RTL transaction)
    // [OPT] B-03: no C reference any more, the expected digests are constants (golden.txt)

    // deterministic message (latency is data-independent; content only feeds the digest check)
    for (i = 0; i < 256; i++) msg[i] = (uint8_t)(i * 167u + 13u);

    const int lens[2] = { c->len0, c->len1 };
    const char *refs[2] = { c->d0, c->d1 };
    for (j = 0; j < 2; j++) {
        memset(ref, 0, sizeof(ref));
        memset(out, 0, sizeof(out));
        test_readhex(ref, refs[j], c->mdlen);                                   // expected digest
        if (tb_hash(msg, lens[j], out, c->mdlen, -1, &ret) != 0 || ret != 0) fails++;   // 1 RTL transaction
        if (memcmp(ref, out, c->mdlen) != 0) {
            fprintf(stderr, "[%s] len %d: digest != golden FAILED.\n", c->name, lens[j]);
            fails++;
        }
    }

    printf("TB_THROUGHPUT algo=%s mdlen=%d rate=%d len0=%d len1=%d k=%d\n",
           c->name, c->mdlen, c->rate, c->len0, c->len1, (c->len1 - c->len0) / c->rate);
    if (fails == 0) printf("Throughput vectors OK (hash_top == golden).\n");

    return fails;
}

// main
int main(int argc, char **argv) {

    // [OPT] with an argument: throughput mode (or robust); without: KAT as before
    // [OPT] B-03: -argv robust runs the wrong-DMA-length tests (also in cosim)
    if (argc > 1 && strcmp(argv[1], "robust") == 0) return test_robust() != 0;
    if (argc > 1) return test_throughput(argv[1]) != 0;

    // [WAS] if (test_hash() == 0 ) printf("All Self-Tests OK!\n");
    // [WAS] return 0;   -> csim/cosim reported PASS even on a digest mismatch
    int fails = test_hash();
    if (fails == 0) printf("All Self-Tests OK!\n");

#ifndef __RTL_SIMULATION__
    // [OPT] B-03: csim only (cosim: -argv robust), keeps the default cosim = KAT transactions only
    fails += test_robust();
#endif

    return fails != 0;   // [OPT] nonzero exit -> csim/cosim FAIL on mismatch
}