/*
 * main.c  -  Benchmark della implementazione SINGOLA SHA-3 (SHA3-256, SHA3-512)
 * ============================================================================
 *
 * Struttura identica al main della versione ibrida, ma:
 *   - non c'e' il parametro `mode` (questa implementazione fa solo SHA-3);
 *   - l'algoritmo (256 vs 512) si distingue solo dalla lunghezza del digest.
 *
 * Uso:
 *   ./bench <file_dati_output> [iterazioni] [warmup]
 */

#include "../bench_common.h"
#include "hash.h"          /* da ../../Sha-Test (fonte unica, via -I) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPL_NAME "sha3"

/* Un caso di test: KAT (verifica correttezza) oppure input sintetico (misura). */
typedef struct {
    const char *algo;          /* "SHA3-256" o "SHA3-512"                     */
    int         hash_len;      /* 32 -> SHA3-256, 64 -> SHA3-512              */
    const char *msg_hex;       /* input KAT esadecimale; NULL se sintetico    */
    const char *expected_hex;  /* digest atteso del KAT; NULL se sintetico    */
    int         syn_size;      /* dimensione input sintetico; 0 se e' un KAT  */
} bench_case_t;

/* KAT SHA3-512 (input di 255 byte) tenuto a parte. */
static const char *SHA3_512_KAT_MSG =
    "3A3A819C48EFDE2AD914FBF00E18AB6BC4F14513AB27D0C178A188B61431E7F5"
    "623CB66B23346775D386B50E982C493ADBBFC54B9A3CD383382336A1A0B2150A"
    "15358F336D03AE18F666C7573D55C4FD181C29E6CCFDE63EA35F0ADF5885CFC0"
    "A3D84A2B2E4DD24496DB789E663170CEF74798AA1BBCD4574EA0BBA40489D764"
    "B2F83AADC66B148B4A0CD95246C127D5871C4F11418690A5DDF01246A0C80A43"
    "C70088B6183639DCFDA4125BD113A8F49EE23ED306FAAC576C3FB0C1E256671D"
    "817FC2534A52F5B439F72E424DE376F4C565CCA82307DD9EF76DA5B7C4EB7E08"
    "5172E328807C02D011FFBF33785378D79DC266F6A5BE6BB0E4A92ECEEBAEB1";
static const char *SHA3_512_KAT_HASH =
    "6E8B8BD195BDD560689AF2348BDC74AB7CD05ED8B9A57711E9BE71E9726FDA45"
    "91FEE12205EDACAF82FFBBAF16DFF9E702A708862080166C2FF6BA379BC7FFC2";

/* Non const: aggancio i puntatori del KAT SHA3-512 a runtime in main(). */
static bench_case_t bench_cases[] = {

    /* --- KAT (verifica correttezza) --- */
    { "SHA3-256", 32,
      "9F2FCC7C90DE090D6B87CD7E9718C1EA6CB21118FC2D5DE9F97E5DB6AC1E9C10",
      "2F1A5F7159E34EA19CDDC70EBF9B81F1A66DB40615D7EAD3CC1F1B954D82A3AF", 0 },
    { "SHA3-512", 64, NULL, NULL, 0 },

    /* --- Input sintetici (misura) --- */
    { "SHA3-256", 32, NULL, NULL,   16 },
    { "SHA3-256", 32, NULL, NULL,   64 },
    { "SHA3-256", 32, NULL, NULL,  256 },
    { "SHA3-256", 32, NULL, NULL, 1024 },
    { "SHA3-256", 32, NULL, NULL, 4096 },

    { "SHA3-512", 64, NULL, NULL,   16 },
    { "SHA3-512", 64, NULL, NULL,   64 },
    { "SHA3-512", 64, NULL, NULL,  256 },
    { "SHA3-512", 64, NULL, NULL, 1024 },
    { "SHA3-512", 64, NULL, NULL, 4096 },
};
static const int NUM_CASES = sizeof(bench_cases) / sizeof(bench_cases[0]);

static uint8_t msg_buf[BENCH_MAX_MSG_BYTES];
static uint8_t hash_buf[64];
static uint8_t expected_buf[64];

static uint64_t *wall_ns = NULL;
static uint64_t *ticks_buf = NULL;   /* tick per chiamata    */
static bench_pmu_t pmu;              /* contatori hardware   */

