#!/bin/bash
# build_driver.sh - B-04: build the uniss_dma driver (axidma.ko) of the supervisors on the KV260
#   Needs the kernel headers: sudo apt install linux-headers-$(uname -r) build-essential
#   (their install_libs.sh installs them). The Kbuild file replaces their 2-line Makefile.
set -e
cd "$(dirname "$0")/axidma-driver"
echo "obj-m += axidma.o" > Kbuild
make -C /lib/modules/$(uname -r)/build M=$PWD modules
ls -l axidma.ko
