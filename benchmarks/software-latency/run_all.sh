#!/usr/bin/env bash
# ============================================================================
# run_all.sh
# ----------
# Compila ed esegue il benchmark di latenza software sulle tre implementazioni
# (hybrid, sha3, ascon) e produce un report leggibile per la piattaforma su cui
# gira:  results/<piattaforma>/result.txt   (+ data.tsv con i dati grezzi)
#
# Il codice misurato e' compilato DIRETTAMENTE dai sorgenti del progetto Vitis:
#   hybrid -> ../../Hybrid-Test/hash.c     sha3 -> ../../Sha-Test/hash.c
#   ascon  -> ../../Ascon-Test/hash.c      (le pragma HLS sono ignorate da gcc)
#
# Funziona su x86_64 (Ubuntu nativo, WSL) e aarch64 (Kria KV260, Ubuntu).
#
# Passi:
#   1) rileva piattaforma, flag di compilazione e disponibilita' della PMU
#   2) compila ogni implementazione in build/<impl>/ e ne misura il footprint
#      statico (contesto, stack, codice)
#   3) esegue ogni benchmark fissato su un core (taskset): scrive le misure in
#      un file di appoggio
#   4) costruisce result.txt (piattaforma + legenda + tabelle) e data.tsv
#
# Uso:
#   ./run_all.sh           esecuzione normale (10000 iterazioni + 1000 warmup)
#   ./run_all.sh quick     esecuzione rapida   (1000 iterazioni + 100 warmup)
#
#   sudo ./isolate.sh run  esecuzione su dispositivo isolato (consigliato su Linux nativo)
#
#   PLATFORM=<nome> ./run_all.sh   forza il nome della cartella dei risultati
#                                  (default: <arch>-<native|wsl>)
#
# Contatori hardware (cicli core reali, istruzioni): servono permessi perf.
#   sudo sysctl -w kernel.perf_event_paranoid=2      (valido fino al riavvio)
# Senza, il benchmark gira comunque e le colonne PMU risultano "n/a".
# ============================================================================

set -euo pipefail

# ----------------------------------------------------------------------------
# 1. Parametri e piattaforma
# ----------------------------------------------------------------------------
ITERS=10000           # iterazioni misurate per ogni caso (per ciascuna fase)
WARMUP=1000           # iterazioni di riscaldamento (scartate)

if [[ "${1:-}" == "quick" ]]; then
    ITERS=1000
    WARMUP=100
    echo "[modalita' quick: $ITERS iterazioni, $WARMUP warmup]"
fi

# Cartella in cui si trova questo script (cosi' funziona da qualunque path).
ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$ROOT/../.." && pwd)"

ARCH="$(uname -m)"
if grep -qi microsoft /proc/version 2>/dev/null; then ENVTYPE="wsl"; else ENVTYPE="native"; fi
PLATFORM="${PLATFORM:-${ARCH}-${ENVTYPE}}"

RESULTS="$ROOT/results/$PLATFORM"
BUILD="$ROOT/build"
mkdir -p "$RESULTS" "$BUILD"

# Flag di compilazione: -O2 + ottimizzazione per la CPU su cui si esegue.
#   x86_64  : -march=native
#   aarch64 : -mcpu=native  (forma raccomandata da gcc per ARM)
case "$ARCH" in
    aarch64|arm64) CPUFLAG="-mcpu=native" ;;
    *)             CPUFLAG="-march=native" ;;
esac
CFLAGS="-O2 $CPUFLAG -Wno-unknown-pragmas -Wno-unused-label -Wno-unused-variable"
LDFLAGS="-lm"

# Le tre implementazioni: nome -> componente Vitis da cui prendere hash.c/.h
IMPLS=("hybrid" "sha3" "ascon")
declare -A SRC=( [hybrid]="$REPO/Hybrid-Test" [sha3]="$REPO/Sha-Test" [ascon]="$REPO/Ascon-Test" )

