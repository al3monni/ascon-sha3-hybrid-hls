#!/usr/bin/env bash
# =============================================================================
# run.sh - Equivalence test of one variant against golden.txt (gcc only)
#
# Usage   : bash tests/equivalence/run.sh <Hybrid-Test|Sha-Test|Ascon-Test|all>
# Builds  : <variant>/hash.cpp|hash.c + kat_sweep.c  ->  build/tests/<variant>
#           (g++ for the C++ AXI-Stream variants, gcc otherwise)
# Checks  : every (algorithm, len = 0..256) digest of hash_top() == golden.txt
# Exit    : 0 = all equal, 1 = mismatch (first differences printed)
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build/tests"
mkdir -p "$OUT"

# Vitis HLS headers (hls_stream.h, ap_axi_sdata.h, ap_int.h) for the C++ variants
VITIS_INC="${VITIS_INC:-/mnt/c/Xilinx/2025.1.1/Vitis/include}"

check() {
    local v="$1" def algos
    case "$v" in
        Hybrid-Test) def=VARIANT_HYBRID; algos='sha3-256|sha3-512|ascon' ;;
        Sha-Test)    def=VARIANT_SHA;    algos='sha3-256|sha3-512' ;;
        Ascon-Test)  def=VARIANT_ASCON;  algos='ascon' ;;
        *) echo "unknown variant: $v" >&2; return 2 ;;
    esac

    if [ -f "$ROOT/$v/hash.cpp" ]; then
        # C++ variant: everything compiled as C++ (Vitis HLS headers from VITIS_INC)
        g++ -O2 -Wno-unknown-pragmas -D"$def" -I"$ROOT/$v" -I"$VITIS_INC" \
            -o "$OUT/$v" "$ROOT/$v/hash.cpp" -x c++ "$HERE/kat_sweep.c"
    else
        gcc -O2 -Wno-unknown-pragmas -D"$def" -I"$ROOT/$v" \
            -o "$OUT/$v" "$ROOT/$v/hash.c" "$HERE/kat_sweep.c"
    fi

    "$OUT/$v" > "$OUT/$v.txt"
    grep -E "^($algos) " "$HERE/golden.txt" > "$OUT/$v.golden"

    if diff -q "$OUT/$v.golden" "$OUT/$v.txt" > /dev/null; then
        echo "$v: OK ($(wc -l < "$OUT/$v.txt") vectors, algos: ${algos//|/ })"
    else
        echo "$v: FAIL - first differences (< golden, > $v):"
        diff "$OUT/$v.golden" "$OUT/$v.txt" | head -n 12
        return 1
    fi
}

if [ "${1:-}" = "all" ]; then
    rc=0
    for v in Hybrid-Test Sha-Test Ascon-Test; do check "$v" || rc=1; done
    exit $rc
fi
check "${1:?usage: run.sh <Hybrid-Test|Sha-Test|Ascon-Test|all>}"
