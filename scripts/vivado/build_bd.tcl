# =============================================================================
# build_bd.tcl - B-04: Vivado 2025.1.1 flow for the KV260, one IP per run
#   project -> block design (Zynq MPSoC + 2 AXI DMA + hash IP) -> synthesis ->
#   implementation -> bitstream (.bit + .bit.bin) -> .xsa -> reports -> device tree overlay
#
# Location : <repo>/scripts/vivado/build_bd.tcl
# Launch   : vivado -mode batch -nojournal -source scripts/vivado/build_bd.tcl   (repo root)
#            or scripts\vivado\run_b04.bat <lite|ib> for the three variants
#
# Parameters (environment variables, same names as the HLS flow):
#   VARIANT  Hybrid-Test | Sha-Test | Ascon-Test   (default: Hybrid-Test)
#   CTRL     lite | ib                             (default: lite)
#   T_HLS    HLS point of the IP [ns]              (default: 10)
#   DESIGN   design label (default: scripts/hls/design.txt)
#   PL_MHZ   pl_clk0 frequency [MHz]               (default: 100)
#   JOBS     parallel jobs for synth/impl runs     (default: 4)
#   STOP_AFTER  bd | synth | (empty = full flow)   (debug: stop early)
#
# Input    : IP exported by scripts/hls/run_point.tcl
#              build/hls/<DESIGN>/<VARIANT>[-ib]/T_<ns>/hls/impl/ip/
#              VLNV unica.it:hls:hash_<hybrid|sha3|ascon>_<lite|ib>:1.0   (common.tcl)
# Output   : build/vivado/<DESIGN>/<VARIANT>[-ib]/          Vivado project (gitignored)
#            results/vivado/<DESIGN>/<VARIANT>[-ib]/         reports, summary, pl.dtsi
#            results/vivado/<DESIGN>/<VARIANT>[-ib]/hw/      .bit, .bit.bin, .xsa
#
# -----------------------------------------------------------------------------
# Port of the supervisors' Vivado scripts (mdc-suite/AXI-based-driver-applications, KV260):
#   aes/vivado/design_1*.tcl         write_bd_tcl dump of the AES block design (2021.2/2023)
#   vpa/vivado/hlsPlatformDesign.tcl write_project_tcl dump of the VPA project (2021.2)
# Those files are tool dumps tied to one design and one Vivado version (AES design_1.tcl: hard
# 2021.2 version check; VPA: absolute paths, imported checkpoint; 2021.2 dumps: PS 3.3).
# This script rebuilds the same system from scratch, for Vivado 2025.1.1 and for our 6 IPs
# (full comparison: docs/reports/porting_flusso_vivado.md):
#
#   their block design                         | ours
#   -------------------------------------------+-------------------------------------------
#   zynq_ultra_ps_e 3.3/3.5, KV260 preset       | latest version in the 2025.1 catalog;
#     (MIO/DDR dict of 793 properties)          |   board preset if the KV260 board files are
#                                               |   installed, else the same dict of the AES
#                                               |   2023 script (ps_kv260_preset.tcl)
#   pl_clk0 100 MHz (AES) / 230 MHz (VPA)       | pl_clk0 PL_MHZ (100 MHz default)
#   M_AXI_HPM0 (+HPM1) -> axi_interconnect      | M_AXI_HPM0_FPD -> SmartConnect (control)
#     -> AXI DMA S_AXI_LITE                     |   -> AXI DMA S_AXI_LITE (+ IP s_axi_control,
#                                               |      lite version only)
#   AXI DMA, no SG, 32-bit streams:             | as VPA: axi_dma_0 MM2S only, axi_dma_1 S2MM only
#     AES 2 MM2S + 1 S2MM, VPA 1 MM2S + 1 S2MM  |   (64-bit streams = one state word per beat)
#   DMA M_AXI -> SmartConnect -> S_AXI_HPC0/HP0 | DMA M_AXI -> SmartConnect -> S_AXI_HPC0_FPD
#   DMA regs: VPA 0xA000_0000, 0xA001_0000;     | VPA addresses (driver channel order:
#     AES 0xA000_0000, 0xA010_0000, 0xA030_0000 |   ch0 = MM2S, ch1 = S2MM, as in VPA)
#   IP: ap_clk, ap_rst_n + AXIS (VPA)           | ib: same (ap_ctrl_none); lite: + s_axi_control
#   bitstream from the GUI, .bin for fpgautil   | write_bitstream + bootgen .bit.bin, .xsa,
#                                               |   reports, pl.dtsi for the uniss_dma driver
# -----------------------------------------------------------------------------
# =============================================================================