# Core su cui fissare il processo: l'ultimo (il core 0 gestisce di solito
# la maggior parte degli interrupt). Se taskset non c'e', nessun pinning.
# Si sceglie la CPU logica ONLINE con indice piu' alto: con SMT spento le CPU
# dispari possono essere offline, quindi non basta usare nproc-1.
NCPU="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc)"   # CPU online (non solo quelle concesse a questo processo)
last_online_cpu() {
    local list max=0 part a b
    list="$(cat /sys/devices/system/cpu/online 2>/dev/null || echo 0)"   # es. "0-7" o "0,2,4,6"
    IFS=',' read -ra parts <<< "$list"
    for part in "${parts[@]}"; do
        a="${part%-*}"; b="${part#*-}"
        (( b > max )) && max=$b
        (( a > max )) && max=$a
    done
    echo "$max"
}
CORE="${BENCH_CORE:-$(last_online_cpu)}"   # BENCH_CORE e' impostato da isolate.sh
if command -v taskset >/dev/null 2>&1; then PIN="taskset -c $CORE"; else PIN=""; CORE="-"; fi

# Informazioni sulla piattaforma (solo lettura, per l'intestazione del report)
cpu_model() {
    local m=""
    if command -v lscpu >/dev/null 2>&1; then
        m="$(lscpu | awk -F: '/^Model name/ {gsub(/^ +/,"",$2); print $2; exit}')"
    fi
    [[ -z "$m" ]] && m="$(awk -F: '/model name|^Processor/ {gsub(/^ +/,"",$2); print $2; exit}' /proc/cpuinfo 2>/dev/null)"
    echo "${m:-sconosciuto}"
}
read_sys() { cat "$1" 2>/dev/null || echo "n/d"; }
CPU_MODEL="$(cpu_model)"
KERNEL="$(uname -r)"
OS_NAME="$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || echo "n/d")"
GCC_VER="$(gcc --version | head -1)"
GOVERNOR="$(read_sys /sys/devices/system/cpu/cpu${CORE/-/0}/cpufreq/scaling_governor)"
FREQ_MAX="$(read_sys /sys/devices/system/cpu/cpu${CORE/-/0}/cpufreq/cpuinfo_max_freq)"
PARANOID="$(read_sys /proc/sys/kernel/perf_event_paranoid)"
CLOCKSRC="$(read_sys /sys/devices/system/clocksource/clocksource0/current_clocksource)"
SMT="$(read_sys /sys/devices/system/cpu/smt/control)"
# CPU logiche che condividono il core fisico con quella misurata (SMT)
SIBLINGS="$(read_sys /sys/devices/system/cpu/cpu${CORE/-/0}/topology/thread_siblings_list)"

# File di appoggio
TMP_PREFIX="$RESULTS/.measure"
COMBINED="$RESULTS/.combined.dat"
OUT="$RESULTS/result.txt"
TSV="$RESULTS/data.tsv"

# ----------------------------------------------------------------------------
# 2. Compilazione + footprint statico
# ----------------------------------------------------------------------------
# Footprint (dati "a compile time", nessun impatto sulle misure):
#   ctx    = sizeof(hash_ctx_t)                       (stampato da ./bench --sizeof)
#   stack  = stima del caso peggiore della catena di chiamate di hash():
#              hash + max(init, update, final) + keccakf_asconp12 + max(foglie)
#            dai frame di -fstack-usage (le funzioni inlinate da gcc non hanno
#            frame proprio e contano 0)
#   text   = byte di codice macchina di hash.o (.text*)
#   rodata = byte di costanti di hash.o (.rodata*, es. tabelle delle costanti)
declare -A F_CTX F_STACK F_TEXT F_RODATA

su_of() {   # su_of FILE FUNC -> byte di stack della funzione (0 se assente)
    awk -F'\t' -v f="$2" '{ split($1,a,":"); if (a[length(a)]==f) { print $2; found=1; exit } }
                          END { if (!found) print 0 }' "$1"
}

echo
echo "Piattaforma: $PLATFORM  ($CPU_MODEL)"
echo "Compilo ($CFLAGS)..."

