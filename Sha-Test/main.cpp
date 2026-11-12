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
//       same as Hybrid-Test without the mode argument
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
    *ret = hash_top(in, out, len, mdlen);
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
       
        {   // SHA3-256, short message
            "9F2FCC7C90DE090D6B87CD7E9718C1EA6CB21118FC2D5DE9F97E5DB6AC1E9C10",
            "2F1A5F7159E34EA19CDDC70EBF9B81F1A66DB40615D7EAD3CC1F1B954D82A3AF"
        },
      
        {   // SHA3-512, multiblock message
            "3A3A819C48EFDE2AD914FBF00E18AB6BC4F14513AB27D0C178A188B61431E7F5"
            "623CB66B23346775D386B50E982C493ADBBFC54B9A3CD383382336A1A0B2150A"
            "15358F336D03AE18F666C7573D55C4FD181C29E6CCFDE63EA35F0ADF5885CFC0"
            "A3D84A2B2E4DD24496DB789E663170CEF74798AA1BBCD4574EA0BBA40489D764"
            "B2F83AADC66B148B4A0CD95246C127D5871C4F11418690A5DDF01246A0C80A43"
            "C70088B6183639DCFDA4125BD113A8F49EE23ED306FAAC576C3FB0C1E256671D"
            "817FC2534A52F5B439F72E424DE376F4C565CCA82307DD9EF76DA5B7C4EB7E08"
            "5172E328807C02D011FFBF33785378D79DC266F6A5BE6BB0E4A92ECEEBAEB1",
            "6E8B8BD195BDD560689AF2348BDC74AB7CD05ED8B9A57711E9BE71E9726FDA45"
            "91FEE12205EDACAF82FFBBAF16DFF9E702A708862080166C2FF6BA379BC7FFC2"
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

    // SHA3-256, 30-byte message = 4 beats (last one partial)
    const int len = 30, mdlen = 32;

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
// [OPT] B-03: reference digests from tests/equivalence/golden.txt (Python hashlib),
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
    { "sha3-256",  32, 136,   0, 136,    // k = 1
      "A7FFC6F8BF1ED76651C14756A061D662F580FF4DE43B49FA82D80A4B80F8434A",
      "8637968A097008C992AF0EE4342D218DE523A99EEF03A4DAF8359CCCB473A432" },
    { "sha3-256v", 32, 136, 120, 256,    // k = 1, linearity check
      "E40C13F3F4EBA4F21BD89D0A5BE837828621D43C417F445E95CE060436C922A4",
      "504503C9DDEDF72E7E3563CC886E1CF1F6F38788FC2CB7E644C9AA9B9CC75EB5" },
    { "sha3-512",  64,  72,   0, 216,    // k = 3
      "A69F73CCA23A9AC5C8B567DC185A756E97C982164FE25859E0D1DCC1475C80A615B2123AF1F5F94C11E3E9402C3AC558F500199D95B6D3E301758586281DCD26",
      "2C31F033C5CE9C692B15CBC820DCB9086AAA710CE851490F61955CF70512CE1918EAD137DB1F5F4F5200457576E5E86891671C3506254CDFAC1393779783C968" },
    { "sha3-512v", 64,  72,  40, 256,    // k = 3, linearity check (optional)
      "7E7336BB852629F292A2914A554A9AE0B1FB722783476831479B302A8CF5B34446C0AA59C1BE75AF1C58159362C060A843979075DB2C049DE477C89F57BE4ECE",
      "4044F2090AFFBF07AF9EE2B19B0002932EC67D116A45DA54A08F9C284DD686F727237F346B26EF2DADB856365E689BEFACCADF8DF6050B25BD8E38DB837829C3" },
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