/*
 * bench_common.h
 * --------------
 * Infrastruttura di misura condivisa dai tre main (hybrid / sha3 / ascon).
 * Portabile su x86_64 (PC, WSL) e aarch64 (Kria KV260, Cortex-A53).
 *
 * COSA SI MISURA (la "regione cronometrata")
 * ------------------------------------------
 * Una chiamata a
 *
 *     hash(msg, len, md, mdlen [, mode]);     // = hash_init + hash_update + hash_final
 *
 * dove `hash()` e' compilata DIRETTAMENTE dai sorgenti del progetto Vitis
 * (../../<Variante>-Test/hash.c, le pragma HLS sono ignorate da gcc).
 * L'input e' gia' in memoria (e caldo in cache dopo il warmup).
 * NON sono misurati: generazione dell'input, verifica KAT, statistiche, I/O.
 *
 * Per ogni caso (algoritmo x dimensione) ci sono DUE fasi di misura:
 *
 *   A) PER-CHIAMATA  - N chiamate, ciascuna racchiusa fra DUE letture del
 *                      contatore di tick (un registro: rdtsc / cntvct_el0).
 *                      Il tempo in ns si ricava dai tick (ns = tick / f_tick):
 *                      cosi' nel loop non c'e' clock_gettime, che su alcuni
 *                      sistemi (clocksource HPET) costa ~1 us per lettura.
 *                      Danno la DISTRIBUZIONE (media, mediana, sd, min, max).
 *                      Nessuna syscall, allocazione o I/O nel loop.
 *
 *   B) BATCH         - N chiamate consecutive SENZA nulla in mezzo, divise in
 *                      BENCH_SUBBATCHES sotto-batch. Per ogni sotto-batch i
 *                      contatori hardware (PMU: cicli core reali + istruzioni)
 *                      e il tempo sono letti SOLO prima e dopo. Si riporta la
 *                      MEDIANA dei sotto-batch (robusta a disturbi passeggeri)
 *                      e la loro dispersione (indicatore di qualita').
 *                      Danno: ns/hash, cicli/hash, istruzioni/hash, IPC, cpb.
 *
 * In questo modo la misura delle metriche aggiuntive non tocca il codice
 * misurato: la PMU conta in hardware e viene letta fuori dal loop.
 *
 * Contatori usati
 *   - tempo  : clock_gettime(CLOCK_MONOTONIC)                      -> ns
 *   - tick   : x86_64  -> rdtsc        (TSC: frequenza COSTANTE di riferimento,
 *                                       NON i cicli reali del core)
 *              aarch64 -> cntvct_el0   (generic timer, frequenza in cntfrq_el0)
 *   - PMU    : perf_event_open(PERF_COUNT_HW_CPU_CYCLES / INSTRUCTIONS),
 *              solo user-space (exclude_kernel). Richiede
 *              /proc/sys/kernel/perf_event_paranoid <= 2. Se non disponibile
 *              (es. WSL senza PMU virtualizzata) le colonne valgono "n/a".
 *
 * Tutto e' "header-only" (funzioni static inline).
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
#include <unistd.h>

#if defined(__linux__)
  #include <sys/ioctl.h>
  #include <sys/syscall.h>
  #include <linux/perf_event.h>
  #define BENCH_HAS_PERF 1
#else
  #define BENCH_HAS_PERF 0
#endif

#if defined(__x86_64__) || defined(__i386__)
  #include <x86intrin.h>
#endif

/* ------------------------------------------------------------------ */
/*  Parametri di default (sovrascrivibili da riga di comando)         */
/* ------------------------------------------------------------------ */

#define BENCH_DEFAULT_WARMUP      1000     /* iterazioni di riscaldamento, scartate */
#define BENCH_DEFAULT_ITERS      10000     /* iterazioni effettivamente misurate    */
#define BENCH_SUBBATCHES            10     /* sotto-batch della fase B (mediana)    */

/* Buffer massimo per il messaggio di input. L'input sintetico piu' grande
 * e' 4096 byte, i vettori KAT arrivano a ~255 byte: 8192 e' abbondante. */
#define BENCH_MAX_MSG_BYTES       8192

/* ------------------------------------------------------------------ */
/*  Tempo e tick                                                      */
/* ------------------------------------------------------------------ */

