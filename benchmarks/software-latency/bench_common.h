/*
 * bench_common.h
 * --------------
 * Infrastruttura di misura condivisa dai tre main (hybrid / sha3 / ascon).
 *
 * Cosa fornisce:
 *   - misura del tempo  : clock_gettime(CLOCK_MONOTONIC)  -> nanosecondi
 *   - misura dei cicli  : __rdtsc()                       -> cicli CPU (x86)
 *   - parsing esadecimale dei vettori KAT (verifica correttezza)
 *   - generatore pseudo-casuale deterministico (input sintetici riproducibili)
 *   - statistiche        : media, mediana, dev. standard, minimo, massimo
 *                          calcolate DOPO rimozione outlier con regola IQR
 *   - scrittura di UNA riga di misure su un file dati intermedio
 *
 * Tutto e' "header-only" (funzioni static inline): ogni cartella si compila
 * da sola senza dover linkare un file oggetto comune.
 *
 * NOTA sugli outlier: la latenza ha una coda solo a destra (interrupt e
 *       context switch possono solo AGGIUNGERE tempo). Per questo gli outlier
 *       si rimuovono col solo recinto SUPERIORE della regola IQR di Tukey:
 *       si scarta tutto cio' che supera  Q3 + 1.5*(Q3-Q1).
 *       Il minimo non viene toccato (i valori bassi sono i piu' puliti).
 */

#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>

/* __rdtsc() esiste solo su x86 / x86_64. */
#if defined(__x86_64__) || defined(__i386__)
  #include <x86intrin.h>
  #define BENCH_HAS_RDTSC 1
#else
  #define BENCH_HAS_RDTSC 0
#endif

/* ------------------------------------------------------------------ */
/*  Parametri di default (sovrascrivibili da riga di comando)         */
/* ------------------------------------------------------------------ */

#define BENCH_DEFAULT_WARMUP      1000     /* iterazioni di riscaldamento, scartate */
#define BENCH_DEFAULT_ITERS     100000     /* iterazioni effettivamente misurate    */

/* Buffer massimo per il messaggio di input. L'input sintetico piu' grande
 * e' 4096 byte, i vettori KAT arrivano a ~255 byte: 8192 e' abbondante. */
#define BENCH_MAX_MSG_BYTES       8192

/* ------------------------------------------------------------------ */
/*  Misura del tempo e dei cicli                                      */
/* ------------------------------------------------------------------ */

/* Tempo assoluto in nanosecondi da un orologio monotono (non torna indietro). */
static inline uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Contatore dei cicli di CPU (time-stamp counter). */
static inline uint64_t bench_now_cycles(void) {
#if BENCH_HAS_RDTSC
    return __rdtsc();
#else
    return 0;   /* su architetture non-x86 i cicli risulteranno 0 */
#endif
}

/* ------------------------------------------------------------------ */
/*  Parsing esadecimale (per i vettori KAT)                           */
/* ------------------------------------------------------------------ */

static inline int bench_hexdigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

/* Converte una stringa esadecimale in byte. Ritorna il numero di byte letti. */
static inline int bench_readhex(uint8_t *buf, const char *str, int maxbytes) {
    int i, hi, lo;
    for (i = 0; i < maxbytes; i++) {
        hi = bench_hexdigit(str[2 * i]);
        if (hi < 0) return i;
        lo = bench_hexdigit(str[2 * i + 1]);
        if (lo < 0) return i;
        buf[i] = (uint8_t)((hi << 4) + lo);
    }
    return i;
}

/* ------------------------------------------------------------------ */
/*  Generatore pseudo-casuale deterministico (xorshift64)             */
/* ------------------------------------------------------------------ */

static inline uint64_t bench_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* Riempie buf con n byte pseudo-casuali ma DETERMINISTICI (seed fisso):
 * cosi' tutte e tre le implementazioni elaborano esattamente gli stessi
 * byte e i risultati sono riproducibili da una run all'altra. */
static inline void bench_fill_synthetic(uint8_t *buf, int n, uint64_t seed) {
    uint64_t state = seed ? seed : 0xC0FFEE12345ULL;
    int i = 0;
    while (i + 8 <= n) {
        uint64_t r = bench_xorshift64(&state);
        memcpy(buf + i, &r, 8);
        i += 8;
    }
    if (i < n) {
        uint64_t r = bench_xorshift64(&state);
        memcpy(buf + i, &r, n - i);
    }
}

/* ------------------------------------------------------------------ */
/*  Statistiche RAW                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t n_total;   /* campioni totali misurati                        */
    uint64_t n_removed; /* campioni scartati come outlier (recinto IQR)    */
    double   mean;      /* media aritmetica   (sui campioni TENUTI)        */
    double   median;    /* mediana            (sui campioni TENUTI)        */
    double   stddev;    /* dev. standard      (sui campioni TENUTI)        */
    uint64_t min;       /* minimo  (= min grezzo: tagliamo solo in cima)   */
    uint64_t max;       /* massimo (= max dei campioni TENUTI)             */
} bench_stats_t;

/* Funzione di confronto per qsort su uint64_t. */
static int bench_cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    if (x < y) return -1;
    if (x > y) return  1;
    return 0;
}

/* Percentile (con interpolazione lineare) su un array GIA' ordinato.
 * p in [0,1]: p=0.25 -> primo quartile Q1, p=0.75 -> terzo quartile Q3. */
