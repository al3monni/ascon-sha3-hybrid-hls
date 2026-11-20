# =============================================================================
# tp_analysis.tcl - LOG-016: throughput / area of every RTL of an HLS clock sweep,
#                   each RTL taken at its own Fmax (fmax_search.tcl)
# Location : <repo>/scripts/hls/tp_analysis.tcl
# Usage    : vitis-run --mode hls --tcl scripts/hls/tp_analysis.tcl     (or plain tclsh)
#            optional environment: VARIANT (default Hybrid-Test), CTRL (default ib),
#              TP_ALGOS (default: the non-"v" configurations of TP_CFG in <VARIANT>/main.cpp),
#              REF_T (reference RTL, default 10 = RTL of the b04 bitstreams), LIN_TOL (default 1 %)
#
# Inputs (no tool is run, results only):
#   results/hls/<D>/<VDIR>/T_<ns>/cosim/<algo>.rpt   throughput cosim (run_cosim.tcl + collect.tcl)
#   results/fmax/<D>/<VDIR>/T_<ns>/fmax.txt          Fmax and area at Fmax (fmax_search.tcl)
#   results/fmax/<D>/<VDIR>/T_<ns>/points.csv        DSP / BRAM of the Fmax probe
#   <VARIANT>/main.cpp                               TP_CFG: rate r, len0, len1 of each config
#
# Definitions
#   k      = (len1 - len0) / r                              extra blocks between the two messages
#   C_blk  = (L(len1) - L(len0)) / k                        cycles per block
#            lite (ap_ctrl_hs): L(len0) = min, L(len1) = max of the Verilog row
#            ib (ap_ctrl_none): latencies counted from the start of the simulation, so
#                               L(len1) = max - min and C_blk = (max - 2*min) / k
#                               (checked on b04 T_10: 3290 / 3268 / 449, as in LOG-015)
#   TP     = r * 8 * Fmax / C_blk                           [Mbit/s, Fmax in MHz]
#   TP/A   = TP / (LUT_at_Fmax / 1000)                      [Mbit/s per kLUT]
#   score  = geometric mean of TP/A over TP_ALGOS           aggregate criterion (ranking)
#   Pareto = per algorithm, on (TP max, LUT min) over the RTLs of the sweep
#   lin    = C_blk of the "v" config (other lengths) vs the main config: OK if within LIN_TOL
#
# Output   : results/fmax/<D>/<VDIR>/tp_sweep.csv   one row per RTL
#            results/fmax/<D>/<VDIR>/tp_sweep.txt   readable table, ranking, Pareto, warnings
# All values are post-route OOC (IP alone) and cosim cycles: no csynth estimate is used.
# =============================================================================

# [OPT] same default as fmax_search.tcl: the sweep is on the in-band IP
if {![info exists ::env(CTRL)] || $::env(CTRL) eq ""} { set ::env(CTRL) ib }

# --- env_or, ROOT, DESIGN, CTRL, variant_dir ---
source [file join [file dirname [file normalize [info script]]] common.tcl]

set VARIANT [env_or VARIANT Hybrid-Test]
set REF_T   [env_or REF_T 10]
set LIN_TOL [env_or LIN_TOL 1]
set VDIR    [variant_dir $VARIANT $CTRL]
set HLS_DIR [file join $ROOT results hls  $DESIGN $VDIR]
set OUT     [file join $ROOT results fmax $DESIGN $VDIR]
file mkdir $OUT

proc rd {path} {
    if {![file exists $path]} { return "" }
    set fh [open $path r]; set t [read $fh]; close $fh
    return $t
}
proc is_num {x} { return [string is double -strict $x] }
# (if, not expr: expr would turn "25.60" back into 25.6)
proc fmt {x {f %.1f}} { if {[is_num $x]} { return [format $f $x] }; return NA }

