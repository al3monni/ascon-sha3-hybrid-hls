/*
 * hash_dma_test.c - B-04 board test of the hash IPs on the KV260 (Ubuntu + uniss_dma driver)
 *
 * Usage : sudo ./hash_dma_test <hybrid|sha3|ascon> <lite|ib> [golden.txt] [max_len]
 * Build : gcc -O2 -Wall -o hash_dma_test hash_dma_test.c
 * Needs : bitstream + overlay loaded (fpgautil), driver inserted (insmod axidma.ko),
 *         golden.txt from tests/equivalence/ (Python hashlib / pyascon references)
 *
 * For every algorithm of the variant and every message length 0..max_len (default 256) it
 * hashes msg[i] = (i*167 + 13) mod 256 (same message as the testbench) on the IP and compares
 * the digest with golden.txt. Same check as tests/equivalence/run.sh, on the real hardware.
 *
 * Data path (block design of scripts/vivado/build_bd.tcl, device tree pl.dtsi):
 *   driver channel 0 = axi_dma_0 MM2S -> hash_0 input stream
 *   driver channel 1 = axi_dma_1 S2MM <- hash_0 output stream
 *
 * Control (see <variant>/hash.h):
 *   lite : inlen / mdlen / mode written in the AXI4-Lite registers of hash_0 (mapped through
 *          /dev/mem at HASH_CTRL_BASE), ap_start, status = ap_return
 *   ib   : no registers; the MM2S buffer starts with the 64-bit header
 *          [31:0] inlen | [47:32] mdlen | [55:48] mode, and the IP sends one 64-bit status
 *          word after the digest (0 = OK, 1 = TLAST did not match inlen)
 *
 * The uniss_dma driver (mdc-suite/AXI-based-driver-applications) only starts a transfer on a
 * halted channel, so the DMAs are reset before every message (IOCTL_DMA_RESET_ALL).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* --- uniss_dma driver interface (copied from axidma.c / dma_test.c) --------------------- */
#define DMA_MAGIC 'D'
#define IOCTL_SELECT_CHANNEL        _IOW(DMA_MAGIC, 0, int)
#define IOCTL_DMA_WRITE_BUFFER      _IOW(DMA_MAGIC, 1, unsigned char *)
#define IOCTL_DMA_READ_BUFFER       _IOR(DMA_MAGIC, 2, unsigned char *)
#define IOCTL_DMA_START_TRANSFER    _IOW(DMA_MAGIC, 3, size_t)
#define IOCTL_READ_STATUS_REGISTER  _IOR(DMA_MAGIC, 4, unsigned int *)
#define IOCTL_DMA_RESET             _IOW(DMA_MAGIC, 5, size_t)
#define IOCTL_DMA_RESET_ALL         _IOW(DMA_MAGIC, 6, size_t)

#define DEVICE_FILE   "/dev/uniss_dma"
#define DMA_BUF_SIZE  65535          /* the driver copies this many bytes on every read/write */
#define CH_MM2S       0
#define CH_S2MM       1
#define DMASR_IDLE    0x2
#define DMASR_ERRORS  0x70           /* DMAIntErr | DMASlvErr | DMADecErr */

/* --- hash_0 AXI4-Lite registers (lite version; offsets from the HLS driver xhash_top_hw.h) */
#define HASH_CTRL_BASE   0xA0020000UL    /* build_bd.tcl: ADDR_IP */
#define HASH_CTRL_SIZE   0x10000
#define REG_CTRL         0x00            /* bit0 ap_start, bit1 ap_done (COR), bit2 ap_idle */
#define REG_RETURN       0x10            /* ap_return: 0 = OK, 1 = length mismatch */
#define REG_INLEN        0x18
#define REG_MDLEN        0x20            /* hybrid, sha3 */
#define REG_MODE         0x28            /* hybrid only */

#define TIMEOUT_POLLS    10000000L

typedef struct { const char *name; int mdlen; uint8_t mode; } algo_t;

static const algo_t ALGOS_HYBRID[] = { {"sha3-256", 32, 1}, {"sha3-512", 64, 2}, {"ascon", 32, 0} };
static const algo_t ALGOS_SHA3[]   = { {"sha3-256", 32, 1}, {"sha3-512", 64, 2} };
static const algo_t ALGOS_ASCON[]  = { {"ascon", 32, 0} };

static int fd_dma = -1;
static volatile uint32_t *regs = NULL;   /* lite only */
static int variant_hybrid = 0, variant_sha3 = 0, use_ib = 0;

