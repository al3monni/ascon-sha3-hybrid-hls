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

// returns zero on success, nonzero + stderr messages on failure
// [WAS] int test_hash()  -> always called hash_top
// [OPT] use_top selects hash_top (RTL in cosim) or hash (pure C): throughput mode
//       validates the C reference on the KAT without adding RTL transactions

int test_hash(int use_top){
	
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
	
    int i,j, fails, msg_len, hash_len;
    uint8_t digest[64], buf[64], msg[256];

    fails = 0;
    for (i = 0; i < 2; i++) {

        memset(digest, 0, sizeof(digest));
        memset(buf, 0, sizeof(buf));
        memset(msg, 0, sizeof(msg));

        msg_len = test_readhex(msg, testvec[i][0], sizeof(msg));
        hash_len = test_readhex(digest, testvec[i][1], sizeof(digest));

        //hash(msg, msg_len, buf, hash_len);
        //hash_top(msg, msg_len, buf, hash_len); //EDIT replace the hash function with the wrapper hash_top
        if (use_top) hash_top(msg, msg_len, buf, hash_len);   // [OPT] RTL in cosim
        else         hash    (msg, msg_len, buf, hash_len);   // [OPT] C reference only
		
        if (memcmp(digest, buf, hash_len) != 0) {
			
            fprintf(stderr, "[%d] HASH-%d, len %d test FAILED.\n", i, hash_len * 8, msg_len);
            fails++;
        }
    }

    return fails;
}

// ---------------------------------------------------------------------------
// [OPT] Throughput mode (cosim_design -argv <algo>)
// One algorithm per cosim run, exactly two RTL transactions: L(len0), L(len1).
// cosim report: min = L(len0), max = L(len1), avg = check (L0+L1)/2.
// len1 - len0 = k * rate  ->  c = (lat_max - lat_min) / k  [cycles per block].
// Reference digest from the C function hash() (validated on the KAT first),
// so cosim also checks RTL == C on the throughput vectors.
// Same argv names as Hybrid-Test (no mode: standalone variant).
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;   // -argv value
    int mdlen;          // digest bytes
    int rate;           // r [bytes]
    int len0, len1;     // message lengths, len1 - len0 = k * rate, len1 <= 256
} tp_cfg_t;

static const tp_cfg_t TP_CFG[] = {
    { "sha3-256",  32, 136,   0, 136 },  // k = 1
    { "sha3-256v", 32, 136, 120, 256 },  // k = 1, linearity check
    { "sha3-512",  64,  72,   0, 216 },  // k = 3
    { "sha3-512v", 64,  72,  40, 256 },  // k = 3, linearity check (optional)
};
#define TP_NCFG ((int)(sizeof(TP_CFG) / sizeof(TP_CFG[0])))

static int test_throughput(const char *algo){

    const tp_cfg_t *c = NULL;
    int i, j, fails = 0;
    uint8_t msg[256], ref[64], out[64];

    for (i = 0; i < TP_NCFG; i++)
        if (strcmp(TP_CFG[i].name, algo) == 0) c = &TP_CFG[i];

    if (c == NULL) {
        fprintf(stderr, "Unknown algo '%s'. Valid:", algo);
        for (i = 0; i < TP_NCFG; i++) fprintf(stderr, " %s", TP_CFG[i].name);
        fprintf(stderr, "\n");
        return 1;
    }

    // 1) the C reference must pass the KAT (pure C, no RTL transaction)
    if (test_hash(0) != 0) {
        fprintf(stderr, "C reference hash() FAILED the KAT.\n");
        return 1;
    }

    // 2) deterministic message (latency is data-independent; content only feeds the digest check)
    for (i = 0; i < 256; i++) msg[i] = (uint8_t)(i * 167u + 13u);

    const int lens[2] = { c->len0, c->len1 };
    for (j = 0; j < 2; j++) {
        memset(ref, 0, sizeof(ref));
        memset(out, 0, sizeof(out));
        hash    (msg, lens[j], ref, c->mdlen);   // C reference
        hash_top(msg, lens[j], out, c->mdlen);   // RTL in cosim (1 transaction)
        if (memcmp(ref, out, c->mdlen) != 0) {
            fprintf(stderr, "[%s] len %d: hash_top != hash FAILED.\n", c->name, lens[j]);
            fails++;
        }
    }

    printf("TB_THROUGHPUT algo=%s mdlen=%d rate=%d len0=%d len1=%d k=%d\n",
           c->name, c->mdlen, c->rate, c->len0, c->len1, (c->len1 - c->len0) / c->rate);
    if (fails == 0) printf("Throughput vectors OK (hash_top == hash).\n");

    return fails;
}

// main
int main(int argc, char **argv) {

    // [OPT] with an argument: throughput mode; without: KAT as before
    if (argc > 1) return test_throughput(argv[1]) != 0;

    // [WAS] if (test_hash() == 0 ) printf("All Self-Tests OK!\n");
    // [WAS] return 0;   -> csim/cosim reported PASS even on a digest mismatch
    int fails = test_hash(1);
    if (fails == 0) printf("All Self-Tests OK!\n");

    return fails != 0;   // [OPT] nonzero exit -> csim/cosim FAIL on mismatch
}