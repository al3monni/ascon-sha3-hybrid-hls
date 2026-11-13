#!/bin/bash
# =============================================================================
# load_cfg.sh - B-04: load one configuration on the KV260 (Ubuntu + uniss_dma)
# Usage : ./load_cfg.sh <hybrid_lite|hybrid_ib|sha3_lite|sha3_ib|ascon_lite|ascon_ib>
# Steps (same as the supervisors' instructions, plus the cleanup of the previous config):
#   1. remove the previous configuration: driver first (frees /dev/uniss_dma), then overlay
#   2. program the PL:      fpgautil -b kv260_<cfg>.bit.bin
#   3. apply the overlay:   pl.dtsi -> pl.dtbo -> configfs overlay "b04"
#   4. load the driver:     axidma.ko (probes the 2 DMA channels of the overlay)
# =============================================================================
set -e
cd "$(dirname "$0")"
CFG=$1
if [ -z "$CFG" ] || [ ! -f "$CFG/kv260_$CFG.bit.bin" ]; then
    echo "usage: $0 <cfg>   cfg = hybrid_lite hybrid_ib sha3_lite sha3_ib ascon_lite ascon_ib"
    exit 1
fi
OV=/sys/kernel/config/device-tree/overlays/b04

echo "== [$CFG] 1. cleanup of the previous configuration"
if lsmod | grep -q '^axidma'; then sudo rmmod axidma; echo "   driver removed"; fi
if [ -d "$OV" ]; then sudo rmdir "$OV"; echo "   overlay b04 removed"; fi

echo "== [$CFG] 2. bitstream"
sudo fpgautil -b "$CFG/kv260_$CFG.bit.bin"

echo "== [$CFG] 3. device tree overlay"
dtc -@ -q -I dts -O dtb -o "$CFG/pl.dtbo" "$CFG/pl.dtsi"
sudo mkdir "$OV"
sudo sh -c "cat '$CFG/pl.dtbo' > '$OV/dtbo'"
echo "   overlay status: $(cat $OV/status)"

echo "== [$CFG] 4. driver"
sudo insmod axidma-driver/axidma.ko
sleep 1
ls -l /dev/uniss_dma
sudo dmesg | grep -E "Found (MM2S|S2MM) channel|Total channels probed" | tail -3
echo "== [$CFG] ready: sudo ./hash_dma_test ${CFG%_*} ${CFG#*_} golden.txt"
