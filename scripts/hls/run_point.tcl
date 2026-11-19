# =============================================================================
# run_point.tcl - Tcl HLS flow: one variant x one HLS clock target
#   csim -> csynth -> cosim -> export_design -flow impl (Vivado, out-of-context)
#
# Location : <repo>/scripts/hls/run_point.tcl
# Launch   : vitis-run --mode hls --tcl scripts\hls\run_point.tcl   (from repo root)
#            or scripts\hls\run_all.bat for all three accelerators
#
# Parameters (environment variables):
#   VARIANT  Hybrid-Test | Sha-Test | Ascon-Test       (default: Hybrid-Test)
#   T_HLS    HLS clock target [ns]                     (default: 10)
#   STEPS    subset of "csim cosim impl"               (default: all; csynth always runs)
#   DESIGN   design label (default: scripts/hls/design.txt, see common.tcl)
#   AUTO_PARTITION  off | auto  (default: off, see below)
#   CTRL     lite | ib  (default: lite)                 [OPT] B-04, see common.tcl
#              lite = AXI4-Lite control (b03 interface)
#              ib   = in-band: -DHASH_INBAND, header beat + status beat, ap_ctrl_none
#   F_VIVADO Fmax search only [MHz] (default: unset)    [OPT] LOG-016, see fmax_search.tcl
#              clock of the Vivado OOC run (config_export -vivado_clock); csynth keeps T_HLS,
#              so the RTL is the same as the T_<T_HLS> point
#
# Output   : build/hls/<DESIGN>/<VARIANT>[-ib]/T_<T_HLS>/   (dots -> 'p', e.g. T_6p67)
#            IP for Vivado: <point>/hls/impl/ip/  VLNV unica.it:hls:hash_<v>_<ctrl>:1.0
#            with F_VIVADO: build/fmax/<DESIGN>/<VARIANT>[-ib]/F_<MHz>/ (not seen by collect.tcl)
#
# -----------------------------------------------------------------------------
# [OPT] B-04: comparison with the reference flow of the supervisors
#   mdc-suite/AXI-based-driver-applications, kv260/applications/vpa/hls/run_hls.tcl
#   (Vitis HLS 2021.2, launched with: vitis_hls -f run_hls.tcl)
#
#   run_hls.tcl (theirs)                      | run_point.tcl (ours, Vitis 2025.1.1)
#   ------------------------------------------+-----------------------------------------------
#   open_project -reset prj_VPA_IP            | open_component -reset T_<ns> -flow_target vivado
#   open_solution -reset solution1            |   (2025.1 Unified flow: project + solution in
#     -flow_target vivado                     |    one "component"; same meaning)
#   add_files <design files>                  | add_files hash.cpp, hash.h  (+ -DHASH_INBAND if ib)
#   add_files -tb tb.cpp                      | add_files -tb main.cpp      (+ -DHASH_INBAND if ib)
#   set_top top_module                        | set_top hash_top
#   set_part {xck26-sfvc784-2LV-c}            | set_part xck26-sfvc784-2LV-c           (same part)
#   create_clock -period "350MHz"             | create_clock -period <T_HLS> (10 ns = 100 MHz)
#   (uncertainty: tool default)               | set_clock_uncertainty 27% (= default, explicit)
#   config_compile -enable_auto_rewind=0      | config_compile -enable_auto_rewind=true (all
#                                             |   variants alike, see AUTO_REWIND below)
#   (array partition: tool default)           | config_array_partition -throughput_driven off
#   source directives.tcl (set_directive_top) | pragmas in the sources (INTERFACE, PIPELINE,
#                                             |   INLINE, ALLOCATION...), nothing in Tcl
#   csim_design                               | csim_design -O -clean
#   csynth_design                             | csynth_design
#   #cosim_design   (commented out)           | cosim_design -O  (KAT; run_cosim.tcl: throughput)
#   export_design -format ip_catalog          | config_export -vendor/-library/-ipname/-version
#     -display_name VPA_IP --ipname VPA_IP    |   -display_name, then export_design -format
#                                             |   ip_catalog [-flow impl]
#   (no Vivado run)                           | -flow impl: out-of-context synth + place&route
#                                             |   of the IP alone -> post-route area / timing
#   exit                                      | exit
#
#   Same steps and same order (csim -> csynth -> [cosim] -> export ip_catalog). Differences:
#   - cosim: they skip it; we run it because the cycle counts of the thesis come from cosim
#   - -flow impl: extra OOC implementation for area/Fmax (does not change the exported IP)
#   - IP name: they set it on export_design; we set it with config_export (same effect), so
#     that lite/ib and the three variants get distinct VLNVs in the same Vivado catalog
#   - auto_rewind: they disable it; we keep the tool default (true) for all variants (LOG-010)
#   - clock: target of the IP synthesis only; the board clock is set in Vivado (pl_clk0)
# -----------------------------------------------------------------------------
# =============================================================================