for impl in "${IMPLS[@]}"; do
    b="$BUILD/$impl"; mkdir -p "$b"
    src="${SRC[$impl]}"
    gcc $CFLAGS -fstack-usage -I"$src" -c "$src/hash.c"            -o "$b/hash.o"
    gcc $CFLAGS               -I"$src" -c "$ROOT/${impl}-test/main.c" -o "$b/main.o"
    gcc $CFLAGS -o "$b/bench" "$b/main.o" "$b/hash.o" $LDFLAGS

    su="$b/hash.su"
    s_hash=$(su_of "$su" hash)
    s_mid=$(printf '%s\n' "$(su_of "$su" hash_init)" "$(su_of "$su" hash_update)" "$(su_of "$su" hash_final)" | sort -n | tail -1)
    s_perm=$(su_of "$su" keccakf_asconp12)
    s_leaf=$(printf '%s\n' 0 "$(su_of "$su" theta)" "$(su_of "$su" rho_pi)" "$(su_of "$su" chi)" "$(su_of "$su" iota)" "$(su_of "$su" diffusion)" | sort -n | tail -1)
    F_STACK[$impl]=$((s_hash + s_mid + s_perm + s_leaf))
    F_TEXT[$impl]=$(size -A "$b/hash.o"   | awk '$1 ~ /^\.text/   {s+=$2} END {print s+0}')
    F_RODATA[$impl]=$(size -A "$b/hash.o" | awk '$1 ~ /^\.rodata/ {s+=$2} END {print s+0}')
    F_CTX[$impl]=$("$b/bench" --sizeof)
    echo "  $impl: ok  (ctx ${F_CTX[$impl]} B, stack ~${F_STACK[$impl]} B, text ${F_TEXT[$impl]} B)"
done

# ----------------------------------------------------------------------------
# 3. Esecuzione
# ----------------------------------------------------------------------------
echo
echo "Eseguo ($ITERS iterazioni x 2 fasi, $WARMUP warmup, core $CORE)..."
: > "$COMBINED"
for impl in "${IMPLS[@]}"; do
    dat="${TMP_PREFIX}.${impl}.dat"
    echo
    echo "--- $impl ---"
    $PIN "$BUILD/$impl/bench" "$dat" "$ITERS" "$WARMUP"
    cat "$dat" >> "$COMBINED"
done

# ----------------------------------------------------------------------------
# 4. Carico le misure (chiave: "impl|algo|size")
# ----------------------------------------------------------------------------
# Riga CALIB (una per eseguibile): ticks_hz overhead_ns overhead_ticks pmu_ok
read -r _ TICKS_HZ OV_NS OV_TK PMU_OK CLK_NS < <(grep '^CALIB' "$COMBINED" | head -1)

# Righe di misura: 24 campi (vedi bench_common.h -> bench_write_row)
declare -A NS_MEAN NS_MED NS_SD NS_MIN NS_MAX NS_REM
declare -A TK_MEAN TK_MED TK_SD TK_MIN TK_MAX TK_REM
declare -A BPS HPS B_NS B_TK P_OK CYC INS SPR SCL

while read -r impl algo size \
             nsm nsmed nssd nsmin nsmax nsrem \
             tkm tkmed tksd tkmin tkmax tkrem \
             bps hps bns btk pok cyc ins spr scl; do
    key="${impl}|${algo}|${size}"
    NS_MEAN[$key]=$nsm;  NS_MED[$key]=$nsmed; NS_SD[$key]=$nssd
    NS_MIN[$key]=$nsmin; NS_MAX[$key]=$nsmax; NS_REM[$key]=$nsrem
    TK_MEAN[$key]=$tkm;  TK_MED[$key]=$tkmed; TK_SD[$key]=$tksd
    TK_MIN[$key]=$tkmin; TK_MAX[$key]=$tkmax; TK_REM[$key]=$tkrem
    BPS[$key]=$bps;      HPS[$key]=$hps
    B_NS[$key]=$bns;     B_TK[$key]=$btk;     P_OK[$key]=$pok
    CYC[$key]=$cyc;      INS[$key]=$ins
    SPR[$key]=$spr;      SCL[$key]=$scl
done < <(grep -v '^CALIB' "$COMBINED")

SIZES=(16 64 256 1024 4096)

# Etichetta dei tick a seconda dell'architettura
case "$ARCH" in
    x86_64|i?86)   TICK_SRC="rdtsc (TSC: frequenza costante di riferimento, non i cicli reali del core)" ;;
    aarch64|arm64) TICK_SRC="cntvct_el0 (generic timer ARM)" ;;
    *)             TICK_SRC="n/d" ;;
