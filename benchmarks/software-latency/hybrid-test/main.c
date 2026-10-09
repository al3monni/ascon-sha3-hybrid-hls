/*
 * main.c  -  Benchmark della implementazione IBRIDA (SHA3-256, SHA3-512, Ascon)
 * ============================================================================
 *
 * Cosa fa, in breve:
 *   1. Per ogni vettore KAT noto, verifica che l'hash prodotto sia corretto.
 *   2. Per ogni dimensione di input sintetico (16/64/256/1024/4096 byte) e per
 *      ogni algoritmo, esegue:
 *          - una fase di WARMUP (iterazioni scartate, per stabilizzare cache
 *            e branch predictor)
 *          - una fase di MISURA (N iterazioni cronometrate)
 *      registrando per ogni iterazione sia il tempo (ns) sia i cicli di CPU.
 *   3. Calcola le statistiche RAW (media, mediana, dev. std., min, max) e
 *      scrive una riga di risultati nel file dati passato come argomento.
 *
 * Uso:
 *   ./bench <file_dati_output> [iterazioni] [warmup]
 *
 *   <file_dati_output>  percorso del file su cui scrivere le righe di misura
 *   [iterazioni]        default 100000
 *   [warmup]            default 1000
 *
 * Il file dati e' un file di appoggio: lo script run_all.sh lo legge per
 * costruire result.txt e poi lo cancella.
 */

#include "../bench_common.h"   /* infrastruttura di misura condivisa */
#include "hash.h"              /* l'implementazione da testare       */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPL_NAME "hybrid"

/* ------------------------------------------------------------------ */
/*  Definizione dei casi di test                                      */
/* ------------------------------------------------------------------ */
/*
 * Ogni caso e' o un VETTORE KAT (per verificare la correttezza) o un
 * INPUT SINTETICO di una certa dimensione (per misurare le prestazioni).
 *
 * Nella implementazione ibrida l'algoritmo si seleziona col campo `mode`:
 *   mode = 1 -> SHA-3 con digest da 32 byte  (SHA3-256)
 *   mode = 2 -> SHA-3 con digest da 64 byte  (SHA3-512)
 *   mode = 0 -> Ascon-Hash (digest da 32 byte)
 */
typedef struct {
    const char *algo;          /* etichetta leggibile: "SHA3-256", ...        */
    uint8_t     mode;          /* selettore algoritmo (vedi sopra)            */
    int         hash_len;      /* lunghezza del digest in byte                */
    const char *msg_hex;       /* input KAT in esadecimale; NULL se sintetico */
    const char *expected_hex;  /* digest atteso del KAT; NULL se sintetico    */
    int         syn_size;      /* dimensione input sintetico; 0 se e' un KAT  */
} bench_case_t;

/* Il vettore KAT per SHA3-512 e' lungo (255 byte): lo tengo a parte per non
 * appesantire la tabella, e lo aggancio al caso giusto dentro main(). */
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

/* NB: l'array NON e' const perche' aggancio a runtime i puntatori del KAT
 *     SHA3-512 (vedi main()); con const finirebbe in memoria di sola lettura. */
static bench_case_t bench_cases[] = {

    /* --- Vettori KAT: servono solo a verificare la correttezza --- */
    { "SHA3-256", 1, 32,
      "9F2FCC7C90DE090D6B87CD7E9718C1EA6CB21118FC2D5DE9F97E5DB6AC1E9C10",
      "2F1A5F7159E34EA19CDDC70EBF9B81F1A66DB40615D7EAD3CC1F1B954D82A3AF", 0 },
    { "SHA3-512", 2, 64, NULL, NULL, 0 },   /* msg/hash agganciati in main() */
    { "Ascon",    0, 32,
      "000102030405060708090A0B0C0D0E0F10111213141516",
      "B4F88D121EDDF6D1FEA9AEF15F68A0F3A16D3D2CDD9817225809C20452B04C61", 0 },

    /* --- Input sintetici: servono a MISURARE le prestazioni --- */
    { "SHA3-256", 1, 32, NULL, NULL,   16 },
    { "SHA3-256", 1, 32, NULL, NULL,   64 },
    { "SHA3-256", 1, 32, NULL, NULL,  256 },
    { "SHA3-256", 1, 32, NULL, NULL, 1024 },
    { "SHA3-256", 1, 32, NULL, NULL, 4096 },

    { "SHA3-512", 2, 64, NULL, NULL,   16 },
    { "SHA3-512", 2, 64, NULL, NULL,   64 },
    { "SHA3-512", 2, 64, NULL, NULL,  256 },
    { "SHA3-512", 2, 64, NULL, NULL, 1024 },
    { "SHA3-512", 2, 64, NULL, NULL, 4096 },

    { "Ascon",    0, 32, NULL, NULL,   16 },
    { "Ascon",    0, 32, NULL, NULL,   64 },
    { "Ascon",    0, 32, NULL, NULL,  256 },
    { "Ascon",    0, 32, NULL, NULL, 1024 },
    { "Ascon",    0, 32, NULL, NULL, 4096 },
};
static const int NUM_CASES = sizeof(bench_cases) / sizeof(bench_cases[0]);

/* Buffer riutilizzati fra i vari casi. */
static uint8_t msg_buf[BENCH_MAX_MSG_BYTES];
static uint8_t hash_buf[64];
static uint8_t expected_buf[64];