# --- Shared settings of the Tcl flow: env_or, ROOT, DESIGN, CTRL, VLNV procs ---
set SCRIPT_DIR [file dirname [file normalize [info script]]]
source [file join $SCRIPT_DIR .. hls common.tcl]
source [file join $SCRIPT_DIR ps_kv260_preset.tcl]

set PART     xck26-sfvc784-2LV-c
set VARIANT  [env_or VARIANT Hybrid-Test]
set T_HLS    [env_or T_HLS 10]
set PL_MHZ   [env_or PL_MHZ 100]
set JOBS     [env_or JOBS 4]
set STOP_AFTER [env_or STOP_AFTER ""]

set VDIR     [variant_dir $VARIANT $CTRL]
set TAG      "T_[string map {. p} $T_HLS]"
set IP_DIR   [file join $ROOT build hls $DESIGN $VDIR $TAG hls impl ip]
set IP_VLNV  [ip_vlnv $VARIANT $CTRL]
set PRJ_NAME "kv260_[variant_short $VARIANT]_$CTRL"      ;# e.g. kv260_hybrid_lite
set PRJ_DIR  [file join $ROOT build vivado $DESIGN $VDIR]
set OUT_DIR  [file join $ROOT results vivado $DESIGN $VDIR]
set HW_DIR   [file join $OUT_DIR hw]
set BD_NAME  hash_bd

# Address map seen by the A53 cores (M_AXI_HPM0_FPD window starts at 0xA000_0000)
set ADDR_DMA0 0xA0000000     ;# axi_dma_0, MM2S  -> driver channel 0
set ADDR_DMA1 0xA0010000     ;# axi_dma_1, S2MM  -> driver channel 1
set ADDR_IP   0xA0020000     ;# hash IP s_axi_control (lite only)
set ADDR_RANGE 0x10000

puts "=== build_bd: \[$DESIGN\] $VARIANT ctrl=$CTRL, IP $IP_VLNV, pl_clk0 $PL_MHZ MHz -> $PRJ_DIR ==="

if {![file exists [file join $IP_DIR component.xml]]} {
    error "IP not found: $IP_DIR (run scripts/hls/run_point.tcl with VARIANT=$VARIANT CTRL=$CTRL first)"
}

# --- Helpers -------------------------------------------------------------------

# Latest version of a Xilinx catalog IP (e.g. xilinx.com:ip:axi_dma:*) -> full VLNV
proc latest_vlnv {pattern} {
    set defs [lsort -dictionary [get_ipdefs -all $pattern]]
    if {[llength $defs] == 0} { error "IP not in catalog: $pattern" }
    return [lindex $defs end]
}

# First interface pin of a BD cell with the given bus type (axis / aximm) and mode
proc intf_pin {cell bus mode} {
    set p [lindex [get_bd_intf_pins -of_objects [get_bd_cells $cell] \
                   -filter "VLNV =~ *:${bus}_rtl:* && MODE == $mode"] 0]
    if {$p eq ""} { error "No $bus $mode interface on $cell" }
    return $p
}

# Run status check after wait_on_run
proc check_run {run} {
    set st [get_property STATUS [get_runs $run]]
    set pr [get_property PROGRESS [get_runs $run]]
    puts "=== $run: $st ($pr) ==="
    if {$pr ne "100%" || [string match -nocase "*error*" $st] || [string match -nocase "*failed*" $st]} {
        error "$run did not complete: $st"
    }
}

# --- 1. Project ------------------------------------------------------------------

file mkdir $PRJ_DIR
create_project -force $PRJ_NAME $PRJ_DIR -part $PART

# KV260 board files (Vivado board store): optional. With them the PS gets the official
# board preset and the .xsa carries the board; without them the preset dict is used.
set BOARD [lindex [lsort -dictionary [get_board_parts -quiet -latest_file_version *kv260_som*]] end]
if {$BOARD ne ""} {
    set_property board_part $BOARD [current_project]
    puts "build_bd: board part $BOARD"
} else {
    puts "build_bd: KV260 board files not installed -> PS preset from ps_kv260_preset.tcl"
}

