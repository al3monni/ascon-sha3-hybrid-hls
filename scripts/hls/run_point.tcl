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
#
# Output   : build/hls/<VARIANT>/T_<T_HLS>/   (dots -> 'p', e.g. T_6p67)
# =============================================================================

# --- Settings shared by all variants (ported from */hls_config.cfg) ---
set PART        xck26-sfvc784-2LV-c   ;# cfg: part=
set TOP         hash_top              ;# cfg: syn.top=
set UNCERTAINTY 27%                   ;# Vitis HLS default, made explicit for traceability

# [WAS] Ascon-Test/hls_config.cfg: syn.compile.enable_auto_rewind=0 (Hybrid/Sha: default true)
# [OPT] Same value for all variants, so that hybrid vs standalone is a like-for-like comparison
set AUTO_REWIND true

# --- Parameters from environment variables ---
proc env_or {name default} {
    if {[info exists ::env($name)] && $::env($name) ne ""} { return $::env($name) }
    return $default
}

set VARIANT [env_or VARIANT Hybrid-Test]
set T_HLS   [env_or T_HLS 10]
set STEPS   [env_or STEPS "csim cosim impl"]

# --- Paths ---
# run_point.tcl -> scripts/hls -> scripts -> repo root
set ROOT [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set SRC  [file join $ROOT $VARIANT]
if {![file exists [file join $SRC hash.c]]} { error "Sources not found in: $SRC" }

set TAG  "T_[string map {. p} $T_HLS]"
set WORK [file join $ROOT build hls $VARIANT]
file mkdir $WORK
cd $WORK

puts "=== run_point: $VARIANT @ $T_HLS ns -> [file join $WORK $TAG] ==="

# --- Component setup (open_component replaces open_project + open_solution) ---
open_component -reset $TAG -flow_target vivado           ;# cfg: flow_target=vivado
set_top $TOP
add_files     [file join $SRC hash.c]                     ;# cfg: syn.file=
add_files     [file join $SRC hash.h]
add_files -tb [file join $SRC main.c]                     ;# cfg: tb.file=
set_part $PART
create_clock -period $T_HLS -name default                 ;# cfg: clock=
set_clock_uncertainty $UNCERTAINTY
config_compile -enable_auto_rewind=$AUTO_REWIND

# --- Flow ---
if {"csim"  in $STEPS} { csim_design -O -clean }          ;# cfg: sim.O=1, csim.clean=1
csynth_design
if {"cosim" in $STEPS} { cosim_design -O }                ;# cfg: sim.O=1
if {"impl"  in $STEPS} { export_design -flow impl -format ip_catalog }  ;# cfg: ip_catalog + vivado.flow=impl

puts "=== run_point: done ($VARIANT @ $T_HLS ns) ==="
exit
