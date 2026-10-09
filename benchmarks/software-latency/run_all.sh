#!/usr/bin/env bash
# ============================================================================
# run_all.sh
# ----------
# Compila ed esegue il benchmark sulle tre implementazioni e produce UN SOLO
# file di output leggibile: results/result.txt
#
# Passi:
#   1) compila ogni implementazione con -O2 -march=native
#   2) esegue ognuna: ogni main scrive le sue misure in un file di appoggio
#   3) unisce i file di appoggio e costruisce result.txt (legenda + tabelle)
#   4) cancella i file di appoggio: alla fine resta solo result.txt
#
# Uso:
#   ./run_all.sh           esecuzione normale (100000 iterazioni + 1000 warmup)
#   ./run_all.sh quick     esecuzione rapida   (2000 iterazioni + 200 warmup)
#                          utile solo per verificare al volo che tutto funzioni
# ============================================================================

set -euo pipefail

# ----------------------------------------------------------------------------
# 1. Parametri
# ----------------------------------------------------------------------------
ITERS=100000          # iterazioni misurate per ogni caso
WARMUP=1000           # iterazioni di riscaldamento (scartate)

if [[ "${1:-}" == "quick" ]]; then
    ITERS=2000
    WARMUP=200
    echo "[modalita' quick: $ITERS iterazioni, $WARMUP warmup]"
fi

# Cartella in cui si trova questo script (cosi' funziona da qualunque path).
ROOT="$(cd "$(dirname "$0")" && pwd)"
RESULTS="$ROOT/results"
mkdir -p "$RESULTS"

# Le tre implementazioni: nome -> cartella  (la cartella e' "<nome>-test").
IMPLS=("hybrid" "sha3" "ascon")

# Flag di compilazione:
#   -O2 -march=native  ottimizzazione richiesta
#   -Wno-unused-label  silenzia gli avvisi sulle etichette di loop rimaste
#                      dai vecchi pragma HLS (sono innocue)
CFLAGS="-O2 -march=native -Wno-unused-label"
LDFLAGS="-lm"

# File di appoggio temporanei (uno per implementazione) e file unito.
TMP_PREFIX="$RESULTS/.measure"     # diventera' .measure.hybrid.dat, ecc.
COMBINED="$RESULTS/.combined.dat"
OUT="$RESULTS/result.txt"

# ----------------------------------------------------------------------------
# 2. Compilazione ed esecuzione
# ----------------------------------------------------------------------------
echo
echo "Compilo ed eseguo ($ITERS iterazioni, $WARMUP warmup, -O2)..."

: > "$COMBINED"        # svuoto/azzero il file unito

for impl in "${IMPLS[@]}"; do
    src_dir="$ROOT/${impl}-test"
    bin="$src_dir/bench"
    dat="${TMP_PREFIX}.${impl}.dat"

    # --- compilazione ---
    echo
    echo "--- $impl: compilo ---"
    ( cd "$src_dir" && gcc $CFLAGS -o bench hash.c main.c $LDFLAGS )

    # --- esecuzione: scrive le misure nel file di appoggio dell'impl ---
    echo "--- $impl: eseguo ---"
    "$bin" "$dat" "$ITERS" "$WARMUP"

    # --- accodo le righe al file unito ---
    cat "$dat" >> "$COMBINED"
done

# ----------------------------------------------------------------------------
# 3. Carico tutte le misure in array associativi (chiave: "impl|algo|size")
# ----------------------------------------------------------------------------
# Ogni riga del file unito ha 17 campi (vedi bench_common.h -> bench_write_row):
#   impl algo size
#   ns_mean ns_med ns_sd ns_min ns_max ns_rem
#   cy_mean cy_med cy_sd cy_min cy_max cy_rem
#   bps hps
declare -A NS_MEAN NS_MED NS_SD NS_MIN NS_MAX NS_REM
declare -A CY_MEAN CY_MED CY_SD CY_MIN CY_MAX CY_REM
declare -A BPS HPS

while read -r impl algo size \
             nsm nsmed nssd nsmin nsmax nsrem \
             cym cymed cysd cymin cymax cyrem \
             bps hps; do
    key="${impl}|${algo}|${size}"
    NS_MEAN[$key]=$nsm;  NS_MED[$key]=$nsmed; NS_SD[$key]=$nssd
    NS_MIN[$key]=$nsmin; NS_MAX[$key]=$nsmax; NS_REM[$key]=$nsrem
    CY_MEAN[$key]=$cym;  CY_MED[$key]=$cymed; CY_SD[$key]=$cysd
    CY_MIN[$key]=$cymin; CY_MAX[$key]=$cymax; CY_REM[$key]=$cyrem
    BPS[$key]=$bps;      HPS[$key]=$hps
