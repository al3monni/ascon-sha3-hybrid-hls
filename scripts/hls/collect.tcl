# =============================================================================
# collect.tcl - Collect the results of the Tcl HLS flow
#   Scans build/hls/<VARIANT>/T_<ns>/, copies the key reports into
#   results/hls/<VARIANT>/T_<ns>/ (tracked by git) and rewrites
#   results/hls/summary.csv (one row per variant x T_HLS).
#
# Location : <repo>/scripts/hls/collect.tcl
# Launch   : vitis-run --mode hls --tcl scripts\hls\collect.tcl   (from repo root)
#
# Column prefixes - never mix them up in the thesis:
#   csyn_   Vitis HLS csynth estimates (operator delay model, no RTL synthesis)
#   syn_    Vivado post-synthesis (out-of-context)
#   impl_   Vivado post-route (out-of-context)  -> values to report
#   cosim_  C/RTL co-simulation (cycles, aggregated over the testbench vectors)
# =============================================================================

# run with: collect.tcl -> scripts/hls -> scripts -> repo root
set ROOT  [file dirname [file dirname [file dirname [file normalize [info script]]]]]
set BUILD [file join $ROOT build hls]
set OUT   [file join $ROOT results hls]

set HEADER {variant T_hls_ns csim
            csyn_fmax_est_MHz csyn_LUT csyn_FF csyn_BRAM csyn_lat_max
            cosim cosim_lat_min cosim_lat_avg cosim_lat_max cosim_ii_min cosim_ii_avg cosim_ii_max
            syn_LUT syn_FF syn_BRAM
            impl_LUT impl_FF impl_DSP impl_BRAM impl_CLB
            cp_req_ns cp_syn_ns cp_impl_ns wns_ns timing f_impl_MHz}

# --- Helpers -----------------------------------------------------------------

proc col_read {path} {
    if {$path eq "" || ![file exists $path]} { return "" }
    set fh [open $path r]
    set txt [read $fh]
    close $fh
    return $txt
}

# First file matching a glob pattern in a directory ("" if none)
proc col_find {dir pattern} {
    return [lindex [glob -nocomplain -directory $dir $pattern] 0]
}

# First integer in a report cell; "-" (resource not used) -> 0
proc col_int {cell} {
    if {[regexp {([0-9]+)} $cell -> n]} { return $n }
    return 0
}

# "<key>: <number>" at line start, first match in txt
proc col_val {txt key} {
    set re [format {^%s:\s*([-0-9.]+)} $key]
    if {[regexp -line $re $txt -> v]} { return $v }
    return NA
}

# Text from the last "#=== <marker> ===" to the end ("" if absent)
proc col_block {txt marker} {
    set i [string last "#=== $marker ===" $txt]
    if {$i < 0} { return "" }
    return [string range $txt $i end]
}

# T_6p67 -> 6.67
proc col_tns {tag} {
    return [string map {p .} [string range $tag 2 end]]
}

# --- Parsers (one per report) --------------------------------------------------

# C simulation: PASS / FAIL / NA
proc col_csim {comp} {
    set txt [col_read [col_find [file join $comp hls csim report] *_csim.log]]
    if {$txt eq ""} { return NA }
    if {[regexp {CSim done with 0 errors} $txt]} { return PASS }
    return FAIL
}

# csynth: estimated Fmax (from hls.log) + top-level row of csynth.rpt
# Row: |+ top | Issue | Slack | Lat(cycles) | Lat(ns) | IterLat | Interval | Trip | Pipelined | BRAM | DSP | FF | LUT | URAM |
proc col_csynth {comp} {
    set fmax NA
    regexp {Estimated Fmax:\s*([0-9.]+)\s*MHz} [col_read [file join $comp hls hls.log]] -> fmax
    set txt [col_read [file join $comp hls syn report csynth.rpt]]
    foreach line [split $txt "\n"] {
        if {[regexp {^\s*\|\+ } $line]} {
            set f [split $line "|"]
            return [list $fmax [col_int [lindex $f 13]] [col_int [lindex $f 12]] \
                               [col_int [lindex $f 10]] [col_int [lindex $f 4]]]
        }
    }
    return [list $fmax NA NA NA NA]
}

