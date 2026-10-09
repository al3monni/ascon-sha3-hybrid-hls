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
       
		{	//ascon 256
			"000102030405060708090A0B0C0D0E0F10111213141516",
			"B4F88D121EDDF6D1FEA9AEF15F68A0F3A16D3D2CDD9817225809C20452B04C61"
		},
       
		{	//ascon 256
			"",
			"0B3BE5850F2F6B98CAF29F8FDEA89B64A1FA70AA249B8F839BD53BAA304D92B2"
		}
    };
	
    int i, fails, msg_len, hash_len;
    uint8_t digest[64], buf[64], msg[256];

    fails = 0;
    for (i = 0; i < 2; i++) {

        memset(digest, 0, sizeof(digest));
        memset(buf, 0, sizeof(buf));
        memset(msg, 0, sizeof(msg));

        msg_len = test_readhex(msg, testvec[i][0], sizeof(msg));
        hash_len = test_readhex(digest, testvec[i][1], sizeof(digest));

        //hash(msg, msg_len, buf, hash_len);
        hash_top(msg, msg_len, buf, hash_len); //EDIT replace the hash function with the wrapper hash_top
		
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