/* Array delle misure: una entry per iterazione, allocati in main(). */
static uint64_t *wall_ns  = NULL;   /* tempi in nanosecondi */
static uint64_t *cycles   = NULL;   /* cicli di CPU         */

/* ------------------------------------------------------------------ */
/*  Verifica di correttezza su un vettore KAT                         */
/* ------------------------------------------------------------------ */
/* Calcola l'hash dell'input KAT e lo confronta col digest atteso.
 * Ritorna 1 se corretto, 0 altrimenti. */
static int verify_kat(const bench_case_t *bc, int msg_len) {
    int exp_len = bench_readhex(expected_buf, bc->expected_hex, sizeof(expected_buf));
    memset(hash_buf, 0, sizeof(hash_buf));

    hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len, bc->mode);

    if (exp_len != bc->hash_len ||
        memcmp(expected_buf, hash_buf, bc->hash_len) != 0) {
        printf("  [FAIL] KAT %-9s : digest NON corretto!\n", bc->algo);
        return 0;
    }
    printf("  [ok]   KAT %-9s : correttezza verificata\n", bc->algo);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Misura di un singolo caso sintetico                               */
/* ------------------------------------------------------------------ */
static void measure_case(const bench_case_t *bc, int warmup, int iters, FILE *fout) {

    /* Genero l'input sintetico: byte deterministici dipendenti dalla taglia. */
    int msg_len   = bc->syn_size;
    uint64_t seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)bc->syn_size;
    bench_fill_synthetic(msg_buf, msg_len, seed);

    /* WARMUP: stesse chiamate, ma i tempi NON vengono registrati. */
    for (int i = 0; i < warmup; i++)
        hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len, bc->mode);

    /* MISURA: cronometro ogni singola chiamata (tempo e cicli). */
    for (int i = 0; i < iters; i++) {
        uint64_t c0 = bench_now_cycles();
        uint64_t t0 = bench_now_ns();

        hash(msg_buf, (size_t)msg_len, hash_buf, bc->hash_len, bc->mode);

        uint64_t t1 = bench_now_ns();
        uint64_t c1 = bench_now_cycles();

        wall_ns[i] = t1 - t0;
        cycles [i] = c1 - c0;
    }

    /* Calcolo le statistiche (le funzioni ordinano gli array sul posto). */
    bench_stats_t ns_stats, cy_stats;
    bench_compute_stats(wall_ns, iters, &ns_stats);
    bench_compute_stats(cycles,  iters, &cy_stats);

    /* Scrivo la riga nel file dati per lo script bash. */
    bench_write_row(fout, IMPL_NAME, bc->algo, msg_len, &ns_stats, &cy_stats);

    /* Stampa di cortesia a video, per seguire l'avanzamento. */
    printf("  %-9s %5d B : mean %9.1f ns | %9.1f cicli\n",
           bc->algo, msg_len, ns_stats.mean, cy_stats.mean);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {

    /* --- Argomenti da riga di comando --- */
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_dati_output> [iterazioni] [warmup]\n", argv[0]);
        return 1;
    }
    const char *out_path = argv[1];
    int iters  = (argc > 2) ? atoi(argv[2]) : BENCH_DEFAULT_ITERS;
    int warmup = (argc > 3) ? atoi(argv[3]) : BENCH_DEFAULT_WARMUP;
    if (iters  <= 0) iters  = BENCH_DEFAULT_ITERS;
    if (warmup <  0) warmup = BENCH_DEFAULT_WARMUP;

    /* --- Aggancio i puntatori del KAT SHA3-512 (vedi commento sopra) --- */
    for (int i = 0; i < NUM_CASES; i++) {
        if (bench_cases[i].mode == 2 && bench_cases[i].syn_size == 0) {
            bench_cases[i].msg_hex      = SHA3_512_KAT_MSG;
            bench_cases[i].expected_hex = SHA3_512_KAT_HASH;
        }
    }

    /* --- Alloco gli array delle misure --- */
    wall_ns = malloc((size_t)iters * sizeof(uint64_t));
    cycles  = malloc((size_t)iters * sizeof(uint64_t));
    if (!wall_ns || !cycles) {
        fprintf(stderr, "Memoria insufficiente per %d iterazioni\n", iters);
        return 1;
    }

    /* --- Apro il file dati in scrittura (lo sovrascrivo da capo) --- */
    FILE *fout = fopen(out_path, "w");
    if (!fout) {
        fprintf(stderr, "Impossibile aprire il file dati '%s'\n", out_path);
        return 1;
    }

    printf("\n===== Implementazione: %s  (iters=%d, warmup=%d) =====\n",
           IMPL_NAME, iters, warmup);

    /* --- Fase 1: verifica correttezza sui KAT --- */
    printf("Verifica correttezza:\n");
    for (int i = 0; i < NUM_CASES; i++) {
        if (bench_cases[i].syn_size == 0)               /* e' un KAT */
            verify_kat(&bench_cases[i],
                       bench_readhex(msg_buf, bench_cases[i].msg_hex, sizeof(msg_buf)));
    }

    /* --- Fase 2: misura sui casi sintetici --- */
    printf("Misure:\n");
    for (int i = 0; i < NUM_CASES; i++) {
        if (bench_cases[i].syn_size > 0)                /* e' un caso sintetico */
            measure_case(&bench_cases[i], warmup, iters, fout);
    }

    fclose(fout);
    free(wall_ns);
    free(cycles);

    printf("Fatto. Misure scritte in: %s\n", out_path);
    return 0;
}