# --- TP_CFG of the testbench: name -> {rate len0 len1} (last three numbers of each entry) ---
set CFG [dict create]
foreach line [split [rd [file join $ROOT $VARIANT main.cpp]] "\n"] {
    if {[regexp {^\s*\{\s*"([A-Za-z0-9-]+)"\s*,([0-9,\s]+),\s*(//.*)?$} $line -> name nums]} {
        set n [regexp -all -inline {\d+} $nums]
        if {[llength $n] >= 3} { dict set CFG $name [lrange $n end-2 end] }
    }
}
if {[dict size $CFG] == 0} { error "No TP_CFG entry found in $VARIANT/main.cpp" }
set main_cfgs {}
foreach a [dict keys $CFG] { if {[string index $a end] ne "v"} { lappend main_cfgs $a } }
set TP_ALGOS [env_or TP_ALGOS $main_cfgs]

# C_blk from one throughput cosim report ("" if missing / not Pass)
proc c_blk {tdir algo} {
    if {![dict exists $::CFG $algo]} { return "" }
    lassign [dict get $::CFG $algo] r l0 l1
    set k [expr {($l1 - $l0) / double($r)}]
    if {$k <= 0 || $k != int($k)} { error "TP_CFG $algo: (len1 - len0) / r = $k is not a positive integer" }
    set t [rd [file join $tdir cosim $algo.rpt]]
    if {![regexp {\|\s*Verilog\|\s*(\w+)\|\s*(\d+)\|\s*\S+\|\s*(\d+)\|} $t -> st mn mx]} { return "" }
    if {$st ne "Pass"} { return "" }
    if {$::CTRL eq "ib"} {
        return [expr {($mx - 2.0 * $mn) / $k}]
    }
    return [expr {($mx - $mn) / $k}]
}

# key: value file (fmax.txt): first word of the value, or the whole value with all = 1
proc kv {path key {all 0}} {
    if {[regexp -line "^${key}:\\s*(.*)\$" [rd $path] -> v]} {
        set v [string trim $v]
        return [expr {$all ? $v : [lindex [split $v] 0]}]
    }
    return NA
}

# DSP / BRAM of the probe at Fmax (points.csv)
proc probe_col {tdir f col} {
    set lines [split [string trim [rd [file join $tdir points.csv]]] "\n"]
    set hdr [split [lindex $lines 0] ,]
    set i [lsearch -exact $hdr $col]
    foreach line [lrange $lines 1 end] {
        set v [split $line ,]
        if {[is_num $f] && [is_num [lindex $v 0]] && [lindex $v 0] == $f} { return [lindex $v $i] }
    }
    return NA
}

# --- One row per RTL of the sweep -----------------------------------------------
set ROWS {}
set WARN {}
# RTLs sorted by T_HLS (T_6p67 -> 6.67)
proc t_of {tdir} { return [string map {p .} [string range [file tail $tdir] 2 end]] }
set TDIRS [lsort -command {apply {{a b} {
    expr {[t_of $a] < [t_of $b] ? -1 : ([t_of $a] > [t_of $b] ? 1 : 0)}
}}} [glob -nocomplain -types d -directory $HLS_DIR T_*]]
foreach tdir $TDIRS {
    set tag [file tail $tdir]
    set T   [t_of $tdir]
    set fdir [file join $OUT $tag]
    set fx  [file join $fdir fmax.txt]
    set row [dict create tag $tag T $T]
    dict set row fmax [kv $fx fmax_MHz]
    dict set row LUT  [kv $fx LUT_at_fmax]
    dict set row FF   [kv $fx FF_at_fmax]
    dict set row stop [expr {[file exists $fx] ? [kv $fx stop 1] : "no Fmax search"}]
    dict set row DSP  [probe_col $fdir [dict get $row fmax] DSP]
    dict set row BRAM [probe_col $fdir [dict get $row fmax] BRAM]
    if {![is_num [dict get $row fmax]]} { lappend WARN "$tag: no Fmax ([dict get $row stop])" }
    # no failing probe yet -> the true Fmax is higher; MAX_RUNS -> resolution not reached
    if {[is_num [dict get $row fmax]] && [kv $fx lowest_fail_MHz] eq "NA"} {
        lappend WARN "$tag: no failing probe ([dict get $row stop]): Fmax is a lower bound"
    } elseif {[string match "MAX_RUNS*" [dict get $row stop]]} {
        lappend WARN "$tag: Fmax search stopped by MAX_RUNS: resolution not reached"
    }
    set logs {}
    foreach a $TP_ALGOS {
        lassign [dict get $CFG $a] r l0 l1
        set c [c_blk $tdir $a]
        set tp NA; set tpa NA
        if {[is_num $c] && $c > 0 && [is_num [dict get $row fmax]]} {
            set tp [expr {$r * 8.0 * [dict get $row fmax] / $c}]
            if {[is_num [dict get $row LUT]] && [dict get $row LUT] > 0} {
                set tpa [expr {$tp / ([dict get $row LUT] / 1000.0)}]
                lappend logs [expr {log($tpa)}]
            }
        }
        if {![is_num $c]} { lappend WARN "$tag: no valid cosim/$a.rpt" }
        dict set row c_$a $c
        dict set row tp_$a $tp
        dict set row tpa_$a $tpa
        # linearity: same algorithm, other message lengths
        set lin NA
        if {[dict exists $CFG ${a}v]} {
            set cv [c_blk $tdir ${a}v]
            if {[is_num $cv] && [is_num $c] && $c > 0} {
                set dev [expr {100.0 * abs($cv - $c) / $c}]
                set lin [expr {$dev <= $LIN_TOL ? "OK" : "CHECK"}]
                if {$lin eq "CHECK"} { lappend WARN [format "%s: %s C_blk %.1f vs %sv %.1f (%.1f %%)" $tag $a $c $a $cv $dev] }
            }
        }
        dict set row lin_$a $lin
    }
    dict set row score [expr {[llength $logs] == [llength $TP_ALGOS] ? exp([tcl::mathop::+ {*}$logs] / [llength $logs]) : "NA"}]
    lappend ROWS $row
}
if {[llength $ROWS] == 0} { error "No T_* point in $HLS_DIR (run the sweep and collect.tcl first)" }

# --- Pareto per algorithm: (TP max, LUT min) ---------------------------------------
foreach a $TP_ALGOS {
    set new {}
    foreach p $ROWS {
        set par NA
        if {[is_num [dict get $p tp_$a]] && [is_num [dict get $p LUT]]} {
            set par yes
            foreach q $ROWS {
                if {$q eq $p || ![is_num [dict get $q tp_$a]] || ![is_num [dict get $q LUT]]} { continue }
                set tq [dict get $q tp_$a]; set tp [dict get $p tp_$a]
                set lq [dict get $q LUT];   set lp [dict get $p LUT]
                if {$tq >= $tp && $lq <= $lp && ($tq > $tp || $lq < $lp)} { set par no; break }
            }
        }
        dict set p par_$a $par
        lappend new $p
    }
    set ROWS $new
}

# --- Ranking on the aggregate score --------------------------------------------------
set valid {}
foreach p $ROWS { if {[is_num [dict get $p score]]} { lappend valid $p } }
set ranked [lsort -decreasing -command {apply {{a b} {
    expr {[dict get $a score] < [dict get $b score] ? -1 : ([dict get $a score] > [dict get $b score] ? 1 : 0)}
}}} $valid]
set RANK [dict create]
set i 0
foreach p $ranked { dict set RANK [dict get $p tag] [incr i] }

# --- tp_sweep.csv ------------------------------------------------------------------------
set fh [open [file join $OUT tp_sweep.csv] w]
set hdr {T_hls_ns fmax_MHz LUT FF DSP BRAM}
foreach a $TP_ALGOS { lappend hdr c_blk_$a tp_${a}_Mbps tpa_${a}_Mbps_kLUT pareto_$a lin_$a }
lappend hdr score_geomean_tpa rank reference fmax_stop
puts $fh [join $hdr ,]
foreach p $ROWS {
    set v [list [dict get $p T] [dict get $p fmax] [dict get $p LUT] [dict get $p FF] [dict get $p DSP] [dict get $p BRAM]]
    foreach a $TP_ALGOS {
        lappend v [fmt [dict get $p c_$a]] [fmt [dict get $p tp_$a]] [fmt [dict get $p tpa_$a] %.2f] \
                  [dict get $p par_$a] [dict get $p lin_$a]
    }
    set tag [dict get $p tag]
    lappend v [fmt [dict get $p score] %.2f] [expr {[dict exists $RANK $tag] ? [dict get $RANK $tag] : "NA"}] \
              [expr {[dict get $p T] == $REF_T ? "yes" : "no"}] [string map {, ;} [dict get $p stop]]
    puts $fh [join $v ,]
}
close $fh

# --- tp_sweep.txt --------------------------------------------------------------------------
set fh [open [file join $OUT tp_sweep.txt] w]
puts $fh "LOG-016 - throughput/area of the HLS clock sweep, each RTL at its own Fmax"
puts $fh "design: $DESIGN   variant: $VDIR   ctrl: $CTRL   reference RTL: T_HLS = $REF_T ns (b04 bitstreams)"
puts $fh "TP = r*8*Fmax/C_blk \[Mbit/s\]; TP/A = TP per kLUT at Fmax; score = geometric mean of TP/A over: $TP_ALGOS"
puts $fh "Fmax and area: Vivado OOC post-route (fmax_search.tcl); C_blk: cosim ([expr {$CTRL eq "ib" ? "(max - 2*min)/k" : "(max - min)/k"}])"
puts $fh ""
puts $fh [format "%-6s %6s %6s %6s | %s | %8s %4s" T_ns Fmax LUT FF \
    [join [lmap a $TP_ALGOS { format "%-28s" "$a: C_blk TP TP/kLUT P" }] " | "] score rank]
foreach p $ROWS {
    set cells {}
    foreach a $TP_ALGOS {
        lappend cells [format "%-28s" [format "%7s %7s %7s %s" [fmt [dict get $p c_$a]] [fmt [dict get $p tp_$a]] \
            [fmt [dict get $p tpa_$a]] [expr {[dict get $p par_$a] eq "yes" ? "*" : " "}]]]
    }
    set tag [dict get $p tag]
    puts $fh [format "%-6s %6s %6s %6s | %s | %8s %4s%s" [dict get $p T] [dict get $p fmax] [dict get $p LUT] [dict get $p FF] \
        [join $cells " | "] [fmt [dict get $p score] %.2f] [expr {[dict exists $RANK $tag] ? [dict get $RANK $tag] : "NA"}] \
        [expr {[dict get $p T] == $REF_T ? "  (reference)" : ""}]]
}
puts $fh ""
puts $fh "P: * = on the Pareto front (TP max, LUT min) of that algorithm"
puts $fh "Ranking (score): [join [lmap p $ranked { format "%s ns" [dict get $p T] }] {, }]"
puts $fh ""
puts $fh "Warnings:"
if {[llength $WARN] == 0} { puts $fh "  none" }
foreach w $WARN { puts $fh "  - $w" }
close $fh

puts [rd [file join $OUT tp_sweep.txt]]
puts "=== tp_analysis: [file join $OUT tp_sweep.csv] ==="
exit