esac
TICK_MHZ=$(awk -v h="$TICKS_HZ" 'BEGIN { printf "%.1f", h/1e6 }')
if [[ "$PMU_OK" == "1" ]]; then
    PMU_TXT="disponibile (perf_event, solo user-space)"
else
    PMU_TXT="NON disponibile (perf_event_paranoid=$PARANOID o PMU non esposta, es. WSL) -> colonne PMU = n/a"
fi

# Avvisi sulla qualita' della misura (riportati nella testata del report)
WARNINGS=()
ISOLATION="${BENCH_ISOLATION:-nessuno}"
[[ -z "${BENCH_ISOLATION:-}" && "$ENVTYPE" == "native" ]] && \
    WARNINGS+=("dispositivo NON isolato: altri processi possono girare sul core di misura. Consigliato: sudo ./isolate.sh run")
awk -v c="$CLK_NS" 'BEGIN { exit !(c > 200) }' && \
    WARNINGS+=("clock_gettime lenta (${CLK_NS} ns, clocksource=$CLOCKSRC): non influisce sulla fase A (solo tick), ma indica un sistema con timer lento")
[[ "$SMT" == "on" && "$SIBLINGS" == *[,-]* ]] && \
    WARNINGS+=("SMT attivo: la CPU $CORE condivide il core fisico con [$SIBLINGS]; un carico sul thread fratello altera i cicli. Consigliato: echo off | sudo tee /sys/devices/system/cpu/smt/control")
[[ "$GOVERNOR" != "performance" && "$GOVERNOR" != "n/d" ]] && \
    WARNINGS+=("governor '$GOVERNOR': la frequenza varia, quindi i tempi in ns variano (i cicli PMU no). Consigliato: governor performance")
grep -v '^CALIB' "$COMBINED" | awk '$24 == 1 { f=1 } END { exit !f }' && \
    WARNINGS+=("il kernel ha multiplexato i contatori PMU in almeno un caso: cicli/istruzioni stimati")
grep -v '^CALIB' "$COMBINED" | awk '$23 > 5 { f=1 } END { exit !f }' && \
    WARNINGS+=("dispersione > 5% fra i sotto-batch della fase B in almeno un caso (vedi colonna disp%): misura disturbata")

# ----------------------------------------------------------------------------
# 5. Funzioni di supporto per disegnare le tabelle ASCII
# ----------------------------------------------------------------------------
rule() {
    local out="  +" w
    for w in "$@"; do
        out+=$(printf '%*s' "$((w + 2))" '' | tr ' ' '-')
        out+="+"
    done
    printf '%s\n' "$out"
}

# pct H S : variazione % di H rispetto a S. Negativo -> hybrid migliore.
pct() {
    awk -v h="$1" -v s="$2" 'BEGIN { if (s>0) printf "%+.2f%%", (h-s)/s*100; else printf "n/a"; }'
}
frac_pct() {
    awk -v r="$1" -v t="$2" 'BEGIN { if (t>0) printf "%.2f", r/t*100; else printf "0"; }'
}
# num V FMT : stampa V con formato FMT, oppure "n/a" se la PMU non e' valida
pmu_num() {
    local ok="$1" v="$2" fmt="$3"
    if [[ "$ok" == "1" ]]; then awk -v v="$v" -v f="$fmt" 'BEGIN { printf f, v }'; else printf "n/a"; fi
}
div() { awk -v a="$1" -v b="$2" -v f="$3" 'BEGIN { if (b>0) printf f, a/b; else printf "n/a" }'; }

