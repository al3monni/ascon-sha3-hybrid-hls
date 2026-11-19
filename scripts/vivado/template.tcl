# =============================================================================
# template.tcl - Vivado batch flow for a stream IP on a Zynq UltraScale+ board
#
#   project -> block design -> synthesis -> implementation -> bitstream ->
#   reports -> .bit.bin -> .xsa -> device tree overlay
#
# What it builds
# --------------
# A complete PS + PL system around ONE user IP (HLS or RTL, packaged for IP Integrator)
# that reads an AXI4-Stream and writes an AXI4-Stream. The data is moved between the DDR
# memory of the processing system (PS) and the IP by two AXI DMA cores, one per direction,
# which is the layout expected by the uniss_dma Linux driver
# (github.com/mdc-suite/AXI-based-driver-applications, kv260/ubuntu-driver):
#
#                    +-------------------- PL (one clock: pl_clk0) ---------------------+
#   PS               |                                                                  |
#   M_AXI_HPM0_FPD --+--> axi_smc_ctrl --+--> axi_dma_0 registers (S_AXI_LITE)          |
#   (control)        |                   +--> axi_dma_1 registers (S_AXI_LITE)          |
#                    |                   +--> user IP registers (AXI4-Lite, optional)   |
#                    |                                                                  |
#   S_AXI_HPC0_FPD <-+--- axi_smc_data <-+--- axi_dma_0 (MM2S) ==AXI4-Stream==> user IP |
#   (DDR access)     |                   +--- axi_dma_1 (S2MM) <==AXI4-Stream== user IP |
#                    +------------------------------------------------------------------+
#
#   MM2S = memory-mapped to stream (DDR -> IP), S2MM = stream to memory-mapped (IP -> DDR).
#
# Requirements on the user IP
# ---------------------------
#   - packaged as an IP (a folder with component.xml), identified by its VLNV
#     (Vendor:Library:Name:Version);
#   - exactly one AXI4-Stream slave (input) and one AXI4-Stream master (output), both with
#     TDATA of STREAM_WIDTH bits; the output must drive TLAST on its last beat, otherwise
#     the S2MM channel of the DMA never closes the transfer;
#   - one clock pin and one active-low reset pin (ap_clk / ap_rst_n for an HLS IP);
#   - optionally one AXI4-Lite slave for control registers (USE_AXILITE below).
#
# How to use
# ----------
#   1. Copy this file next to your project and edit section 0 (user settings) only.
#   2. Launch from a shell, in the folder of this file:
#        vivado -mode batch -nojournal -source template.tcl
#      or, from the Tcl console of an open Vivado:   source <path>/template.tcl
#   3. Results are written to OUT_DIR, the Vivado project to PRJ_DIR.
#
# Vivado version
# --------------
# Written and tested with Vivado 2025.1.1, but not tied to it: there is no version check
# and the catalog IPs are taken in the latest version that the running Vivado offers
# (latest_vlnv, section 1). Lines tagged [VERSION] mark the points that are known to
# differ between releases, i.e. where to look first if another version stops with an error.
#   [VERSION] Install layout: from 2025.1 the tools are in <root>/<version>/Vivado
#             (e.g. C:\Xilinx\2025.1.1\Vivado\bin\vivado.bat); up to 2024.2 the layout
#             was <root>/Vivado/<version>. Adjust launch scripts and PATH accordingly.
# =============================================================================


# =============================================================================
# 0. User settings - the only section to edit
# =============================================================================

set SCRIPT_DIR [file dirname [file normalize [info script]]]

# --- Device and board ---------------------------------------------------------
# PART          : FPGA part, as listed by [get_parts].
# BOARD_PATTERN : pattern matching the board part, as listed by [get_board_parts].
# The defaults are the Kria KV260 (K26 SOM). Any Zynq UltraScale+ board with board files
# installed works the same way: the board files carry the PS configuration (DDR, MIO,
# clocks), which this script applies as "board preset".
set PART          xck26-sfvc784-2LV-c
set BOARD_PATTERN *kv260_som*

# --- User IP -------------------------------------------------------------------
# IP_REPO : folder that contains the packaged IP (component.xml, also in a subfolder).
#           [VERSION] A Vitis 2025.1 HLS component exports it to <component>/hls/impl/ip;
#           the classic Vitis HLS projects of older releases to <project>/<solution>/impl/ip.
# IP_VLNV : Vendor:Library:Name:Version of the IP, as shown in the IP catalog.
# IP_INST : name of the IP instance inside the block design (free choice).
set IP_REPO      [file join $SCRIPT_DIR ip_repo]
set IP_VLNV      vendor:library:name:1.0
set IP_INST      user_ip_0
set IP_CLK_PIN   ap_clk        ;# clock pin of the IP
set IP_RSTN_PIN  ap_rst_n      ;# reset pin of the IP, active low

