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

int test_hash(){

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
        },

		{	//ascon 256
			"000102030405060708090A0B0C0D0E0F10111213141516",
			"B4F88D121EDDF6D1FEA9AEF15F68A0F3A16D3D2CDD9817225809C20452B04C61"
		},

		{	//ascon 256
			"",
			"0B3BE5850F2F6B98CAF29F8FDEA89B64A1FA70AA249B8F839BD53BAA304D92B2"
		}
    };

    uint8_t modev[4]={1,2,0,0};
    int i,j, fails, msg_len, hash_len;
    uint8_t digest[64], buf[64], msg[256];

    fails = 0;
    for (i = 0; i < 4; i++) {

        memset(digest, 0, sizeof(digest));
        memset(buf, 0, sizeof(buf));
        memset(msg, 0, sizeof(msg));

        msg_len = test_readhex(msg, testvec[i][0], sizeof(msg));
        hash_len = test_readhex(digest, testvec[i][1], sizeof(digest));

        //hash(msg, msg_len, buf, hash_len, modev[i]);
        hash_top(msg, msg_len, buf, hash_len, modev[i]); //EDIT replace the hash function with the wrapper hash_top

        if (memcmp(digest, buf, hash_len) != 0) {

            fprintf(stderr, "[%d] HASH-%d, len %d test FAILED.\n", i, hash_len * 8, msg_len);
            fails++;
        }
    }

    return fails;
}

// main
int main(int argc, char **argv) {

    if (test_hash() == 0 ) printf("All Self-Tests OK!\n");

    return 0;
}