/* Tempo assoluto in nanosecondi da un orologio monotono (non torna indietro). */
static inline uint64_t bench_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Contatore di tick a basso costo (lettura di un registro, nessuna syscall). */
static inline uint64_t bench_now_ticks(void) {
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#elif defined(__aarch64__)
    uint64_t v;
    /* isb: evita che la lettura venga anticipata rispetto alle istruzioni precedenti */
    __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v) :: "memory");
    return v;
#else
    return 0;
#endif
}

/* Frequenza dei tick in Hz.
 * aarch64: letta dal registro cntfrq_el0 (valore architetturale).
 * x86_64 : il TSC non espone la frequenza -> stimata confrontando tick e ns
 *          su ~100 ms (eseguita UNA volta, fuori dalle misure). */
static inline double bench_ticks_hz(void) {
#if defined(__aarch64__)
    uint64_t f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return (double)f;
#elif defined(__x86_64__) || defined(__i386__)
    uint64_t t0 = bench_now_ns(), k0 = bench_now_ticks();
    while (bench_now_ns() - t0 < 100000000ULL) { /* attesa attiva ~100 ms */ }
    uint64_t t1 = bench_now_ns(), k1 = bench_now_ticks();
    return (double)(k1 - k0) * 1e9 / (double)(t1 - t0);
#else
    return 0.0;
#endif
}

/* ------------------------------------------------------------------ */
/*  Contatori hardware (PMU) via perf_event_open                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int fd_cycles;     /* leader del gruppo: cicli core reali (user-space) */
    int fd_instr;      /* istruzioni ritirate (user-space)                 */
    int ok;            /* 1 se entrambi i contatori sono disponibili       */
} bench_pmu_t;

typedef struct {
    uint64_t cycles;
    uint64_t instructions;
    int      scaled;   /* 1 se il kernel ha multiplexato i contatori (valori stimati) */
} bench_pmu_reading_t;

#if BENCH_HAS_PERF
static inline int bench_perf_open(uint64_t config, int group_fd) {
    struct perf_event_attr a;
    memset(&a, 0, sizeof(a));
    a.size           = sizeof(a);
    a.type           = PERF_TYPE_HARDWARE;
    a.config         = config;
    a.disabled       = (group_fd == -1);   /* il leader parte disabilitato */
    a.exclude_kernel = 1;                  /* solo codice utente: hash() non entra mai nel kernel */
    a.exclude_hv     = 1;
    a.read_format    = PERF_FORMAT_GROUP
                     | PERF_FORMAT_TOTAL_TIME_ENABLED
                     | PERF_FORMAT_TOTAL_TIME_RUNNING;
    /* pid=0 (questo processo), cpu=-1 (qualsiasi CPU) */
    return (int)syscall(__NR_perf_event_open, &a, 0, -1, group_fd, 0);
}
#endif

/* Apre il gruppo {cicli, istruzioni}. Se fallisce, pmu->ok = 0 e si prosegue
 * senza PMU (le colonne relative saranno "n/a"). */
static inline void bench_pmu_open(bench_pmu_t *pmu) {
    pmu->fd_cycles = pmu->fd_instr = -1;
    pmu->ok = 0;
#if BENCH_HAS_PERF
    pmu->fd_cycles = bench_perf_open(PERF_COUNT_HW_CPU_CYCLES, -1);
    if (pmu->fd_cycles < 0) return;
    pmu->fd_instr = bench_perf_open(PERF_COUNT_HW_INSTRUCTIONS, pmu->fd_cycles);
    if (pmu->fd_instr < 0) { close(pmu->fd_cycles); pmu->fd_cycles = -1; return; }
    pmu->ok = 1;
#endif
}