# Our HLS IP: one repository = the export folder of this point
set_property ip_repo_paths [list $IP_DIR] [current_project]
update_ip_catalog -rebuild
if {[llength [get_ipdefs -all $IP_VLNV]] == 0} { error "IP $IP_VLNV not found in $IP_DIR" }

# --- 2. Block design -------------------------------------------------------------

create_bd_design $BD_NAME

# 2a. Processing system
set ps [create_bd_cell -type ip -vlnv [latest_vlnv xilinx.com:ip:zynq_ultra_ps_e:*] zynq_ultra_ps_e_0]
if {$BOARD ne ""} {
    apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $ps
} else {
    # same dict as the AES 2023 script; if this catalog version rejects it as a whole,
    # apply it property by property and report what was skipped
    if {[catch {set_property -dict $PS_KV260_PRESET $ps} err]} {
        puts "WARNING: build_bd: preset dict rejected ($err), applying it property by property"
        set skipped 0
        foreach {k v} $PS_KV260_PRESET {
            if {[catch {set_property $k $v $ps}]} { incr skipped }
        }
        puts "WARNING: build_bd: $skipped preset properties skipped"
    }
}

# Our settings on top of the preset:
#   HPM0_FPD only (control: DMA registers + IP registers), 128 bit as the KV260 default
#   HPC0_FPD slave (DMA data to/from DDR), 128 bit as in the AES design
#   pl_clk0 = PL_MHZ for the whole PL, pl_resetn0; no PL->PS interrupt (the driver polls)
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

# 2b. AXI DMA, one per direction, direct register mode (no scatter-gather), as in the
#     supervisors' designs. 64-bit streams and memory-map side: one beat = one state word.
#     Length register 26 bit (64 MB), the uniss_dma driver caps a transfer at 65535 bytes.
set DMA_VLNV [latest_vlnv xilinx.com:ip:axi_dma:*]
set dma_common [list \
    CONFIG.c_include_sg {0} \
    CONFIG.c_sg_include_stscntrl_strm {0} \
    CONFIG.c_sg_length_width {26} \
    CONFIG.c_addr_width {64} \
]
set dma0 [create_bd_cell -type ip -vlnv $DMA_VLNV axi_dma_0]
set_property -dict [concat $dma_common [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_m_axi_mm2s_data_width {64} \
    CONFIG.c_m_axis_mm2s_tdata_width {64} \
    CONFIG.c_mm2s_burst_size {16} \
]] $dma0
set dma1 [create_bd_cell -type ip -vlnv $DMA_VLNV axi_dma_1]
set_property -dict [concat $dma_common [list \
    CONFIG.c_include_mm2s {0} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_m_axi_s2mm_data_width {64} \
    CONFIG.c_s_axis_s2mm_tdata_width {64} \
    CONFIG.c_s2mm_burst_size {16} \
]] $dma1

# 2c. Hash IP (HLS)
set ip [create_bd_cell -type ip -vlnv $IP_VLNV hash_0]

# 2d. Interconnect: SmartConnect for data (2 DMA masters -> HPC0) and for control
#     (HPM0 -> DMA registers [+ IP registers]); SmartConnect also converts AXI4 -> AXI4-Lite
set SMC_VLNV [latest_vlnv xilinx.com:ip:smartconnect:*]
set smc_data [create_bd_cell -type ip -vlnv $SMC_VLNV axi_smc_data]
set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1}] $smc_data
set NCTRL [expr {$CTRL eq "lite" ? 3 : 2}]
set smc_ctrl [create_bd_cell -type ip -vlnv $SMC_VLNV axi_smc_ctrl]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI $NCTRL] $smc_ctrl

# 2e. Reset synchronizer for pl_resetn0
set rst [create_bd_cell -type ip -vlnv [latest_vlnv xilinx.com:ip:proc_sys_reset:*] rst_pl0]

# --- 3. Connections -------------------------------------------------------------

# Data path: DDR -> MM2S -> hash_0/in, hash_0/out -> S2MM -> DDR
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXIS_MM2S] [intf_pin hash_0 axis Slave]
connect_bd_intf_net [intf_pin hash_0 axis Master] [get_bd_intf_pins axi_dma_1/S_AXIS_S2MM]
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXI_MM2S] [get_bd_intf_pins axi_smc_data/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_1/M_AXI_S2MM] [get_bd_intf_pins axi_smc_data/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_smc_data/M00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HPC0_FPD]

