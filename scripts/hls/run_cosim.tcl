# =============================================================================
# run_cosim.tcl - Throughput cosim on an already synthesized point
#   Re-opens build/hls/<VARIANT>/T_<T_HLS>/ WITHOUT reset (csynth + impl kept)
#   and runs one cosim per algorithm in the testbench throughput mode
#   (main.c: argv = algorithm). Each report is saved as <point>/cosim/<algo>.rpt;
#   the KAT report of run_point.tcl is kept as <point>/cosim/kat.rpt.
#
# Location : <repo>/scripts/hls/run_cosim.tcl
# Launch   : vitis-run --mode hls --tcl scripts\hls\run_cosim.tcl   (from repo root)
#            or scripts\hls\cosim_all.bat for all variants x all points
#
# Parameters (environment variables):
#   VARIANT  Hybrid-Test | Sha-Test | Ascon-Test       (default: Hybrid-Test)
#   T_HLS    HLS clock target [ns] of an existing point (default: 10)
#   ALGOS    algorithms to run (default per variant, see DEFAULT_ALGOS);
#            add sha3-256v / sha3-512v for the linearity check
# =============================================================================

proc env_or {name default} {
    if {[info exists ::env($name)] && $::env($name) ne ""} { return $::env($name) }
    return $default
}

# Same names as TP_CFG[] in the three main.c
set DEFAULT_ALGOS [dict create \
    Hybrid-Test {sha3-256 sha3-512 ascon} \
    Sha-Test    {sha3-256 sha3-512} \
    Ascon-Test  {ascon}]

set VARIANT [env_or VARIANT Hybrid-Test]
set T_HLS   [env_or T_HLS 10]
if {![dict exists $DEFAULT_ALGOS $VARIANT]} { error "Unknown VARIANT: $VARIANT" }
set ALGOS   [env_or ALGOS [dict get $DEFAULT_ALGOS $VARIANT]]

# --- Paths (same layout as run_point.tcl) ---
set ROOT [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set TAG  "T_[string map {. p} $T_HLS]"
set WORK [file join $ROOT build hls $VARIANT]
set COMP [file join $WORK $TAG]

if {![file exists [file join $COMP hls syn report csynth.rpt]]} {
    error "Point not synthesized: $COMP (run run_point.tcl first)"
}

set RPT_DIR [file join $COMP hls sim report]
set OUT_DIR [file join $COMP cosim]
file mkdir $OUT_DIR

# Keep the KAT cosim report written by run_point.tcl (overwritten below)
set kat [lindex [glob -nocomplain -directory $RPT_DIR *_cosim.rpt] 0]
if {$kat ne "" && ![file exists [file join $OUT_DIR kat.rpt]]} {
    file copy $kat [file join $OUT_DIR kat.rpt]
}

# --- Re-open the point: no -reset, so csynth and impl results are kept ---
cd $WORK
open_component $TAG

set failed {}
foreach algo $ALGOS {
    puts "=== run_cosim: $VARIANT @ $T_HLS ns, algo=$algo ==="
    file delete -force [file join $OUT_DIR $algo.rpt]
    # Testbench returns nonzero on digest mismatch or unknown algo -> cosim error
    if {[catch {cosim_design -O -argv $algo} err]} {
        puts "ERROR: run_cosim: cosim failed for $algo: $err"
        lappend failed $algo
        continue
    }
    set rpt [lindex [glob -nocomplain -directory $RPT_DIR *_cosim.rpt] 0]
    file copy -force $rpt [file join $OUT_DIR $algo.rpt]
}

if {[llength $failed]} {
    puts "=== run_cosim: done with FAILURES ($VARIANT @ $T_HLS ns): $failed ==="
} else {
    puts "=== run_cosim: done ($VARIANT @ $T_HLS ns): $ALGOS ==="
}
exit