# Distribuzione per chiamata (fase A): tempo ns o tick
print_stat_table() {
    local algo="$1" single="$2" which="$3"
    local -n MEAN MED SD MIN MAX REM
    if [[ "$which" == "ns" ]]; then
        MEAN=NS_MEAN; MED=NS_MED; SD=NS_SD; MIN=NS_MIN; MAX=NS_MAX; REM=NS_REM
        echo "  TEMPO PER CHIAMATA (ns, dai tick) - fase A"
    else
        MEAN=TK_MEAN; MED=TK_MED; SD=TK_SD; MIN=TK_MIN; MAX=TK_MAX; REM=TK_REM
        echo "  TICK PER CHIAMATA (${TICK_MHZ} MHz) - fase A"
    fi
    rule 7 6 11 11 10 10 11 6
    printf "  | %-7s | %6s | %11s | %11s | %10s | %10s | %11s | %6s |\n" \
           "impl" "input" "mean" "median" "sd" "min" "max" "out%"
    rule 7 6 11 11 10 10 11 6
    local size k im
    for size in "${SIZES[@]}"; do
        for im in hybrid "$single"; do
            k="${im}|${algo}|${size}"
            printf "  | %-7s | %4d B | %11.1f | %11.1f | %10.1f | %10.0f | %11.0f | %5.2f%% |\n" \
                   "$im" "$size" "${MEAN[$k]}" "${MED[$k]}" "${SD[$k]}" "${MIN[$k]}" "${MAX[$k]}" \
                   "$(frac_pct "${REM[$k]}" "$ITERS")"
        done
        rule 7 6 11 11 10 10 11 6
    done
    echo
}

# Contatori hardware e valori medi puliti (fase B)
print_pmu_table() {
    local algo="$1" single="$2"
    echo "  CONTATORI HARDWARE - fase B (mediana dei sotto-batch, valori per hash)"
    rule 7 6 10 12 12 6 9 6
    printf "  | %-7s | %6s | %10s | %12s | %12s | %6s | %9s | %6s |\n" \
           "impl" "input" "ns/hash" "cicli/hash" "istr/hash" "IPC" "cpb" "disp%"
    rule 7 6 10 12 12 6 9 6
    local size k im ok
    for size in "${SIZES[@]}"; do
        for im in hybrid "$single"; do
            k="${im}|${algo}|${size}"; ok="${P_OK[$k]}"
            printf "  | %-7s | %4d B | %10.1f | %12s | %12s | %6s | %9s | %5.1f%% |\n" \
                   "$im" "$size" "${B_NS[$k]}" \
                   "$(pmu_num "$ok" "${CYC[$k]}" "%.0f")" \
                   "$(pmu_num "$ok" "${INS[$k]}" "%.0f")" \
                   "$( [[ "$ok" == "1" ]] && div "${INS[$k]}" "${CYC[$k]}" "%.2f" || echo n/a)" \
                   "$( [[ "$ok" == "1" ]] && div "${CYC[$k]}" "$size" "%.1f" || echo n/a)" \
                   "${SPR[$k]}"
        done
        rule 7 6 10 12 12 6 9 6
    done
    echo
}

print_throughput_table() {
    local algo="$1" single="$2"
    echo "  THROUGHPUT (dalla media della fase A)"
    rule 7 6 16 14
    printf "  | %-7s | %6s | %16s | %14s |\n" "impl" "input" "byte/s" "hash/s"
    rule 7 6 16 14
    local size k im
    for size in "${SIZES[@]}"; do
        for im in hybrid "$single"; do
            k="${im}|${algo}|${size}"
            printf "  | %-7s | %4d B | %16.0f | %14.0f |\n" "$im" "$size" "${BPS[$k]}" "${HPS[$k]}"
        done
        rule 7 6 16 14
    done
    echo
}

print_diff_table() {
    local algo="$1" single="$2"
    echo "  DIFFERENZE (hybrid rispetto a ${single}; negativo = hybrid migliore)"
    rule 6 12 12 12 12
    printf "  | %6s | %12s | %12s | %12s | %12s |\n" "input" "d tempo (A)" "d tick (A)" "d tempo (B)" "d cicli (B)"
    rule 6 12 12 12 12
    local size kh ks dcy
    for size in "${SIZES[@]}"; do
        kh="hybrid|${algo}|${size}"; ks="${single}|${algo}|${size}"
        if [[ "${P_OK[$kh]}" == "1" && "${P_OK[$ks]}" == "1" ]]; then dcy=$(pct "${CYC[$kh]}" "${CYC[$ks]}"); else dcy="n/a"; fi
        printf "  | %4d B | %12s | %12s | %12s | %12s |\n" "$size" \
               "$(pct "${NS_MEAN[$kh]}" "${NS_MEAN[$ks]}")" "$(pct "${TK_MEAN[$kh]}" "${TK_MEAN[$ks]}")" \
               "$(pct "${B_NS[$kh]}" "${B_NS[$ks]}")" "$dcy"
    done
    rule 6 12 12 12 12
    echo
}