static uint8_t *buf_in, *buf_out;        /* DMA_BUF_SIZE + 1 each (driver copies 65535 bytes) */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* golden.txt line: "<algo> <len> <hex digest>" -> expected digest of (algo, len) */
static int golden_lookup(const char *path, const char *algo, int len, uint8_t *md, int mdlen) {
    static char line[512];
    char name[32], hex[256];
    int l, i;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%31s %d %255s", name, &l, hex) != 3) continue;
        if (strcmp(name, algo) != 0 || l != len) continue;
        if ((int) strlen(hex) != 2 * mdlen) break;
        for (i = 0; i < mdlen; i++) md[i] = (uint8_t) (hexval(hex[2 * i]) << 4 | hexval(hex[2 * i + 1]));
        fclose(f);
        return 0;
    }
    fclose(f);
    return -1;
}

static int dma_ioctl(unsigned long cmd, unsigned long arg, const char *what) {
    if (ioctl(fd_dma, cmd, arg) < 0) { perror(what); return -1; }
    return 0;
}

static int dma_start(int ch, size_t len) {
    if (dma_ioctl(IOCTL_SELECT_CHANNEL, ch, "select channel")) return -1;
    return dma_ioctl(IOCTL_DMA_START_TRANSFER, len, "start transfer");
}

static int dma_wait_idle(int ch) {
    unsigned int st = 0;
    long n;
    if (dma_ioctl(IOCTL_SELECT_CHANNEL, ch, "select channel")) return -1;
    for (n = 0; n < TIMEOUT_POLLS; n++) {
        if (ioctl(fd_dma, IOCTL_READ_STATUS_REGISTER, &st) < 0) { perror("read status"); return -1; }
        if (st & DMASR_ERRORS) { fprintf(stderr, "  DMA ch%d error, DMASR = 0x%08X\n", ch, st); return -1; }
        if (st & DMASR_IDLE) return 0;
    }
    fprintf(stderr, "  DMA ch%d timeout, DMASR = 0x%08X\n", ch, st);
    return -1;
}

/* One message on the IP. Returns 0 if the transaction completed; *status = IP status. */
static int ip_hash(const uint8_t *msg, int len, uint8_t *md, int mdlen, uint8_t mode, int *status) {

    int off = use_ib ? 8 : 0;           /* ib: 8-byte header in front of the message */
    long n;

    if (len + off > DMA_BUF_SIZE) { fprintf(stderr, "message too long\n"); return -1; }

    if (dma_ioctl(IOCTL_DMA_RESET_ALL, 0, "reset DMAs")) return -1;

    memset(buf_out, 0, DMA_BUF_SIZE + 1);
    memcpy(buf_in + off, msg, len);

    if (use_ib) {
        /* header beat, little-endian: [31:0] inlen | [47:32] mdlen | [55:48] mode */
        uint64_t h = (uint64_t) (uint32_t) len | ((uint64_t) mdlen << 32) | ((uint64_t) mode << 48);
        memcpy(buf_in, &h, 8);
    } else {
        /* lite: parameters, then ap_start (the IP waits for the stream) */
        if (!(regs[REG_CTRL / 4] & 0x4)) { fprintf(stderr, "  IP not idle\n"); return -1; }
        regs[REG_INLEN / 4] = (uint32_t) len;
        if (variant_hybrid || variant_sha3) regs[REG_MDLEN / 4] = (uint32_t) mdlen;
        if (variant_hybrid) regs[REG_MODE / 4] = mode;
        regs[REG_CTRL / 4] = 0x1;
    }

    /* S2MM first (ends on TLAST, so a larger length is fine), then MM2S.
       lite + empty message: no MM2S transfer at all (the IP reads no beat). */
    if (dma_start(CH_S2MM, 128)) return -1;
    if (len + off > 0) {
        if (dma_ioctl(IOCTL_SELECT_CHANNEL, CH_MM2S, "select channel")) return -1;
        if (dma_ioctl(IOCTL_DMA_WRITE_BUFFER, (unsigned long) buf_in, "write buffer")) return -1;
        if (dma_ioctl(IOCTL_DMA_START_TRANSFER, (size_t) (len + off), "start MM2S")) return -1;
        if (dma_wait_idle(CH_MM2S)) return -1;
    }
    if (dma_wait_idle(CH_S2MM)) return -1;

    if (dma_ioctl(IOCTL_SELECT_CHANNEL, CH_S2MM, "select channel")) return -1;
    if (dma_ioctl(IOCTL_DMA_READ_BUFFER, (unsigned long) buf_out, "read buffer")) return -1;
    memcpy(md, buf_out, mdlen);

    if (use_ib) {
        uint64_t s;
        memcpy(&s, buf_out + mdlen, 8);  /* status word right after the digest (packed by S2MM) */
        *status = (int) s;
    } else {
        for (n = 0; n < TIMEOUT_POLLS && !(regs[REG_CTRL / 4] & 0x6); n++) ;   /* done or idle */
        if (n == TIMEOUT_POLLS) { fprintf(stderr, "  ap_done timeout\n"); return -1; }
        *status = (int) regs[REG_RETURN / 4];
    }
    return 0;
}