# Control path: HPM0 -> DMA registers (+ IP registers in the lite version)
connect_bd_intf_net [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] [get_bd_intf_pins axi_smc_ctrl/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M00_AXI] [get_bd_intf_pins axi_dma_0/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M01_AXI] [get_bd_intf_pins axi_dma_1/S_AXI_LITE]
if {$CTRL eq "lite"} {
    connect_bd_intf_net [get_bd_intf_pins axi_smc_ctrl/M02_AXI] [intf_pin hash_0 aximm Slave]
}

# One clock (pl_clk0) and one reset domain for the whole PL
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk] \
    [get_bd_pins zynq_ultra_ps_e_0/saxihpc0_fpd_aclk] \
    [get_bd_pins rst_pl0/slowest_sync_clk] \
    [get_bd_pins axi_dma_0/s_axi_lite_aclk] [get_bd_pins axi_dma_0/m_axi_mm2s_aclk] \
    [get_bd_pins axi_dma_1/s_axi_lite_aclk] [get_bd_pins axi_dma_1/m_axi_s2mm_aclk] \
    [get_bd_pins axi_smc_data/aclk] [get_bd_pins axi_smc_ctrl/aclk] \
    [get_bd_pins hash_0/ap_clk]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] [get_bd_pins rst_pl0/ext_reset_in]
connect_bd_net [get_bd_pins rst_pl0/peripheral_aresetn] \
    [get_bd_pins axi_dma_0/axi_resetn] [get_bd_pins axi_dma_1/axi_resetn] \
    [get_bd_pins axi_smc_data/aresetn] [get_bd_pins axi_smc_ctrl/aresetn] \
    [get_bd_pins hash_0/ap_rst_n]

# --- 4. Address map -------------------------------------------------------------

set ps_data [get_bd_addr_spaces zynq_ultra_ps_e_0/Data]
assign_bd_address -offset $ADDR_DMA0 -range $ADDR_RANGE -target_address_space $ps_data [get_bd_addr_segs axi_dma_0/S_AXI_LITE/Reg] -force
assign_bd_address -offset $ADDR_DMA1 -range $ADDR_RANGE -target_address_space $ps_data [get_bd_addr_segs axi_dma_1/S_AXI_LITE/Reg] -force
if {$CTRL eq "lite"} {
    set ip_seg [lindex [get_bd_addr_segs -of_objects [intf_pin hash_0 aximm Slave]] 0]
    assign_bd_address -offset $ADDR_IP -range $ADDR_RANGE -target_address_space $ps_data $ip_seg -force
}
# DMA masters -> DDR through HPC0: let Vivado map every reachable segment
assign_bd_address

validate_bd_design
save_bd_design

# Address map as built (for the report and the device tree)
set amap {}
foreach seg [lsort [get_bd_addr_segs -of_objects $ps_data]] {
    lappend amap [format "%-40s offset %s  range %s" [get_property NAME $seg] \
        [get_property OFFSET $seg] [get_property RANGE $seg]]
}
puts "build_bd: PS address map:\n  [join $amap "\n  "]"

# BD as a Tcl script, for inspection / reuse in the GUI
file mkdir $OUT_DIR
write_bd_tcl -force [file join $OUT_DIR ${BD_NAME}.tcl]

if {$STOP_AFTER eq "bd"} { puts "=== build_bd: stopped after the block design ==="; return }

# --- 5. Wrapper, synthesis, implementation, bitstream ----------------------------

set bd_file [get_files ${BD_NAME}.bd]
# global synthesis of the whole BD (as in VPA: SYNTH_CHECKPOINT_MODE None): one run,
# less memory and time than one out-of-context run per IP for a design this small
set_property synth_checkpoint_mode None $bd_file
generate_target all $bd_file
set wrapper [make_wrapper -files $bd_file -top]
add_files -norecurse $wrapper
set_property top ${BD_NAME}_wrapper [current_fileset]
update_compile_order -fileset sources_1

launch_runs synth_1 -jobs $JOBS
wait_on_run synth_1
check_run synth_1
if {$STOP_AFTER eq "synth"} { puts "=== build_bd: stopped after synthesis ==="; return }