# --- Settings shared by all variants (ported from */hls_config.cfg) ---
set PART        xck26-sfvc784-2LV-c   ;# cfg: part=
set TOP         hash_top              ;# cfg: syn.top=
set UNCERTAINTY 27%                   ;# Vitis HLS default, made explicit for traceability

# [WAS] Ascon-Test/hls_config.cfg: syn.compile.enable_auto_rewind=0 (Hybrid/Sha: default true)
# [OPT] Same value for all variants, so that hybrid vs standalone is a like-for-like comparison
set AUTO_REWIND true

# [WAS] Vitis HLS default: config_array_partition -throughput_driven auto
# [OPT] off: every partitioning of the state must be an explicit, annotated pragma.
#       With auto, after B-01 HLS inferred cyclic factor=3 on the Hybrid state (LOG-013):
#       bank = word % 3 -> sequential urem (35 cycles) + 4 DSP on every byte access.
#       Baseline unaffected: no automatic partitioning (HLS 214-270) in any of its 12 logs.

# --- env_or, ROOT, DESIGN ---
source [file join [file dirname [file normalize [info script]]] common.tcl]

# --- Parameters from environment variables ---
set VARIANT [env_or VARIANT Hybrid-Test]
set T_HLS   [env_or T_HLS 10]
set STEPS   [env_or STEPS "csim cosim impl"]
set AUTO_PARTITION [env_or AUTO_PARTITION off]
if {$AUTO_PARTITION ni {off auto}} { error "AUTO_PARTITION must be off or auto: $AUTO_PARTITION" }
# [OPT] B-04: control version (CTRL, VDIR, CTRL_CFLAGS from common.tcl)
set VDIR [variant_dir $VARIANT $CTRL]
# [OPT] LOG-016: Fmax search point (fixed RTL, tighter clock for the Vivado OOC run only)
set F_VIVADO [env_or F_VIVADO ""]
if {$F_VIVADO ne "" && !([string is double -strict $F_VIVADO] && $F_VIVADO > 0)} {
    error "F_VIVADO must be a frequency in MHz: '$F_VIVADO'"
}

# --- Paths ---
set SRC  [file join $ROOT $VARIANT]
# [WAS] hash.c / main.c hard-coded
# [OPT] C++ (.cpp, AXI-Stream variants: hls::stream + ap_axiu, LOG-014) or C (.c) sources
proc src_file {dir base} {
    foreach ext {cpp c} {
        set f [file join $dir $base.$ext]
        if {[file exists $f]} { return $f }
    }
    error "Sources not found: $base.cpp / $base.c in $dir"
}
set SRC_HASH [src_file $SRC hash]
set SRC_TB   [src_file $SRC main]

set TAG  "T_[string map {. p} $T_HLS]"
# [WAS] set WORK [file join $ROOT build hls $DESIGN $VARIANT]
# [OPT] B-04: <VARIANT>-ib for the in-band version (collect.tcl sees it as one more variant)
set WORK [file join $ROOT build hls $DESIGN $VDIR]
# [OPT] LOG-016: Fmax points in their own tree, so that the T_<ns> point (IP of the
#       bitstreams) and results/hls/<DESIGN>/summary.csv are never touched by the search
set VIVADO_OPTS {}
if {$F_VIVADO ne ""} {
    set TAG  "F_[string map {. p} $F_VIVADO]"
    set WORK [file join $ROOT build fmax $DESIGN $VDIR]
    lappend VIVADO_OPTS -vivado_clock [format %.4f [expr {1000.0 / $F_VIVADO}]]
}
file mkdir $WORK
cd $WORK