# TDATA width of both streams of the IP [bit]: 8, 16, 32, 64, 128, 256, 512 or 1024
set STREAM_WIDTH 64

# 1 = the IP has an AXI4-Lite slave (control registers) to connect to the PS; 0 = streams only
set USE_AXILITE  1

# --- PL clock --------------------------------------------------------------------
# Frequency requested for pl_clk0 [MHz]: the only clock of the PL. It is also the timing
# constraint of the implementation (no .xdc file is needed: the PS IP provides it).
set PL_MHZ       100

# --- Address map (as seen by the processor) -----------------------------------
# The M_AXI_HPM0_FPD port of a Zynq UltraScale+ answers from 0xA000_0000 (256 MB window).
# Each peripheral gets a 64 KB block; the same values go into the device tree overlay.
set ADDR_DMA0    0xA0000000    ;# axi_dma_0 registers (MM2S, DDR -> IP)
set ADDR_DMA1    0xA0010000    ;# axi_dma_1 registers (S2MM, IP -> DDR)
set ADDR_IP      0xA0020000    ;# user IP registers (used only if USE_AXILITE = 1)
set ADDR_RANGE   0x10000

# --- Project and outputs -----------------------------------------------------------
set PRJ_NAME     my_system                                ;# also the base name of the output files
set BD_NAME      system_bd                                ;# block design name
set PRJ_DIR      [file join $SCRIPT_DIR build vivado]     ;# Vivado project (can be deleted and rebuilt)
set OUT_DIR      [file join $SCRIPT_DIR results vivado]   ;# reports, bitstream, .xsa, overlay
set JOBS         4                                        ;# parallel jobs for synthesis / implementation

# Stop early, to inspect an intermediate result:  "" = full flow | bd | synth
set STOP_AFTER   ""


# =============================================================================
# 1. Checks and helper procedures
# =============================================================================

if {![file isdirectory $IP_REPO]} { error "IP_REPO not found: $IP_REPO" }
if {$USE_AXILITE ni {0 1}}        { error "USE_AXILITE must be 0 or 1" }
if {$STREAM_WIDTH ni {8 16 32 64 128 256 512 1024}} { error "Unsupported STREAM_WIDTH: $STREAM_WIDTH" }

# Full VLNV of a catalog IP in the latest version available in the running Vivado
# (e.g. xilinx.com:ip:axi_dma:* -> xilinx.com:ip:axi_dma:7.1), so no IP version is
# hard-coded. It needs an open project: it is called from section 3.
# [VERSION] IP versions change between releases (zynq_ultra_ps_e: 3.3 in 2021.2, 3.5 in
#           2025.1.1; in 2025.1.1 also axi_dma 7.1, smartconnect 1.0, proc_sys_reset 5.0).
#           A newer IP version can rename or drop a CONFIG.* parameter set in section 3:
#           list the valid ones with   report_property [get_bd_cells <cell>]
proc latest_vlnv {pattern} {
    set defs [lsort -dictionary [get_ipdefs -all $pattern]]
    if {[llength $defs] == 0} { error "IP not in catalog: $pattern" }
    return [lindex $defs end]
}

# Interface pin of a block design cell, selected by bus type (axis / aximm) and direction
# (Master / Slave) instead of by name, so the script does not depend on how the IP calls
# its ports. The IP must have exactly one interface of each requested kind.
proc intf_pin {cell bus mode} {
    set pins [get_bd_intf_pins -of_objects [get_bd_cells $cell] \
                  -filter "VLNV =~ *:${bus}_rtl:* && MODE == $mode"]
    if {[llength $pins] != 1} {
        error "$cell: expected one $bus $mode interface, found [llength $pins]"
    }
    return [lindex $pins 0]
}

# wait_on_run returns also when a run fails: stop the script unless the run completed.
proc check_run {run} {
    set status   [get_property STATUS   [get_runs $run]]
    set progress [get_property PROGRESS [get_runs $run]]
    if {$progress ne "100%" || [string match -nocase "*error*" $status] || [string match -nocase "*failed*" $status]} {
        error "$run did not complete: $status"
    }
}