# .bin next to the .bit (write_bitstream -bin_file); the .bit.bin for fpgautil comes from bootgen
set_property STEPS.WRITE_BITSTREAM.ARGS.BIN_FILE true [get_runs impl_1]
launch_runs impl_1 -to_step write_bitstream -jobs $JOBS
wait_on_run impl_1
check_run impl_1

# --- 6. Reports and outputs -----------------------------------------------------

open_run impl_1
file mkdir $HW_DIR
report_timing_summary -max_paths 10 -file [file join $OUT_DIR timing_summary.rpt]
report_utilization -file [file join $OUT_DIR utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 2 -file [file join $OUT_DIR utilization_hier.rpt]

set wns [get_property SLACK [lindex [get_timing_paths -max_paths 1 -nworst 1 -setup] 0]]
set whs [get_property SLACK [lindex [get_timing_paths -max_paths 1 -nworst 1 -hold] 0]]
set timing_ok [expr {$wns >= 0 && $whs >= 0}]

# Post-route area of the hash IP inside the system and of the whole design, from the
# hierarchical utilization table:
# | Instance | Module | Total LUTs | Logic LUTs | LUTRAMs | SRLs | FFs | RAMB36 | RAMB18 | URAM | DSP Blocks |
proc util_row {txt inst} {
    foreach line [split $txt "\n"] {
        set f {}
        foreach x [split $line "|"] { lappend f [string trim $x] }
        if {[lindex $f 1] eq $inst} {
            set bram [expr {[lindex $f 8] + [lindex $f 9] / 2.0}]
            return [list [lindex $f 3] [lindex $f 7] $bram [lindex $f 11]]
        }
    }
    return [list NA NA NA NA]
}
set hier [report_utilization -hierarchical -hierarchical_depth 2 -return_string]
lassign [util_row $hier hash_0]             ip_lut  ip_ff  ip_bram  ip_dsp
lassign [util_row $hier ${BD_NAME}_wrapper] tot_lut tot_ff tot_bram tot_dsp

set run_dir [get_property DIRECTORY [get_runs impl_1]]
set bit [file join $run_dir ${BD_NAME}_wrapper.bit]
set bin [file join $run_dir ${BD_NAME}_wrapper.bin]
set base "${PRJ_NAME}"                                  ;# kv260_<variant>_<ctrl>
file copy -force $bit [file join $HW_DIR $base.bit]
if {[file exists $bin]} { file copy -force $bin [file join $HW_DIR $base.bin] }

# .bit.bin for fpgautil / xmutil (bootgen -process_bitstream bin), as aes256_dma.bin of the
# supervisors. bootgen ships with Vivado and Vitis; skipped with a warning if not found.
set bootgen ""
set cands [list [auto_execok bootgen]]
if {[info exists ::env(XILINX_VIVADO)]} {
    lappend cands [file join $::env(XILINX_VIVADO) bin bootgen.bat] \
                  [file join [file dirname $::env(XILINX_VIVADO)] Vitis bin bootgen.bat]
}
foreach cand $cands {
    if {$cand ne "" && [file exists [lindex $cand 0]]} { set bootgen [lindex $cand 0]; break }
}
set bitbin_ok 0
if {$bootgen ne ""} {
    set bif [file join $HW_DIR $base.bif]
    set fh [open $bif w]
    puts $fh "all:\n\{\n  \[destination_device = pl\] $base.bit\n\}"
    close $fh
    set here [pwd]
    cd $HW_DIR
    if {[catch {exec $bootgen -image $base.bif -arch zynqmp -process_bitstream bin -w on} msg]} {
        puts "WARNING: build_bd: bootgen: $msg"
    }
    cd $here
    set bitbin_ok [file exists [file join $HW_DIR $base.bit.bin]]
} else {
    puts "WARNING: build_bd: bootgen not found, no .bit.bin (convert on the board or with Vitis)"
}

write_hw_platform -fixed -include_bit -force -file [file join $HW_DIR $base.xsa]

# Device tree overlay for the uniss_dma driver (same layout as pl_vpa.dtsi of the supervisors)
set dtsi [file join $OUT_DIR pl.dtsi]
set fh [open $dtsi w]
puts $fh "/dts-v1/;
/plugin/;

/*
 * pl.dtsi - device tree overlay for $PRJ_NAME (B-04, generated by scripts/vivado/build_bd.tcl)
 * Driver: uniss_dma (mdc-suite/AXI-based-driver-applications, kv260/ubuntu-driver)
 *   channel 0 = axi_dma_0 MM2S  -> hash_0 input stream  (64-bit, xlnx,datawidth = 0x40)
 *   channel 1 = axi_dma_1 S2MM  <- hash_0 output stream
 * Build : dtc -@ -I dts -O dtb -o pl.dtbo pl.dtsi
 * Load  : sudo fpgautil -b $base.bit.bin   then   sudo fpgautil -o pl.dtbo
 */

/ {
    fragment@0 {
        target-path = \"/\";
        __overlay__ {
            axi_dma_0: axi_dma_0@[string range [format %x $ADDR_DMA0] 0 end] {
                compatible = \"xlnx,axi-dma\";
                status = \"okay\";
                reg = <0x0 [format 0x%x $ADDR_DMA0] 0x0 [format 0x%x $ADDR_RANGE]>;
                mm2s-channels = <0x1>;
                s2mm-channels = <0x0>;
                dma_channel_[format %x $ADDR_DMA0]: dma-channel@[format %x $ADDR_DMA0] {
                    compatible = \"xlnx,axi-dma-mm2s-channel\";
                    xlnx,datawidth = <0x40>;
                    xlnx,device-id = <0x0>;
                    dma-channels = <0x1>;
                    mm2s = <0x1>;
                };
            };

            axi_dma_1: axi_dma_1@[format %x $ADDR_DMA1] {
                compatible = \"xlnx,axi-dma\";
                status = \"okay\";
                reg = <0x0 [format 0x%x $ADDR_DMA1] 0x0 [format 0x%x $ADDR_RANGE]>;
                mm2s-channels = <0x0>;
                s2mm-channels = <0x1>;
                dma_channel_[format %x [expr {$ADDR_DMA1 + 0x30}]]: dma-channel@[format %x [expr {$ADDR_DMA1 + 0x30}]] {
                    compatible = \"xlnx,axi-dma-s2mm-channel\";
                    xlnx,datawidth = <0x40>;
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
if {$CTRL eq "lite"} {
    puts $fh "
/*
 * hash_0 control registers (lite version): AXI4-Lite at [format 0x%08X $ADDR_IP], not described in
 * the overlay (no kernel driver binds them); the test application maps them through /dev/mem.
 */"
}
close $fh

# Summary (one line per key, read by the final report)
set sum [file join $OUT_DIR summary.txt]
set fh [open $sum w]
puts $fh "design:        $DESIGN"
puts $fh "variant:       $VARIANT"
puts $fh "ctrl:          $CTRL"
puts $fh "ip_vlnv:       $IP_VLNV"
puts $fh "ip_source:     $IP_DIR"
puts $fh "board_part:    [expr {$BOARD eq "" ? "none (ps_kv260_preset.tcl)" : $BOARD}]"
puts $fh "pl_clk0_MHz:   $PL_MHZ"
puts $fh "wns_ns:        $wns"
puts $fh "whs_ns:        $whs"
puts $fh "timing_met:    [expr {$timing_ok ? "yes" : "NO"}]"
puts $fh "ip_LUT:        $ip_lut"
puts $fh "ip_FF:         $ip_ff"
puts $fh "ip_BRAM:       $ip_bram"
puts $fh "ip_DSP:        $ip_dsp"
puts $fh "total_LUT:     $tot_lut"
puts $fh "total_FF:      $tot_ff"
puts $fh "total_BRAM:    $tot_bram"
puts $fh "total_DSP:     $tot_dsp"
puts $fh "bitstream:     hw/$base.bit"
puts $fh "bit_bin:       [expr {$bitbin_ok ? "hw/$base.bit.bin" : "MISSING"}]"
puts $fh "xsa:           hw/$base.xsa"
puts $fh "address_map:"
foreach a $amap { puts $fh "  $a" }
close $fh

puts "=== build_bd: done (\[$DESIGN\] $VARIANT ctrl=$CTRL): WNS $wns ns, WHS $whs ns, hash_0 $ip_lut LUT / $ip_ff FF / $ip_bram BRAM ==="
puts "=== build_bd: outputs in $OUT_DIR ==="
