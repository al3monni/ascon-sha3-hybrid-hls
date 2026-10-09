/*
 * main.c  -  Benchmark della implementazione SINGOLA Ascon-Hash
 * ============================================================================
 *
 * Stessa struttura degli altri due main. Qui c'e' un solo algoritmo (Ascon),
 * niente parametro `mode`, digest sempre da 32 byte.
 *
 * Uso:
 *   ./bench <file_dati_output> [iterazioni] [warmup]
 */

#include "../bench_common.h"
#include "hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPL_NAME "ascon"

/* Un caso di test: KAT (verifica correttezza) oppure input sintetico (misura). */
typedef struct {
    const char *algo;          /* sempre "Ascon"                              */
    int         hash_len;      /* sempre 32                                   */
    const char *msg_hex;       /* input KAT esadecimale; NULL se sintetico    */
    const char *expected_hex;  /* digest atteso del KAT; NULL se sintetico    */
    int         syn_size;      /* dimensione input sintetico; 0 se e' un KAT  */
} bench_case_t;

/* Qui niente stringhe lunghe -> l'array puo' restare const. */
static const bench_case_t bench_cases[] = {

    /* --- KAT (verifica correttezza) --- */
    { "Ascon", 32,
      "000102030405060708090A0B0C0D0E0F10111213141516",
      "B4F88D121EDDF6D1FEA9AEF15F68A0F3A16D3D2CDD9817225809C20452B04C61", 0 },
    { "Ascon", 32,
      "",   /* messaggio vuoto */
      "0B3BE5850F2F6B98CAF29F8FDEA89B64A1FA70AA249B8F839BD53BAA304D92B2", 0 },

    /* --- Input sintetici (misura) --- */
    { "Ascon", 32, NULL, NULL,   16 },
    { "Ascon", 32, NULL, NULL,   64 },
    { "Ascon", 32, NULL, NULL,  256 },
    { "Ascon", 32, NULL, NULL, 1024 },
    { "Ascon", 32, NULL, NULL, 4096 },
};
static const int NUM_CASES = sizeof(bench_cases) / sizeof(bench_cases[0]);

static uint8_t msg_buf[BENCH_MAX_MSG_BYTES];
static uint8_t hash_buf[64];
static uint8_t expected_buf[64];

static uint64_t *wall_ns = NULL;
static uint64_t *cycles  = NULL;

static int verify_kat(const bench_case_t *bc, int msg_len) {
    int exp_len = bench_readhex(expected_buf, bc->expected_hex, sizeof(expected_buf));
    memset(hash_buf, 0, sizeof(hash_buf));

    hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len);   /* senza mode */

    if (exp_len != bc->hash_len ||
        memcmp(expected_buf, hash_buf, bc->hash_len) != 0) {
        printf("  [FAIL] KAT Ascon (%3d B) : digest NON corretto!\n", msg_len);
        return 0;
    }
    printf("  [ok]   KAT Ascon (%3d B) : correttezza verificata\n", msg_len);
    return 1;
}

static void measure_case(const bench_case_t *bc, int warmup, int iters, FILE *fout) {
    int msg_len   = bc->syn_size;
    uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)bc->syn_size;
    bench_fill_synthetic(msg_buf, msg_len, seed);

    for (int i = 0; i < warmup; i++)
        hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len);

    for (int i = 0; i < iters; i++) {
        uint64_t c0 = bench_now_cycles();
        uint64_t t0 = bench_now_ns();

        hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len);

        uint64_t t1 = bench_now_ns();
        uint64_t c1 = bench_now_cycles();

        wall_ns[i] = t1 - t0;
        cycles [i] = c1 - c0;
    }

    bench_stats_t ns_stats, cy_stats;
    bench_compute_stats(wall_ns, iters, &ns_stats);
    bench_compute_stats(cycles,  iters, &cy_stats);

    bench_write_row(fout, IMPL_NAME, bc->algo, msg_len, &ns_stats, &cy_stats);

    printf("  %-9s %5d B : mean %9.1f ns | %9.1f cicli\n",
           bc->algo, msg_len, ns_stats.mean, cy_stats.mean);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_dati_output> [iterazioni] [warmup]\n", argv[0]);
        return 1;
    }
    const char *out_path = argv[1];
    int iters  = (argc > 2) ? atoi(argv[2]) : BENCH_DEFAULT_ITERS;
    int warmup = (argc > 3) ? atoi(argv[3]) : BENCH_DEFAULT_WARMUP;
    if (iters  <= 0) iters  = BENCH_DEFAULT_ITERS;
    if (warmup <  0) warmup = BENCH_DEFAULT_WARMUP;

    wall_ns = malloc((size_t)iters * sizeof(uint64_t));
    cycles  = malloc((size_t)iters * sizeof(uint64_t));
    if (!wall_ns || !cycles) {
        fprintf(stderr, "Memoria insufficiente per %d iterazioni\n", iters);
        return 1;
    }

    FILE *fout = fopen(out_path, "w");
    if (!fout) {
        fprintf(stderr, "Impossibile aprire il file dati '%s'\n", out_path);
        return 1;
    }

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
    free(cycles);

    printf("Fatto. Misure scritte in: %s\n", out_path);
    return 0;
}