puts "=== run_point: \[$DESIGN\] $VARIANT ctrl=$CTRL @ $T_HLS ns (auto partition: $AUTO_PARTITION) -> [file join $WORK $TAG] ==="
if {$F_VIVADO ne ""} { puts "=== run_point: Fmax point, Vivado OOC clock $F_VIVADO MHz ($VIVADO_OPTS) ===" }

# --- Component setup (open_component replaces open_project + open_solution) ---
open_component -reset $TAG -flow_target vivado           ;# cfg: flow_target=vivado

# [OPT] -reset does not touch cosim/ (written by run_cosim.tcl): a stale cosim/kat.rpt of the
#       previous synthesis would be archived by collect.tcl as the KAT report of this one
file delete -force [file join $WORK $TAG cosim]
set_top $TOP
# [WAS] add_files $SRC_HASH / add_files -tb $SRC_TB
# [OPT] B-04: -cflags selects the control version (ib: -DHASH_INBAND) for synthesis, csim and
#       cosim alike; lite: empty flags -> same sources and settings as b03
#       (-cflags is passed only when not empty: lite writes exactly the b03 cfg)
proc add_src {args} {
    if {$::CTRL_CFLAGS ne ""} { add_files {*}$args -cflags $::CTRL_CFLAGS } else { add_files {*}$args }
}
add_src           $SRC_HASH                               ;# cfg: syn.file= (+ syn.file_cflags)
add_files         [file join $SRC hash.h]
add_src       -tb $SRC_TB                                 ;# cfg: tb.file=  (+ tb.file_cflags)
set_part $PART
create_clock -period $T_HLS -name default                 ;# cfg: clock=
set_clock_uncertainty $UNCERTAINTY
config_compile -enable_auto_rewind=$AUTO_REWIND
config_array_partition -throughput_driven $AUTO_PARTITION  ;# cfg: syn.array_partition.throughput_driven=

# [OPT] B-04: IP identity in the Vivado catalog (theirs: export_design -display_name/-ipname).
#       Distinct names for the 3 variants x 2 versions, so that one ip_repo can hold all of them;
#       the Vivado script (scripts/vivado/build_bd.tcl) finds the IP by this VLNV.
config_export -vendor $IP_VENDOR -library $IP_LIBRARY -version $IP_VERSION \
              -ipname [ip_name $VARIANT $CTRL] -display_name [ip_display_name $VARIANT $CTRL] \
              -description "Ascon / SHA-3 hash core ($VARIANT, ctrl=$CTRL, design $DESIGN)" \
              {*}$VIVADO_OPTS
# [OPT] LOG-016: VIVADO_OPTS = -vivado_clock <ns> only for Fmax points (empty otherwise: the
#       OOC run uses T_HLS, as before). It constrains the Vivado run only, not the scheduling.

# --- Flow ---
if {"csim"  in $STEPS} { csim_design -O -clean }          ;# cfg: sim.O=1, csim.clean=1
csynth_design
if {"cosim" in $STEPS} { cosim_design -O }                ;# cfg: sim.O=1
# [WAS] if {"impl" in $STEPS} { export_design -flow impl -format ip_catalog }
# [OPT] B-04: the IP is always exported (Vivado needs it); -flow impl only adds the OOC run
if {"impl"  in $STEPS} {
    export_design -flow impl -format ip_catalog           ;# cfg: ip_catalog + vivado.flow=impl
} else {
    export_design -format ip_catalog                      ;# cfg: ip_catalog
}

puts "=== run_point: done (\[$DESIGN\] $VARIANT ctrl=$CTRL @ $T_HLS ns) ==="
exit