print_algo_block() {
    local title="$1" algo="$2" single="$3"
    echo "============================================================================"
    echo "  $title"
    echo "============================================================================"
    echo
    print_stat_table       "$algo" "$single" "ns"
    print_stat_table       "$algo" "$single" "tk"
    print_pmu_table        "$algo" "$single"
    print_throughput_table "$algo" "$single"
    print_diff_table       "$algo" "$single"
}

print_footprint() {
    echo "============================================================================"
    echo "  FOOTPRINT STATICO (equivalente software dell'area)"
    echo "============================================================================"
    echo
    rule 13 9 11 10 10 11
    printf "  | %-13s | %9s | %11s | %10s | %10s | %11s |\n" "impl" "ctx (B)" "stack (B)" ".text (B)" ".rodata(B)" "codice tot"
    rule 13 9 11 10 10 11
    local im
    for im in hybrid sha3 ascon; do
        printf "  | %-13s | %9d | %11d | %10d | %10d | %11d |\n" "$im" "${F_CTX[$im]}" "${F_STACK[$im]}" \
               "${F_TEXT[$im]}" "${F_RODATA[$im]}" "$(( F_TEXT[$im] + F_RODATA[$im] ))"
    done
    rule 13 9 11 10 10 11
    local st=$(( F_TEXT[sha3] + F_TEXT[ascon] )) sr=$(( F_RODATA[sha3] + F_RODATA[ascon] ))
    printf "  | %-13s | %9s | %11s | %10d | %10d | %11d |\n" "sha3 + ascon" "-" "-" "$st" "$sr" "$(( st + sr ))"
    rule 13 9 11 10 10 11
    echo
    echo "  Codice hybrid rispetto a sha3+ascon: $(pct "$(( F_TEXT[hybrid] + F_RODATA[hybrid] ))" "$(( st + sr ))")"
    echo "  (negativo = l'ibrido occupa meno memoria di codice delle due implementazioni insieme)"
    echo
}

