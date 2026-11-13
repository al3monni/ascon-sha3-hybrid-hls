#!/bin/bash
# =============================================================================
# test_all.sh - B-04: load and test the 6 configurations one after the other
# Usage : ./test_all.sh            (log in results_b04.txt)
# =============================================================================
cd "$(dirname "$0")"
LOG=results_b04.txt
echo "B-04 board test, $(date)" | tee $LOG
for CFG in hybrid_lite hybrid_ib sha3_lite sha3_ib ascon_lite ascon_ib; do
    echo "################ $CFG" | tee -a $LOG
    if ./load_cfg.sh $CFG >> $LOG 2>&1; then
        sudo ./hash_dma_test ${CFG%_*} ${CFG#*_} golden.txt 2>&1 | tee -a $LOG | tail -4
    else
        echo "LOAD FAILED (see $LOG)" | tee -a $LOG
    fi
done
echo "################ summary" | tee -a $LOG
grep -E "^#{16} |^(PASS|FAIL):|LOAD FAILED" $LOG | tee -a ${LOG%.txt}_summary.txt