# =============================================================================
# 2. Project
# =============================================================================

# -force overwrites an existing project in PRJ_DIR: every launch starts from scratch.
file mkdir $PRJ_DIR
create_project -force $PRJ_NAME $PRJ_DIR -part $PART

# Board files: needed for the PS preset (step 3a) and recorded in the .xsa.
# If the board is not found, install its files from the Vivado Store
# (GUI: Tools > Vivado Store > Boards), then relaunch.
set board [lindex [lsort -dictionary [get_board_parts -quiet -latest_file_version $BOARD_PATTERN]] end]
if {$board eq ""} { error "No board files matching '$BOARD_PATTERN': install them from the Vivado Store" }
set_property board_part $board [current_project]

# Make the user IP visible to this project: the IP repository is the folder of the
# packaged IP. After the catalog refresh the IP must be found by its VLNV.
set_property ip_repo_paths [list $IP_REPO] [current_project]
update_ip_catalog -rebuild
if {[llength [get_ipdefs -all $IP_VLNV]] == 0} { error "IP $IP_VLNV not found in $IP_REPO" }


# =============================================================================
# 3. Block design: cells
# =============================================================================
# The block design (BD) is the IP Integrator schematic. It is created here with explicit
# commands (cells, then connections, then addresses) instead of replaying a GUI export, so
# every choice is visible and commented.

create_bd_design $BD_NAME

# --- 3a. Processing system ------------------------------------------------------
# The Zynq UltraScale+ PS block. "apply_board_preset" loads the configuration of the board
# (DDR, MIO, peripherals, clocks) from the board files.
set ps [create_bd_cell -type ip -vlnv [latest_vlnv xilinx.com:ip:zynq_ultra_ps_e:*] zynq_ultra_ps_e_0]
apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $ps

# Settings of this system on top of the preset:
#   M_AXI_HPM0_FPD only   PS master port: the processor writes the PL registers through it
#   S_AXI_HPC0_FPD        PS slave port: the DMAs read/write the DDR through it
#   IRQ0 off              no PL -> PS interrupt (the driver polls the DMA status registers)
#   PL0 clock             pl_clk0 at PL_MHZ, the clock of the whole PL
# (Parameter names keep the GP numbering: M_AXI_GP0/1/2 = HPM0_FPD/HPM1_FPD/HPM0_LPD,
#  S_AXI_GP0 = HPC0_FPD.)
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {0} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__MAXIGP0__DATA_WIDTH {128} \
    CONFIG.PSU__USE__S_AXI_GP0 {1} \
    CONFIG.PSU__SAXIGP0__DATA_WIDTH {128} \
    CONFIG.PSU__USE__IRQ0 {0} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ $PL_MHZ \
] $ps

# --- 3b. AXI DMA, one per direction ------------------------------------------------
# Direct register mode (no scatter-gather): one transfer = one contiguous buffer, started
# by writing address and length into the DMA registers.
#   c_include_sg 0          scatter-gather engine off
#   c_sg_length_width 26    width of the length register: up to 2^26 bytes per transfer
#                           (the software driver may impose a lower limit)
#   c_addr_width 64         64-bit buffer addresses (the PS has more than 4 GB of address space)
# Stream side = STREAM_WIDTH; memory side = same width, but never below 32 bit.
set VLNV_DMA [latest_vlnv xilinx.com:ip:axi_dma:*]
set dma_mm_width [expr {$STREAM_WIDTH < 32 ? 32 : $STREAM_WIDTH}]
set dma_common [list \
    CONFIG.c_include_sg {0} \
    CONFIG.c_sg_include_stscntrl_strm {0} \
    CONFIG.c_sg_length_width {26} \
    CONFIG.c_addr_width {64} \
]
# axi_dma_0: MM2S channel only (DDR -> user IP)
set dma0 [create_bd_cell -type ip -vlnv $VLNV_DMA axi_dma_0]
set_property -dict [concat $dma_common [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_m_axi_mm2s_data_width $dma_mm_width \
    CONFIG.c_m_axis_mm2s_tdata_width $STREAM_WIDTH \
    CONFIG.c_mm2s_burst_size {16} \
]] $dma0
# axi_dma_1: S2MM channel only (user IP -> DDR)
set dma1 [create_bd_cell -type ip -vlnv $VLNV_DMA axi_dma_1]
set_property -dict [concat $dma_common [list \
    CONFIG.c_include_mm2s {0} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_m_axi_s2mm_data_width $dma_mm_width \
    CONFIG.c_s_axis_s2mm_tdata_width $STREAM_WIDTH \
    CONFIG.c_s2mm_burst_size {16} \
]] $dma1