done < "$COMBINED"

# Dimensioni di input testate (devono combaciare con quelle nei main).
SIZES=(16 64 256 1024 4096)

# ----------------------------------------------------------------------------
# 4. Funzioni di supporto per disegnare le tabelle ASCII
# ----------------------------------------------------------------------------

# rule W1 W2 ... : stampa una riga di bordo "+------+----+..." con segmenti
#                  larghi (Wi + 2) trattini, per allinearsi alle celle.
#                  I 2 spazi iniziali allineano il bordo con le righe (che
#                  iniziano anch'esse con "  | ").
rule() {
    local out="  +"
    local w
    for w in "$@"; do
        out+=$(printf '%*s' "$((w + 2))" '' | tr ' ' '-')
        out+="+"
    done
    printf '%s\n' "$out"
}

# pct H S : percentuale di variazione di H rispetto a S, cioe' (H-S)/S*100.
#           Usata solo come calcolatrice per una singola percentuale.
#           Valore negativo  -> hybrid piu' veloce della singola.
pct() {
    awk -v h="$1" -v s="$2" 'BEGIN { if (s>0) printf "%+.2f%%", (h-s)/s*100; else printf "n/a"; }'
}

# frac_pct R T : percentuale R/T*100 (campioni scartati sul totale).
frac_pct() {
    awk -v r="$1" -v t="$2" 'BEGIN { if (t>0) printf "%.2f", r/t*100; else printf "0"; }'
}

# Tabella TEMPO o CICLI. Argomenti: ALGO  SINGLE_IMPL  WHICH(ns|cy)
# Mette una riga "hybrid" e una riga della singola per ogni dimensione.
# La colonna out% indica la percentuale di campioni scartati come outlier.
print_stat_table() {
    local algo="$1" single="$2" which="$3"
    local -n MEAN MED SD MIN MAX REM
    if [[ "$which" == "ns" ]]; then
        MEAN=NS_MEAN; MED=NS_MED; SD=NS_SD; MIN=NS_MIN; MAX=NS_MAX; REM=NS_REM
        echo "  TEMPO (ns)"
    else
        MEAN=CY_MEAN; MED=CY_MED; SD=CY_SD; MIN=CY_MIN; MAX=CY_MAX; REM=CY_REM
        echo "  CICLI CPU"
    fi

    rule 7 6 11 11 10 10 11 6
    printf "  | %-7s | %6s | %11s | %11s | %10s | %10s | %11s | %6s |\n" \
           "impl" "input" "mean" "median" "sd" "min" "max" "out%"
    rule 7 6 11 11 10 10 11 6

    local size kh ks oph ops
    for size in "${SIZES[@]}"; do
        kh="hybrid|${algo}|${size}"
        ks="${single}|${algo}|${size}"
        oph=$(frac_pct "${REM[$kh]}" "$ITERS")   # % scartati riga hybrid
        ops=$(frac_pct "${REM[$ks]}" "$ITERS")   # % scartati riga singola
        printf "  | %-7s | %4d B | %11.1f | %11.1f | %10.1f | %10.0f | %11.0f | %5.2f%% |\n" \
               "hybrid" "$size" "${MEAN[$kh]}" "${MED[$kh]}" "${SD[$kh]}" "${MIN[$kh]}" "${MAX[$kh]}" "$oph"
        printf "  | %-7s | %4d B | %11.1f | %11.1f | %10.1f | %10.0f | %11.0f | %5.2f%% |\n" \
               "$single" "$size" "${MEAN[$ks]}" "${MED[$ks]}" "${SD[$ks]}" "${MIN[$ks]}" "${MAX[$ks]}" "$ops"
        rule 7 6 11 11 10 10 11 6
    done
    echo
}

# Tabella THROUGHPUT. Argomenti: ALGO  SINGLE_IMPL
print_throughput_table() {
    local algo="$1" single="$2"
    echo "  THROUGHPUT"
    rule 7 6 16 14
    printf "  | %-7s | %6s | %16s | %14s |\n" "impl" "input" "byte/s" "hash/s"
    rule 7 6 16 14

    local size kh ks
    for size in "${SIZES[@]}"; do
        kh="hybrid|${algo}|${size}"
        ks="${single}|${algo}|${size}"
        printf "  | %-7s | %4d B | %16.0f | %14.0f |\n" \
               "hybrid" "$size" "${BPS[$kh]}" "${HPS[$kh]}"
        printf "  | %-7s | %4d B | %16.0f | %14.0f |\n" \
               "$single" "$size" "${BPS[$ks]}" "${HPS[$ks]}"
        rule 7 6 16 14
    done
    echo
}