# ----------------------------------------------------------------------------
# 6. Scrivo result.txt
# ----------------------------------------------------------------------------
{
    echo "############################################################################"
    echo "#  CONFRONTO DI LATENZA SOFTWARE - implementazione ibrida vs singole"
    echo "############################################################################"
    echo
    echo "Piattaforma: $PLATFORM"
    echo "  - CPU               : $CPU_MODEL ($NCPU core), misura fissata sul core $CORE"
    echo "  - Sistema           : $OS_NAME, kernel $KERNEL"
    echo "  - Governor / f max  : $GOVERNOR / $FREQ_MAX kHz"
    echo "  - SMT / fratelli    : $SMT / CPU [$SIBLINGS]"
    echo "  - Isolamento        : $ISOLATION"
    echo "  - Clocksource       : $CLOCKSRC (clock_gettime: ${CLK_NS} ns)"
    echo "  - Compilatore       : $GCC_VER"
    echo "  - Flag              : $CFLAGS"
    echo "  - Data              : $(date '+%Y-%m-%d %H:%M')"
    echo
    echo "Cosa viene misurato (regione cronometrata):"
    echo "  UNA chiamata a  hash(msg, len, md, mdlen[, mode])  =  hash_init + hash_update"
    echo "  + hash_final, compilata dai sorgenti Vitis (*-Test/hash.c). Input gia' in"
    echo "  memoria e caldo in cache (warmup). Esclusi: generazione input, KAT, I/O."
    echo
    echo "Configurazione:"
    echo "  - Iterazioni        : $ITERS (fase A) + $ITERS (fase B) per caso, $WARMUP warmup"
    echo "  - Fase A            : per chiamata, due letture dei tick -> distribuzione;"
    echo "                        ns = tick / f_tick (nessuna clock_gettime nel loop)"
    echo "  - Fase B            : 10 sotto-batch di chiamate consecutive, contatori letti"
    echo "                        solo prima/dopo ciascuno -> MEDIANA dei sotto-batch"
    echo "  - Tempo fase B      : clock_gettime(CLOCK_MONOTONIC), fuori dai loop"
    echo "  - Tick              : $TICK_SRC, ${TICK_MHZ} MHz"
    echo "  - Overhead fase A   : ${OV_TK} tick = ${OV_NS} ns per misura (mediana, NON sottratto)"
    echo "  - PMU               : $PMU_TXT"
    echo "  - Outlier (fase A)  : rimossi con regola IQR (recinto superiore Q3 + 1.5*IQR)"
    echo "  - Input sintetici   : 16, 64, 256, 1024, 4096 byte (deterministici)"
    echo "  - Correttezza       : verificata sui vettori KAT prima delle misure"
    echo
    if (( ${#WARNINGS[@]} )); then
        echo "AVVISI SULLA QUALITA' DELLA MISURA:"
        for w in "${WARNINGS[@]}"; do echo "  ! $w"; done
    else
        echo "Nessun avviso sulla qualita' della misura."
    fi
    echo
    echo "----------------------------------------------------------------------------"
    echo "LEGENDA"
    echo "----------------------------------------------------------------------------"
    echo "  ns          nanosecondi"
    echo "  tick        unita' del contatore a basso costo (vedi 'Tick' sopra)"
    echo "  mean/median/sd/min/max  statistiche sui campioni tenuti (fase A)"
    echo "  out%        percentuale di campioni scartati come outlier (regola IQR)"
    echo "  ns/hash     tempo per hash nella fase B (mediana dei sotto-batch)"
    echo "  disp%       (max - min) / mediana dei 10 sotto-batch (cicli, o ns senza PMU)"
    echo "  cicli/hash  cicli REALI del core per hash (PMU, solo codice utente)"
    echo "  istr/hash   istruzioni eseguite per hash (PMU)"
    echo "  IPC         istruzioni per ciclo = istr/hash / cicli/hash"
    echo "  cpb         cicli per byte = cicli/hash / dimensione input"
    echo "              (metrica standard dei benchmark crittografici, es. eBACS)"
    echo "  byte/s      dimensione / tempo medio (fase A);  hash/s = 1 / tempo medio"
    echo "  d ...       variazione %: (hybrid - singola)/singola; negativo = hybrid migliore"
    echo
    print_algo_block "SHA3-256   -   hybrid  vs  sha3"   "SHA3-256" "sha3"
    print_algo_block "SHA3-512   -   hybrid  vs  sha3"   "SHA3-512" "sha3"
    print_algo_block "Ascon      -   hybrid  vs  ascon"  "Ascon"    "ascon"
    print_footprint
    echo "  ctx        sizeof(hash_ctx_t): stato + contatori del contesto di hash"
    echo "  stack      stima del caso peggiore della catena hash -> init/update/final"
    echo "             -> keccakf_asconp12 -> funzioni foglia (da gcc -fstack-usage)"
    echo "  .text      codice macchina di hash.o (core + wrapper hash_top)"
    echo "  .rodata    costanti di hash.o (tabelle delle costanti di round, ecc.)"
} > "$OUT"

# data.tsv: dati grezzi per grafici (una riga per caso, con intestazione)
{
    printf "platform\timpl\talgo\tsize\tns_mean\tns_median\tns_sd\tns_min\tns_max\tns_removed\t"
    printf "tk_mean\ttk_median\ttk_sd\ttk_min\ttk_max\ttk_removed\tbyte_s\thash_s\t"
    printf "batch_ns\tbatch_ticks\tpmu_ok\tcycles\tinstructions\tspread_pct\tpmu_scaled\n"
    grep -v '^CALIB' "$COMBINED" | awk -v p="$PLATFORM" 'BEGIN{OFS="\t"} {$1=$1; print p, $0}' | tr ' ' '\t'
} > "$TSV"

rm -f "$COMBINED" "${TMP_PREFIX}".*.dat

echo
echo "============================================================================"
echo "  FATTO. Risultati in:  $OUT"
echo "                        $TSV"
echo "============================================================================"