# --- 3c. User IP -------------------------------------------------------------------
set ip [create_bd_cell -type ip -vlnv $IP_VLNV $IP_INST]

# --- 3d. Interconnect ----------------------------------------------------------------
# Two SmartConnect cores, which also adapt protocol and data width between the two sides:
#   axi_smc_data  2 slaves (the two DMA masters) -> 1 master (PS port to the DDR)
#   axi_smc_ctrl  1 slave (PS master port)       -> 2 or 3 masters (DMA registers, IP registers)
#                 it converts the AXI4 of the PS into the AXI4-Lite of the registers
# [VERSION] Older reference designs use axi_interconnect here; SmartConnect is its
#           replacement for UltraScale+ designs and needs no per-port clock/reset pins.
set VLNV_SMC [latest_vlnv xilinx.com:ip:smartconnect:*]
set smc_data [create_bd_cell -type ip -vlnv $VLNV_SMC axi_smc_data]
set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1}] $smc_data
set smc_ctrl [create_bd_cell -type ip -vlnv $VLNV_SMC axi_smc_ctrl]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI [expr {$USE_AXILITE ? 3 : 2}]] $smc_ctrl

# --- 3e. Reset --------------------------------------------------------------------
# proc_sys_reset synchronizes the asynchronous PS reset (pl_resetn0) to pl_clk0 and
# provides the active-low reset of the PL peripherals.
set rst [create_bd_cell -type ip -vlnv [latest_vlnv xilinx.com:ip:proc_sys_reset:*] rst_pl0]


# =============================================================================
# 4. Block design: connections
# =============================================================================
# connect_bd_intf_net connects a whole interface (all the signals of an AXI bus at once),
# connect_bd_net connects single pins (clocks and resets).

# --- 4a. Data path: DDR -> axi_dma_0 -> user IP -> axi_dma_1 -> DDR -------------------
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXIS_MM2S] [intf_pin $IP_INST axis Slave]
connect_bd_intf_net [intf_pin $IP_INST axis Master]          [get_bd_intf_pins axi_dma_1/S_AXIS_S2MM]
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXI_MM2S]  [get_bd_intf_pins axi_smc_data/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_1/M_AXI_S2MM]  [get_bd_intf_pins axi_smc_data/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_smc_data/M00_AXI]  [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HPC0_FPD]

# --- 4b. Control path: PS -> registers ---------------------------------------------
connect_bd_intf_net [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] [get_bd_intf_pins axi_smc_ctrl/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M00_AXI] [get_bd_intf_pins axi_dma_0/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M01_AXI] [get_bd_intf_pins axi_dma_1/S_AXI_LITE]
if {$USE_AXILITE} {
    connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M02_AXI] [intf_pin $IP_INST aximm Slave]
}

# --- 4c. Clock and reset: one domain for the whole PL -------------------------------
# pl_clk0 also clocks the two PS AXI ports in use (each PS port has its own clock input).
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk] \
    [get_bd_pins zynq_ultra_ps_e_0/saxihpc0_fpd_aclk] \
    [get_bd_pins rst_pl0/slowest_sync_clk] \
    [get_bd_pins axi_dma_0/s_axi_lite_aclk] [get_bd_pins axi_dma_0/m_axi_mm2s_aclk] \
    [get_bd_pins axi_dma_1/s_axi_lite_aclk] [get_bd_pins axi_dma_1/m_axi_s2mm_aclk] \
    [get_bd_pins axi_smc_data/aclk] [get_bd_pins axi_smc_ctrl/aclk] \
    [get_bd_pins $IP_INST/$IP_CLK_PIN]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] [get_bd_pins rst_pl0/ext_reset_in]
connect_bd_net [get_bd_pins rst_pl0/peripheral_aresetn] \
    [get_bd_pins axi_dma_0/axi_resetn] [get_bd_pins axi_dma_1/axi_resetn] \
    [get_bd_pins axi_smc_data/aresetn] [get_bd_pins axi_smc_ctrl/aresetn] \
    [get_bd_pins $IP_INST/$IP_RSTN_PIN]


# =============================================================================
# 5. Block design: address map, validation, save
# =============================================================================