/* Azzeramento + avvio: chiamato SUBITO PRIMA del batch (fuori dal loop). */
static inline void bench_pmu_start(bench_pmu_t *pmu) {
#if BENCH_HAS_PERF
    if (!pmu->ok) return;
    ioctl(pmu->fd_cycles, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP);
    ioctl(pmu->fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
#else
    (void)pmu;
#endif
}

/* Arresto + lettura: chiamato SUBITO DOPO il batch (fuori dal loop). */
static inline void bench_pmu_stop(bench_pmu_t *pmu, bench_pmu_reading_t *r) {
    memset(r, 0, sizeof(*r));
#if BENCH_HAS_PERF
    if (!pmu->ok) return;
    ioctl(pmu->fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    struct { uint64_t nr, t_enabled, t_running, v[2]; } buf;
    if (read(pmu->fd_cycles, &buf, sizeof(buf)) != (ssize_t)sizeof(buf) || buf.nr != 2) {
        pmu->ok = 0;
        return;
    }
    double scale = 1.0;
    if (buf.t_running > 0 && buf.t_running < buf.t_enabled) {
        scale = (double)buf.t_enabled / (double)buf.t_running;
        r->scaled = 1;
    }
    r->cycles       = (uint64_t)((double)buf.v[0] * scale);
    r->instructions = (uint64_t)((double)buf.v[1] * scale);
#else
    (void)pmu;
#endif
}

/* ------------------------------------------------------------------ */
/*  Statistiche                                                       */
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
 *   1. ordino i dati;  2. Q1, Q3;  3. IQR = Q3 - Q1;  4. recinto = Q3 + 1.5*IQR;
 *   5. tengo i campioni <= recinto;  6. media, mediana, sd, min, max sui tenuti.
 *
 * ATTENZIONE: ordina l'array `data` sul posto.
 */
static inline void bench_compute_stats(uint64_t *data, int n, bench_stats_t *out) {
    memset(out, 0, sizeof(*out));
    if (n <= 0) return;

    qsort(data, n, sizeof(uint64_t), bench_cmp_u64);

    double q1    = bench_percentile_sorted(data, n, 0.25);
    double q3    = bench_percentile_sorted(data, n, 0.75);
    double iqr   = q3 - q1;
    double fence = q3 + 1.5 * iqr;

    int kept = 0;
    while (kept < n && (double)data[kept] <= fence) kept++;
    if (kept < 1) kept = 1;

    out->n_total   = (uint64_t)n;
    out->n_removed = (uint64_t)(n - kept);
    out->min = data[0];
    out->max = data[kept - 1];

    double sum = 0.0;
    for (int i = 0; i < kept; i++) sum += (double)data[i];
    out->mean = sum / (double)kept;

    if (kept % 2 == 1) out->median = (double)data[kept / 2];
    else               out->median = ((double)data[kept / 2 - 1] + (double)data[kept / 2]) / 2.0;

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
/*  Overhead del timer (calibrazione, eseguita una volta)             */
/* ------------------------------------------------------------------ */
/* Misura la regione VUOTA con la stessa coppia di letture usata nella fase
 * per-chiamata (due letture dei tick). Il valore (mediana) va letto come
 * "rumore di fondo" della misura per-chiamata; NON viene sottratto.
 * Riporta anche il costo di UNA clock_gettime (usata solo fuori dai loop):
 * se e' alto (> ~200 ns) la clocksource del kernel e' lenta (es. HPET). */
static inline void bench_timer_overhead(double *ticks_med, double *clock_ns_med) {
    enum { N = 20001 };
    static uint64_t a[N], b[N];
    for (int i = 0; i < N; i++) {
        uint64_t k0 = bench_now_ticks();
        uint64_t k1 = bench_now_ticks();
        a[i] = k1 - k0;
    }
    for (int i = 0; i < N; i++) {
        uint64_t t0 = bench_now_ns();
        uint64_t t1 = bench_now_ns();
        b[i] = t1 - t0;
    }
    qsort(a, N, sizeof(uint64_t), bench_cmp_u64);
    qsort(b, N, sizeof(uint64_t), bench_cmp_u64);
    *ticks_med    = (double)a[N / 2];
    *clock_ns_med = (double)b[N / 2];
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
/*  Risultati di un caso + scrittura della riga nel file dati         */
/* ------------------------------------------------------------------ */

typedef struct {
    bench_stats_t ns;          /* fase A: tempo per chiamata (ns)                 */
    bench_stats_t ticks;       /* fase A: tick per chiamata                       */
    double batch_ns;           /* fase B: ns medi per hash (senza overhead timer) */
    double batch_ticks;        /* fase B: tick medi per hash                      */
    int    pmu_ok;             /* fase B: 1 se cicli/istruzioni sono validi       */
    double cycles;             /* fase B: cicli core medi per hash (PMU)          */
    double instructions;       /* fase B: istruzioni medie per hash (PMU)         */
    double spread_pct;         /* fase B: (max-min)/mediana dei sotto-batch, %    */
    int    scaled;             /* fase B: 1 se il kernel ha multiplexato la PMU   */
} bench_case_result_t;

/*
 * Scrive UNA riga (separata da spazi) con tutte le misure di un caso.
 * Lo script bash legge questo file per costruire le tabelle di result.txt.
 *
 * Ordine dei 24 campi (IMPORTANTE: deve combaciare con il `read` in bash):
 *
 *   impl  algo  size_byte
 *   ns_mean  ns_median  ns_sd  ns_min  ns_max  ns_removed           (fase A)
 *   tk_mean  tk_median  tk_sd  tk_min  tk_max  tk_removed           (fase A)
 *   throughput_byte_s  throughput_hash_s                            (fase A, media ns)
 *   batch_ns  batch_ticks  pmu_ok  cycles  instructions             (fase B, mediane)
 *   spread_pct  scaled                                              (fase B, qualita')
 *
 * In fase A i valori in ns sono i tick convertiti con la frequenza calibrata.
 */
static inline void bench_write_row(FILE *f, const char *impl, const char *algo,
                                   int msg_size, const bench_case_result_t *r) {
    double bps = 0.0, hps = 0.0;
    if (r->ns.mean > 0.0) {
        bps = (double)msg_size * 1e9 / r->ns.mean;
        hps = 1e9 / r->ns.mean;
    }
    fprintf(f,
        "%s %s %d "
        "%.1f %.1f %.1f %llu %llu %llu "
        "%.1f %.1f %.1f %llu %llu %llu "
        "%.1f %.1f "
        "%.2f %.2f %d %.2f %.2f %.2f %d\n",
        impl, algo, msg_size,
        r->ns.mean, r->ns.median, r->ns.stddev,
        (unsigned long long)r->ns.min, (unsigned long long)r->ns.max,
        (unsigned long long)r->ns.n_removed,
        r->ticks.mean, r->ticks.median, r->ticks.stddev,
        (unsigned long long)r->ticks.min, (unsigned long long)r->ticks.max,
        (unsigned long long)r->ticks.n_removed,
        bps, hps,
        r->batch_ns, r->batch_ticks, r->pmu_ok, r->cycles, r->instructions,
        r->spread_pct, r->scaled);
}

/* Frequenza dei tick, calibrata UNA volta all'avvio (bench_write_calibration)
 * e usata per convertire i tick della fase A in ns. */
static double bench_hz = 0.0;

/* Riga di calibrazione (una per eseguibile), letta dallo script:
 *   CALIB  ticks_hz  overhead_ns  overhead_ticks  pmu_ok  clock_gettime_ns
 * overhead_* = costo della coppia di letture tick della fase A (mediana). */
static inline void bench_write_calibration(FILE *f, const bench_pmu_t *pmu) {
    double ov_tk, clk_ns;
    bench_hz = bench_ticks_hz();
    bench_timer_overhead(&ov_tk, &clk_ns);
    fprintf(f, "CALIB %.0f %.1f %.1f %d %.1f\n",
            bench_hz, bench_hz > 0 ? ov_tk * 1e9 / bench_hz : 0.0, ov_tk, pmu->ok, clk_ns);
}

/* Converte le statistiche in tick in statistiche in ns (scala lineare). */
static inline void bench_stats_ticks_to_ns(const bench_stats_t *tk, bench_stats_t *ns) {
    double k = bench_hz > 0 ? 1e9 / bench_hz : 0.0;
    *ns = *tk;
    ns->mean   = tk->mean   * k;
    ns->median = tk->median * k;
    ns->stddev = tk->stddev * k;
    ns->min    = (uint64_t)((double)tk->min * k + 0.5);
    ns->max    = (uint64_t)((double)tk->max * k + 0.5);
}

/* Mediana di un piccolo array di double (ordinato sul posto, insertion sort). */
static inline double bench_median_d(double *v, int n) {
    for (int i = 1; i < n; i++) {
        double x = v[i]; int j = i - 1;
        while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
        v[j + 1] = x;
    }
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

/* ------------------------------------------------------------------ */
/*  Misura di un caso (macro: la chiamata CALL resta DIRETTA)         */
/* ------------------------------------------------------------------ */
/*
 * E' una macro e non una funzione con puntatore a funzione: cosi' nel loop
 * misurato c'e' esattamente la chiamata diretta a hash(...), identica a quella
 * di un utilizzatore reale (niente chiamate indirette che altererebbero i tempi).
 *
 *   RES    : bench_case_result_t* in cui scrivere i risultati
 *   PMU    : bench_pmu_t*
 *   WARMUP : iterazioni di riscaldamento (scartate)
 *   ITERS  : iterazioni misurate (fase A; fase B = BENCH_SUBBATCHES x ITERS/BENCH_SUBBATCHES)
 *   NS_BUF, TK_BUF : array uint64_t[ITERS] preallocati
 *   CALL   : l'espressione da misurare, es. hash(msg, len, md, 32, 1)
 */
#define BENCH_MEASURE(RES, PMU, WARMUP, ITERS, NS_BUF, TK_BUF, CALL)            \
    do {                                                                        \
        /* WARMUP: stesse chiamate, nessuna misura (cache e predittori caldi) */ \
        for (int _i = 0; _i < (WARMUP); _i++) { CALL; }                         \
                                                                                \
        /* FASE A - per chiamata: solo due letture dei tick attorno a CALL    */\
        for (int _i = 0; _i < (ITERS); _i++) {                                  \
            uint64_t _k0 = bench_now_ticks();                                   \
            CALL;                                                               \
            uint64_t _k1 = bench_now_ticks();                                   \
            (TK_BUF)[_i] = _k1 - _k0;                                           \
        }                                                                       \
        (void)(NS_BUF);                                                         \
        bench_compute_stats((TK_BUF), (ITERS), &(RES)->ticks);                  \
        bench_stats_ticks_to_ns(&(RES)->ticks, &(RES)->ns);                     \
                                                                                \
        /* FASE B - BENCH_SUBBATCHES sotto-batch di chiamate consecutive;      */\
        /* contatori e tempo letti solo prima/dopo ogni sotto-batch.           */\
        double _ns[BENCH_SUBBATCHES], _tk[BENCH_SUBBATCHES];                    \
        double _cy[BENCH_SUBBATCHES], _in[BENCH_SUBBATCHES];                    \
        int _per = (ITERS) / BENCH_SUBBATCHES; if (_per < 1) _per = 1;          \
        (RES)->scaled = 0;                                                      \
        for (int _b = 0; _b < BENCH_SUBBATCHES; _b++) {                         \
            bench_pmu_reading_t _rd;                                            \
            bench_pmu_start(PMU);                                               \
            uint64_t _bt0 = bench_now_ns();                                     \
            uint64_t _bk0 = bench_now_ticks();                                  \
            for (int _i = 0; _i < _per; _i++) { CALL; }                         \
            uint64_t _bk1 = bench_now_ticks();                                  \
            uint64_t _bt1 = bench_now_ns();                                     \
            bench_pmu_stop(PMU, &_rd);                                          \
            _ns[_b] = (double)(_bt1 - _bt0) / (double)_per;                     \
            _tk[_b] = (double)(_bk1 - _bk0) / (double)_per;                     \
            _cy[_b] = (double)_rd.cycles / (double)_per;                        \
            _in[_b] = (double)_rd.instructions / (double)_per;                  \
            (RES)->scaled |= _rd.scaled;                                        \
        }                                                                       \
        (RES)->pmu_ok = (PMU)->ok;                                              \
        /* dispersione su cicli (se PMU) altrimenti su ns: (max-min)/mediana   */\
        {                                                                       \
            double *_q = (PMU)->ok ? _cy : _ns, _mn = _q[0], _mx = _q[0];       \
            for (int _b = 1; _b < BENCH_SUBBATCHES; _b++) {                     \
                if (_q[_b] < _mn) _mn = _q[_b];                                 \
                if (_q[_b] > _mx) _mx = _q[_b];                                 \
            }                                                                   \
            double _md = bench_median_d(_q, BENCH_SUBBATCHES);                  \
            (RES)->spread_pct = _md > 0 ? (_mx - _mn) / _md * 100.0 : 0.0;      \
        }                                                                       \
        (RES)->batch_ns     = bench_median_d(_ns, BENCH_SUBBATCHES);            \
        (RES)->batch_ticks  = bench_median_d(_tk, BENCH_SUBBATCHES);            \
        (RES)->cycles       = bench_median_d(_cy, BENCH_SUBBATCHES);            \
        (RES)->instructions = bench_median_d(_in, BENCH_SUBBATCHES);            \
    } while (0)

#endif /* BENCH_COMMON_H */