static int verify_kat(const bench_case_t *bc, int msg_len) {
    int exp_len = bench_readhex(expected_buf, bc->expected_hex, sizeof(expected_buf));
    memset(hash_buf, 0, sizeof(hash_buf));

    hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len);   /* senza mode */

    if (exp_len != bc->hash_len ||
        memcmp(expected_buf, hash_buf, bc->hash_len) != 0) {
        printf("  [FAIL] KAT %-9s : digest NON corretto!\n", bc->algo);
        return 0;
    }
    printf("  [ok]   KAT %-9s : correttezza verificata\n", bc->algo);
    return 1;
}

static void measure_case(const bench_case_t *bc, int warmup, int iters, FILE *fout) {
    int msg_len   = bc->syn_size;
    uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)bc->syn_size;
    bench_fill_synthetic(msg_buf, msg_len, seed);

    /* Misura (fasi A e B, vedi bench_common.h). CALL e' la chiamata diretta a hash(). */
    bench_case_result_t res;
    BENCH_MEASURE(&res, &pmu, warmup, iters, wall_ns, ticks_buf,
                  hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len));

    /* Scrivo la riga nel file dati per lo script bash. */
    bench_write_row(fout, IMPL_NAME, bc->algo, msg_len, &res);

    /* Stampa di cortesia a video, per seguire l'avanzamento. */
    if (res.pmu_ok)
        printf("  %-9s %5d B : mean %9.1f ns | %10.1f cicli core | IPC %.2f\n",
               bc->algo, msg_len, res.ns.mean, res.cycles,
               res.cycles > 0 ? res.instructions / res.cycles : 0.0);
    else
        printf("  %-9s %5d B : mean %9.1f ns | %10.1f tick (PMU n/a)\n",
               bc->algo, msg_len, res.ns.mean, res.ticks.mean);
}

int main(int argc, char **argv) {
    /* --sizeof: stampa la dimensione del contesto dell'hash (footprint) ed esce */
    if (argc == 2 && strcmp(argv[1], "--sizeof") == 0) {
        printf("%zu\n", sizeof(hash_ctx_t));
        return 0;
    }

    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_dati_output> [iterazioni] [warmup]\n", argv[0]);
        return 1;
    }
    const char *out_path = argv[1];
    int iters  = (argc > 2) ? atoi(argv[2]) : BENCH_DEFAULT_ITERS;
    int warmup = (argc > 3) ? atoi(argv[3]) : BENCH_DEFAULT_WARMUP;
    if (iters  <= 0) iters  = BENCH_DEFAULT_ITERS;
    if (warmup <  0) warmup = BENCH_DEFAULT_WARMUP;

    /* Aggancio il KAT SHA3-512 (digest 64 byte, caso KAT). */
    for (int i = 0; i < NUM_CASES; i++) {
        if (bench_cases[i].hash_len == 64 && bench_cases[i].syn_size == 0) {
            bench_cases[i].msg_hex      = SHA3_512_KAT_MSG;
            bench_cases[i].expected_hex = SHA3_512_KAT_HASH;
        }
    }

    wall_ns = malloc((size_t)iters * sizeof(uint64_t));
    ticks_buf = malloc((size_t)iters * sizeof(uint64_t));
    if (!wall_ns || !ticks_buf) {
        fprintf(stderr, "Memoria insufficiente per %d iterazioni\n", iters);
        return 1;
    }

    FILE *fout = fopen(out_path, "w");
    if (!fout) {
        fprintf(stderr, "Impossibile aprire il file dati '%s'\n", out_path);
        return 1;
    }

    /* --- Contatori hardware (se disponibili) e calibrazione del timer --- */
    bench_pmu_open(&pmu);
    bench_write_calibration(fout, &pmu);

    printf("\n===== Implementazione: %s  (iters=%d, warmup=%d) =====\n",
           IMPL_NAME, iters, warmup);

    printf("Verifica correttezza:\n");
    for (int i = 0; i < NUM_CASES; i++)
        if (bench_cases[i].syn_size == 0)
            verify_kat(&bench_cases[i],
                       bench_readhex(msg_buf, bench_cases[i].msg_hex, sizeof(msg_buf)));

    printf("Misure:\n");
    for (int i = 0; i < NUM_CASES; i++)
        if (bench_cases[i].syn_size > 0)
            measure_case(&bench_cases[i], warmup, iters, fout);

    fclose(fout);
    free(wall_ns);
    free(ticks_buf);

    printf("Fatto. Misure scritte in: %s\n", out_path);
    return 0;
}