# Registers at fixed addresses in the address space of the processor: software and
# device tree must use the same values, so they are forced instead of auto-assigned.
set ps_data [get_bd_addr_spaces zynq_ultra_ps_e_0/Data]
assign_bd_address -offset $ADDR_DMA0 -range $ADDR_RANGE -target_address_space $ps_data \
    [get_bd_addr_segs axi_dma_0/S_AXI_LITE/Reg] -force
assign_bd_address -offset $ADDR_DMA1 -range $ADDR_RANGE -target_address_space $ps_data \
    [get_bd_addr_segs axi_dma_1/S_AXI_LITE/Reg] -force
if {$USE_AXILITE} {
    set ip_seg [lindex [get_bd_addr_segs -of_objects [intf_pin $IP_INST aximm Slave]] 0]
    assign_bd_address -offset $ADDR_IP -range $ADDR_RANGE -target_address_space $ps_data $ip_seg -force
}
# Everything still unmapped (the DDR as seen by the two DMA masters): automatic assignment
assign_bd_address

# Design rule check of the BD (unconnected pins, clock/width mismatches, address conflicts).
# It also propagates parameters between connected interfaces. It stops the script on error.
validate_bd_design
save_bd_design

# Copy of the BD as a Tcl script: to review it or to recreate it from the GUI
# (Tcl console: source <file>). The copy is tied to the Vivado version that wrote it.
file mkdir $OUT_DIR
write_bd_tcl -force [file join $OUT_DIR ${BD_NAME}.tcl]

if {$STOP_AFTER eq "bd"} { return }


# =============================================================================
# 6. Synthesis, implementation, bitstream
# =============================================================================

set bd_file [get_files ${BD_NAME}.bd]

# Synthesis mode of the BD:
#   None (used here)        the whole BD is synthesized in one run with the top level:
#                           simplest, and fast enough for a small design
#   Hierarchical (default)  one out-of-context run per IP, cached between builds:
#                           better for large designs that are rebuilt often
set_property synth_checkpoint_mode None $bd_file

# Generate the HDL of every IP of the BD, then the HDL wrapper that instantiates the BD:
# the wrapper is the top level of the design.
generate_target all $bd_file
set wrapper [make_wrapper -files $bd_file -top]
add_files -norecurse $wrapper
set_property top ${BD_NAME}_wrapper [current_fileset]
update_compile_order -fileset sources_1

# Synthesis (run synth_1, default strategy). launch_runs starts the run in the background,
# wait_on_run blocks until it ends.
launch_runs synth_1 -jobs $JOBS
wait_on_run synth_1
check_run synth_1
if {$STOP_AFTER eq "synth"} { return }

# Implementation (run impl_1: opt, place, route) up to the bitstream.
# BIN_FILE also writes the raw .bin next to the .bit.
set_property STEPS.WRITE_BITSTREAM.ARGS.BIN_FILE true [get_runs impl_1]
launch_runs impl_1 -to_step write_bitstream -jobs $JOBS
wait_on_run impl_1
check_run impl_1


# =============================================================================
# 7. Reports and output files
# =============================================================================

# Load the routed design in memory: the reports below refer to it.
open_run impl_1