static inline double bench_percentile_sorted(const uint64_t *s, int n, double p) {
    if (n <= 0) return 0.0;
    if (n == 1) return (double)s[0];
    double rank = p * (double)(n - 1);
    int lo = (int)rank;
    int hi = lo + 1;
    if (hi >= n) hi = n - 1;
    double frac = rank - (double)lo;
    return (double)s[lo] + frac * ((double)s[hi] - (double)s[lo]);
}

/*
 * Calcola le statistiche DOPO aver rimosso gli outlier con la regola IQR,
 * applicata solo al RECINTO SUPERIORE (la latenza ha una coda solo a destra).
 *
 * Procedura:
 *   1. ordino i dati;
 *   2. calcolo Q1 (25 percentile) e Q3 (75 percentile);
 *   3. IQR = Q3 - Q1;
 *   4. recinto superiore = Q3 + 1.5 * IQR;
 *   5. tengo tutti i campioni <= recinto, scarto quelli sopra;
 *   6. calcolo media, mediana, dev. standard, min e max sui campioni tenuti.
 *
 * Nota: NON applico il recinto inferiore, quindi il minimo coincide col
 *       minimo grezzo (i valori bassi sono le misure piu' pulite).
 *
 * ATTENZIONE: ordina l'array `data` sul posto.
 */
static inline void bench_compute_stats(uint64_t *data, int n, bench_stats_t *out) {
    memset(out, 0, sizeof(*out));
    if (n <= 0) return;

    /* 1. ordino */
    qsort(data, n, sizeof(uint64_t), bench_cmp_u64);

    /* 2-4. quartili, IQR e recinto superiore */
    double q1    = bench_percentile_sorted(data, n, 0.25);
    double q3    = bench_percentile_sorted(data, n, 0.75);
    double iqr   = q3 - q1;
    double fence = q3 + 1.5 * iqr;

    /* 5. conto quanti campioni stanno sotto/uguale al recinto.
     *    Essendo l'array ordinato, sono i primi `kept` elementi. */
    int kept = 0;
    while (kept < n && (double)data[kept] <= fence) kept++;
    if (kept < 1) kept = 1;   /* salvaguardia: tengo almeno un campione */

    out->n_total   = (uint64_t)n;
    out->n_removed = (uint64_t)(n - kept);

    /* 6. statistiche sui campioni tenuti (data[0 .. kept-1]) */
    out->min = data[0];          /* tagliamo solo in cima -> min invariato */
    out->max = data[kept - 1];   /* massimo "normale" sopravvissuto         */

    /* media */
    double sum = 0.0;
    for (int i = 0; i < kept; i++) sum += (double)data[i];
    out->mean = sum / (double)kept;

    /* mediana del sottoinsieme tenuto */
    if (kept % 2 == 1) {
        out->median = (double)data[kept / 2];
    } else {
        out->median = ((double)data[kept / 2 - 1] + (double)data[kept / 2]) / 2.0;
    }

    /* deviazione standard campionaria (denominatore kept-1) */
    if (kept > 1) {
        double sq = 0.0;
        for (int i = 0; i < kept; i++) {
            double d = (double)data[i] - out->mean;
            sq += d * d;
        }
        out->stddev = sqrt(sq / (double)(kept - 1));
    }
}

/* ------------------------------------------------------------------ */
/*  Utility varie                                                     */
/* ------------------------------------------------------------------ */

/* Crea una directory (e quelle intermedie) se non esistono. */
static inline void bench_mkdir_p(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return;
    strcpy(tmp, path);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* ------------------------------------------------------------------ */
/*  Scrittura di una riga di misure sul file dati intermedio          */
/* ------------------------------------------------------------------ */

/*
 * Scrive UNA riga (separata da spazi) con tutte le misure di un caso.
 * Lo script bash legge questo file per costruire le tabelle di result.txt.
 *
 * Ordine dei 17 campi (IMPORTANTE: deve combaciare con il `read` in bash):
 *
 *   impl  algo  size_byte
 *   ns_mean  ns_median  ns_sd  ns_min  ns_max  ns_removed
 *   cy_mean  cy_median  cy_sd  cy_min  cy_max  cy_removed
 *   throughput_byte_s  throughput_hash_s
 *
 * Tutte le statistiche sono calcolate DOPO la rimozione degli outlier (IQR).
 * ns_removed / cy_removed = quanti campioni sono stati scartati come outlier.
 *
 * Il throughput e' calcolato sulla MEDIA (ripulita) dei tempi:
 *   byte/s = dimensione_input * 1e9 / ns_mean
 *   hash/s = 1e9 / ns_mean
 */
static inline void bench_write_row(FILE *f,
                                   const char *impl,
                                   const char *algo,
                                   int msg_size,
                                   const bench_stats_t *ns,
                                   const bench_stats_t *cy) {
    double bps = 0.0, hps = 0.0;
    if (ns->mean > 0.0) {
        bps = (double)msg_size * 1e9 / ns->mean;
        hps = 1e9 / ns->mean;
    }

    fprintf(f,
        "%s %s %d "
        "%.1f %.1f %.1f %llu %llu %llu "
        "%.1f %.1f %.1f %llu %llu %llu "
        "%.1f %.1f\n",
        impl, algo, msg_size,
        ns->mean, ns->median, ns->stddev,
        (unsigned long long)ns->min, (unsigned long long)ns->max,
        (unsigned long long)ns->n_removed,
        cy->mean, cy->median, cy->stddev,
        (unsigned long long)cy->min, (unsigned long long)cy->max,
        (unsigned long long)cy->n_removed,
        bps, hps);
}

#endif /* BENCH_COMMON_H */