# Tabella DIFFERENZE: variazione % di hybrid rispetto alla singola.
# Argomenti: ALGO  SINGLE_IMPL
print_diff_table() {
    local algo="$1" single="$2"
    echo "  DIFFERENZE (hybrid rispetto a ${single}; negativo = hybrid piu' veloce)"
    rule 6 12 12
    printf "  | %6s | %12s | %12s |\n" "input" "delta tempo" "delta cicli"
    rule 6 12 12

    local size kh ks dns dcy
    for size in "${SIZES[@]}"; do
        kh="hybrid|${algo}|${size}"
        ks="${single}|${algo}|${size}"
        dns=$(pct "${NS_MEAN[$kh]}" "${NS_MEAN[$ks]}")
        dcy=$(pct "${CY_MEAN[$kh]}" "${CY_MEAN[$ks]}")
        printf "  | %4d B | %12s | %12s |\n" "$size" "$dns" "$dcy"
    done
    rule 6 12 12
    echo
}

# Stampa l'intero blocco di confronto per un algoritmo.
# Argomenti: TITOLO  ALGO  SINGLE_IMPL
print_algo_block() {
    local title="$1" algo="$2" single="$3"
    echo "============================================================================"
    echo "  $title"
    echo "============================================================================"
    echo
    print_stat_table       "$algo" "$single" "ns"
    print_stat_table       "$algo" "$single" "cy"
    print_throughput_table "$algo" "$single"
    print_diff_table       "$algo" "$single"
}

# ----------------------------------------------------------------------------
# 5. Scrivo result.txt
# ----------------------------------------------------------------------------
{
    echo "############################################################################"
    echo "#  CONFRONTO DI LATENZA SOFTWARE - implementazione ibrida vs singole"
    echo "############################################################################"
    echo
    echo "Configurazione:"
    echo "  - Compilazione      : -O2 -march=native"
    echo "  - Iterazioni        : $ITERS misurate + $WARMUP warmup (scartate)"
    echo "  - Outlier           : rimossi con regola IQR (recinto superiore"
    echo "                        Q3 + 1.5*(Q3-Q1)); il minimo non viene toccato"
    echo "  - Tempo             : clock_gettime(CLOCK_MONOTONIC)"
    echo "  - Cicli             : __rdtsc()"
    echo "  - Input sintetici   : 16, 64, 256, 1024, 4096 byte (deterministici)"
    echo "  - Correttezza       : verificata sui vettori KAT prima delle misure"
    echo
    echo "----------------------------------------------------------------------------"
    echo "LEGENDA"
    echo "----------------------------------------------------------------------------"
    echo "  ns          nanosecondi (tempo di UNA singola esecuzione di hash)"
    echo "  cicli       cicli di CPU (contati con rdtsc)"
    echo "  mean        media aritmetica            (sui campioni tenuti)"
    echo "  median      mediana (valore centrale)   (sui campioni tenuti)"
    echo "  sd          deviazione standard         (sui campioni tenuti)"
    echo "  min         minimo: coincide col minimo grezzo (non tagliamo in basso)"
    echo "  max         massimo NON-outlier (il piu' grande sopravvissuto al taglio)"
    echo "  out%        percentuale di campioni scartati come outlier (regola IQR)"
    echo "  byte/s      throughput in byte al secondo  ( = dimensione / tempo medio )"
    echo "  hash/s      throughput in hash al secondo  ( = 1 / tempo medio )"
    echo "  delta tempo variazione % della media dei tempi: (hybrid - singola)/singola"
    echo "              valore NEGATIVO = hybrid piu' veloce della singola"
    echo "  delta cicli come 'delta tempo' ma calcolato sui cicli di CPU"
    echo
    echo "Tutte le statistiche sono calcolate DOPO la rimozione degli outlier."
    echo "In ogni tabella la riga 'hybrid' e la riga della singola sono affiancate,"
    echo "per ogni dimensione di input, cosi' da leggere subito le differenze."
    echo

    print_algo_block "SHA3-256   -   hybrid  vs  sha3-test"  "SHA3-256" "sha3"
    print_algo_block "SHA3-512   -   hybrid  vs  sha3-test"  "SHA3-512" "sha3"
    print_algo_block "Ascon      -   hybrid  vs  ascon-test" "Ascon"    "ascon"

} > "$OUT"

# ----------------------------------------------------------------------------
# 6. Pulizia: resta solo result.txt
# ----------------------------------------------------------------------------
rm -f "$COMBINED" "${TMP_PREFIX}".*.dat

echo
echo "============================================================================"
echo "  FATTO. Risultato in:  $OUT"
echo "============================================================================"