# --- 7a. Reports ---------------------------------------------------------------------
# timing_summary.rpt   "Design Timing Summary": WNS (setup) and WHS (hold) must be >= 0,
#                      otherwise the design does not meet PL_MHZ and the bitstream is not reliable
# utilization.rpt      resources of the whole PL
# utilization_hier.rpt resources per block: the row of IP_INST is the user IP alone
report_timing_summary -max_paths 10 -file [file join $OUT_DIR timing_summary.rpt]
report_utilization -file [file join $OUT_DIR utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 2 -file [file join $OUT_DIR utilization_hier.rpt]

# --- 7b. Bitstream -------------------------------------------------------------------
set run_dir [get_property DIRECTORY [get_runs impl_1]]
file copy -force [file join $run_dir ${BD_NAME}_wrapper.bit] [file join $OUT_DIR $PRJ_NAME.bit]

# --- 7c. .bit.bin for Linux (fpgautil / xmutil) -------------------------------------------
# The Linux FPGA manager loads the bitstream in the format produced by
# "bootgen -process_bitstream bin". bootgen reads a .bif file that lists the input image.
# bootgen ships with Vivado and Vitis: it is searched in the PATH and in the install folders.
set bootgen ""
set candidates [list [lindex [auto_execok bootgen] 0]]
if {[info exists ::env(XILINX_VIVADO)]} {
    foreach dir [list [file join $::env(XILINX_VIVADO) bin] \
                      [file join [file dirname $::env(XILINX_VIVADO)] Vitis bin]] {
        lappend candidates [file join $dir bootgen] [file join $dir bootgen.bat]
    }
}
foreach c $candidates {
    if {$c ne "" && [file isfile $c]} { set bootgen $c; break }
}
if {$bootgen ne ""} {
    set fh [open [file join $OUT_DIR $PRJ_NAME.bif] w]
    puts $fh "all:\n\{\n  \[destination_device = pl\] $PRJ_NAME.bit\n\}"
    close $fh
    set here [pwd]
    cd $OUT_DIR
    # bootgen writes its banner to stderr, which exec reports as an error: the result is
    # checked on the output file instead
    catch {exec $bootgen -image $PRJ_NAME.bif -arch zynqmp -process_bitstream bin -w on}
    cd $here
}
if {![file exists [file join $OUT_DIR $PRJ_NAME.bit.bin]]} {
    puts "WARNING: $PRJ_NAME.bit.bin not generated (bootgen missing or failed)"
}

# --- 7d. Hardware platform (.xsa) -------------------------------------------------------
# Archive with the hardware description (PS configuration, address map) and the bitstream:
# the input of Vitis (bare-metal / Linux applications) and PetaLinux.
# [VERSION] write_hw_platform exists from 2019.2; earlier releases used write_hwdef /
#           write_sysdef and the .hdf format.
write_hw_platform -fixed -include_bit -force -file [file join $OUT_DIR $PRJ_NAME.xsa]

# --- 7e. Device tree overlay for the uniss_dma driver ---------------------------------
# Linux needs a description of the hardware added to the PL: one node per DMA (register
# address, channel direction, stream width) and the "uniss,dmas" node that hands both DMAs
# to the driver. Channel order seen by the driver: 0 = axi_dma_0 (MM2S), 1 = axi_dma_1 (S2MM).
# The S2MM channel node is named after the offset of its registers inside the DMA (0x30).
# The user IP registers (USE_AXILITE = 1) are not described: no kernel driver binds them,
# an application reaches them at ADDR_IP by mapping /dev/mem.
# On the board:
#   dtc -@ -I dts -O dtb -o pl.dtbo pl.dtsi
#   sudo fpgautil -b <PRJ_NAME>.bit.bin
#   sudo fpgautil -o pl.dtbo
set dma0_addr [format %x $ADDR_DMA0]
set dma1_addr [format %x $ADDR_DMA1]
set dma1_chan [format %x [expr {$ADDR_DMA1 + 0x30}]]
set reg_range [format 0x%x $ADDR_RANGE]
set width_hex [format 0x%x $STREAM_WIDTH]
set fh [open [file join $OUT_DIR pl.dtsi] w]
puts $fh "/dts-v1/;
/plugin/;

/ {
    fragment@0 {
        target-path = \"/\";
        __overlay__ {
            axi_dma_0: axi_dma_0@$dma0_addr {
                compatible = \"xlnx,axi-dma\";
                status = \"okay\";
                reg = <0x0 0x$dma0_addr 0x0 $reg_range>;
                mm2s-channels = <0x1>;
                s2mm-channels = <0x0>;
                dma_channel_$dma0_addr: dma-channel@$dma0_addr {
                    compatible = \"xlnx,axi-dma-mm2s-channel\";
                    xlnx,datawidth = <$width_hex>;
                    xlnx,device-id = <0x0>;
                    dma-channels = <0x1>;
                    mm2s = <0x1>;
                };
            };

            axi_dma_1: axi_dma_1@$dma1_addr {
                compatible = \"xlnx,axi-dma\";
                status = \"okay\";
                reg = <0x0 0x$dma1_addr 0x0 $reg_range>;
                mm2s-channels = <0x0>;
                s2mm-channels = <0x1>;
                dma_channel_$dma1_chan: dma-channel@$dma1_chan {
                    compatible = \"xlnx,axi-dma-s2mm-channel\";
                    xlnx,datawidth = <$width_hex>;
                    xlnx,device-id = <0x1>;
                    dma-channels = <0x1>;
                    mm2s = <0x0>;
                };
            };

            dmas {
                compatible = \"uniss,dmas\";
                status = \"okay\";
                dev-handles = <&axi_dma_0 &axi_dma_1>;
            };
        };
    };
};"
close $fh