# cosim: Verilog row -> status lat_min lat_avg lat_max ii_min ii_avg ii_max
proc col_cosim {comp} {
    set txt [col_read [col_find [file join $comp hls sim report] *_cosim.rpt]]
    foreach line [split $txt "\n"] {
        set f {}
        foreach x [split $line "|"] { lappend f [string trim $x] }
        if {[lindex $f 1] eq "Verilog"} { return [lrange $f 2 8] }
    }
    return [lrepeat 7 NA]
}

# Vivado OOC run (impl/verilog/vivado.log):
#   syn_LUT syn_FF syn_BRAM impl_LUT impl_FF impl_DSP impl_BRAM impl_CLB
#   cp_req cp_syn cp_impl wns timing f_impl_MHz
proc col_vivado {comp} {
    set txt [col_read [file join $comp hls impl verilog vivado.log]]
    if {$txt eq ""} { return [lrepeat 14 NA] }

    set syn  [col_block $txt "Post-Synthesis Resource usage"]
    set impl [col_block $txt "Post-Implementation Resource usage"]

    set res {}
    foreach k {LUT FF BRAM}         { lappend res [col_val $syn  $k] }
    foreach k {LUT FF DSP BRAM CLB} { lappend res [col_val $impl $k] }

    set cp_impl [col_val $impl "CP achieved post-implementation"]
    lappend res [col_val $impl "CP required"] \
                [col_val $impl "CP achieved post-synthesis"] \
                $cp_impl

    set wns NA
    regexp {worst setup slack \(WNS\)=([-0-9.]+)} $txt -> wns
    lappend res $wns

    set timing NA
    if {[regexp -line {^Timing ([^\n]*)$} $impl -> t]} {
        set timing [string map {" " _} [string trim $t]]   ;# "met" / "not_met" ...
    }
    lappend res $timing

    if {[string is double -strict $cp_impl] && $cp_impl > 0} {
        lappend res [format %.1f [expr {1000.0 / $cp_impl}]]
    } else {
        lappend res NA
    }
    return $res
}

# Copy the key reports of one point into results/hls/<VARIANT>/<TAG>/
proc col_archive {comp dst} {
    file mkdir $dst
    foreach src [list \
        [file join $comp hls_config.cfg] \
        [file join $comp hls syn report csynth.rpt] \
        [col_find [file join $comp hls sim report] *_cosim.rpt] \
        [file join $comp hls impl report verilog export_syn.rpt] \
        [file join $comp hls impl report verilog export_impl.rpt]] {
        if {$src ne "" && [file exists $src]} { file copy -force $src $dst }
    }
}

# --- Main --------------------------------------------------------------------

set rows {}
foreach vdir [lsort [glob -nocomplain -type d -directory $BUILD *]] {
    set variant [file tail $vdir]
    set points {}
    foreach c [glob -nocomplain -type d -directory $vdir T_*] {
        lappend points [list [col_tns [file tail $c]] $c]
    }
    # Slowest clock first
    foreach p [lsort -real -decreasing -index 0 $points] {
        lassign $p tns comp
        col_archive $comp [file join $OUT $variant [file tail $comp]]
        lappend rows [concat $variant $tns [col_csim $comp] \
                             [col_csynth $comp] [col_cosim $comp] [col_vivado $comp]]
    }
}

file mkdir $OUT
set csv [file join $OUT summary.csv]
set fh [open $csv w]
puts $fh [join $HEADER ,]
foreach r $rows { puts $fh [join $r ,] }
close $fh

# Console summary (post-route values)
puts ""
puts [format "%-11s %6s %5s %5s | %6s %6s %4s | %8s %7s %7s | %s" \
      variant T_ns csim cosim LUT FF BRAM cp_impl f_MHz timing "cosim lat min/avg/max"]
foreach r $rows {
    array set v {}
    foreach k $HEADER x $r { set v($k) $x }
    puts [format "%-11s %6s %5s %5s | %6s %6s %4s | %8s %7s %7s | %s/%s/%s" \
          $v(variant) $v(T_hls_ns) $v(csim) $v(cosim) \
          $v(impl_LUT) $v(impl_FF) $v(impl_BRAM) $v(cp_impl_ns) $v(f_impl_MHz) $v(timing) \
          $v(cosim_lat_min) $v(cosim_lat_avg) $v(cosim_lat_max)]
}
puts ""
puts "=== collect: [llength $rows] point(s) -> $csv ==="
exit