int main(int argc, char **argv) {

    const algo_t *algos;
    int nalgos, a, len, i, st, max_len = 256, fails = 0, total = 0, missing = 0;
    const char *golden = "golden.txt";
    uint8_t msg[DMA_BUF_SIZE], md[64], ref[64];
    struct timespec t0, t1;
    double us = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: sudo %s <hybrid|sha3|ascon> <lite|ib> [golden.txt] [max_len]\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "hybrid"))     { algos = ALGOS_HYBRID; nalgos = 3; variant_hybrid = 1; }
    else if (!strcmp(argv[1], "sha3"))  { algos = ALGOS_SHA3;   nalgos = 2; variant_sha3 = 1; }
    else if (!strcmp(argv[1], "ascon")) { algos = ALGOS_ASCON;  nalgos = 1; }
    else { fprintf(stderr, "unknown variant %s\n", argv[1]); return 2; }
    if (!strcmp(argv[2], "ib")) use_ib = 1;
    else if (strcmp(argv[2], "lite")) { fprintf(stderr, "ctrl must be lite or ib\n"); return 2; }
    if (argc > 3) golden = argv[3];
    if (argc > 4) max_len = atoi(argv[4]);

    buf_in  = calloc(1, DMA_BUF_SIZE + 1);
    buf_out = calloc(1, DMA_BUF_SIZE + 1);
    if (!buf_in || !buf_out) { perror("calloc"); return 1; }

    fd_dma = open(DEVICE_FILE, O_RDWR);
    if (fd_dma < 0) { perror("open " DEVICE_FILE); return 1; }

    if (!use_ib) {
        int fd_mem = open("/dev/mem", O_RDWR | O_SYNC);
        if (fd_mem < 0) { perror("open /dev/mem (run with sudo)"); return 1; }
        void *p = mmap(NULL, HASH_CTRL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, HASH_CTRL_BASE);
        if (p == MAP_FAILED) { perror("mmap hash_0 registers"); return 1; }
        regs = (volatile uint32_t *) p;
    }

    for (i = 0; i < (int) sizeof msg; i++) msg[i] = (uint8_t) (i * 167u + 13u);

    printf("hash_dma_test: variant %s, ctrl %s, lengths 0..%d\n", argv[1], argv[2], max_len);

    for (a = 0; a < nalgos; a++) {
        int afails = 0;
        for (len = 0; len <= max_len; len++) {
            if (golden_lookup(golden, algos[a].name, len, ref, algos[a].mdlen)) { missing++; continue; }
            memset(md, 0, sizeof md);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int rc = ip_hash(msg, len, md, algos[a].mdlen, algos[a].mode, &st);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            us += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
            total++;
            if (rc || st != 0 || memcmp(md, ref, algos[a].mdlen)) {
                if (afails < 5) {
                    printf("  FAIL %s len %d: rc %d status %d digest ", algos[a].name, len, rc, st);
                    for (i = 0; i < algos[a].mdlen; i++) printf("%02X", md[i]);
                    printf("\n");
                }
                afails++;
                if (rc) break;          /* transaction error: stop this algorithm */
            }
        }
        printf("  %-9s %s (%d failures)\n", algos[a].name, afails ? "FAIL" : "OK", afails);
        fails += afails;
    }

    if (missing) printf("  note: %d lengths not found in %s\n", missing, golden);
    printf("%s: %d / %d digests correct; mean %.1f us per message (driver + ioctl overhead included)\n",
           fails ? "FAIL" : "PASS", total - fails, total, total ? us / total : 0.0);

    close(fd_dma);
    return fails != 0;
}
