# =============================================================================
# fmax_search.tcl - LOG-016: post-route Fmax of one IP (Vivado out-of-context), fixed RTL
#
# Location : <repo>/scripts/hls/fmax_search.tcl
# Launch   : scripts\hls\run_fmax.bat [variants]     (one minimized console per variant)
#            or, for one variant:  set VARIANT=Hybrid-Test
#                                  vitis-run --mode hls --tcl scripts\hls\fmax_search.tcl
#
# Method (LOG-010, phase 2: "first the HLS trade-off, then the Fmax in Vivado on that RTL")
#   - RTL fixed: csynth at T_HLS = 10 ns, the RTL of the b04 bitstreams (same cycles per block).
#   - Only the clock constraint of the Vivado OOC run changes: run_point.tcl with F_VIVADO
#     (config_export -vivado_clock), synthesis + place & route with the tool defaults
#     (same strategy as every OOC result of results/hls/).
#   - Fmax = highest target frequency with timing met (post-route WNS >= 0), RES MHz resolution.
#   - Search, two modes:
#     grid (F_LIST set, the mode used by run_fmax.bat):
#       the listed frequencies in ascending order; with STOP_AT_FAIL = 1 the search stops at
#       the first one that does not close timing (Fmax = last PASS, resolution = grid step);
#       if the first point fails, one probe FALLBACK_STEP below it; TIME_BUDGET_MIN bounds
#       the duration (a probe starts only if it can end within the budget);
#     bisection (F_LIST empty):
#       1. first probe = OOC frequency achieved with the 10 ns constraint
#          (results/hls/<DESIGN>/summary.csv, f_impl_MHz), rounded down to RES;
#       2. while no probe has failed: next = highest pass + STEP_UP;
#       3. bisection between the highest pass and the lowest fail until hi - lo <= RES.
#     P&R is not monotonic in the constraint: the result is the highest PASS seen and every
#     probe is kept in points.csv. Each probe checks that the RTL is the one of T_<T_HLS>.
#
# Parameters (environment variables):
#   VARIANT   Hybrid-Test | Sha-Test | Ascon-Test   (default: Hybrid-Test)
#   CTRL      lite | ib                             (default here: ib)
#   T_HLS     HLS clock of the fixed RTL [ns]       (default: 10)
#   F_LIST    grid of probes [MHz], e.g. "200 225 250", or auto = per-variant grid (GRID_AUTO)
#             (default: empty = bisection; run_fmax.bat sets auto)
#   STOP_AT_FAIL  grid mode: 1 = stop at the first fail (default), 0 = run every point
#   FALLBACK_STEP grid mode: if the first point fails, one probe this many MHz below (default 25)
#   TIME_BUDGET_MIN grid mode: no new probe if it would end after this many minutes (0 = none)
#   F_START   bisection: first probe [MHz]          (default: from summary.csv, see above)
#   STEP_UP   [MHz] (default 25)   RES [MHz] (default 5)
#   F_CAP     highest probe [MHz] (default 500)     MAX_RUNS (default 10)
#
# Output   : build/fmax/<DESIGN>/<VDIR>/F_<MHz>/        HLS components (gitignored)
#            results/fmax/<DESIGN>/<VDIR>/points.csv    one row per probe
#            results/fmax/<DESIGN>/<VDIR>/F_<MHz>/export_impl.rpt
#            results/fmax/<DESIGN>/<VDIR>/fmax.txt      result of the search
#            logs/fmax/<VDIR>_F_<MHz>.log, logs/fmax/progress_<VDIR>.txt
#
# Values are post-route OOC (IP alone, no I/O constraints): they are not the frequency of the
# KV260 system, which also depends on DMA, SmartConnect and the PS clock dividers.
# =============================================================================

# [OPT] this search is meant for the in-band IPs: CTRL defaults to ib (common.tcl: lite)
if {![info exists ::env(CTRL)] || $::env(CTRL) eq ""} { set ::env(CTRL) ib }

# --- env_or, ROOT, DESIGN, CTRL, variant_dir ---
source [file join [file dirname [file normalize [info script]]] common.tcl]

set VARIANT  [env_or VARIANT Hybrid-Test]
set T_HLS    [env_or T_HLS 10]
set STEP_UP  [env_or STEP_UP 25]
set RES      [env_or RES 5]
set F_CAP    [env_or F_CAP 500]
set MAX_RUNS [env_or MAX_RUNS 10]
# [OPT] grid mode (fixed frequencies), chosen for LOG-016 instead of the bisection
set F_LIST   [env_or F_LIST ""]
set STOP_AT_FAIL [env_or STOP_AT_FAIL 1]
set TIME_BUDGET_MIN [env_or TIME_BUDGET_MIN 0]
set FALLBACK_STEP   [env_or FALLBACK_STEP 25]
# [OPT] F_LIST = auto: grid per variant around the OOC frequency reached at 10 ns
#       (results/hls/b04: Hybrid ib 182.0, SHA-3 ib 178.3, Ascon ib 265.4 MHz), 3 points each
#       so that the three searches fit in about one hour when run in parallel
set GRID_AUTO [dict create Hybrid-Test {200 225 250} Sha-Test {200 225 250} Ascon-Test {250 275 300}]
if {$F_LIST eq "auto"} { set F_LIST [dict get $GRID_AUTO $VARIANT] }
set T0 [clock seconds]
set DUR_MAX 0
set VDIR     [variant_dir $VARIANT $CTRL]

set T_TAG  "T_[string map {. p} $T_HLS]"
set REF    [file join $ROOT build hls $DESIGN $VDIR $T_TAG]   ;# point of the fixed RTL
set BUILD  [file join $ROOT build fmax $DESIGN $VDIR]
set OUT    [file join $ROOT results fmax $DESIGN $VDIR]
set LOGS   [file join $ROOT logs fmax]
set POINT_BAT [file nativename [file join $ROOT scripts hls run_fmax_point.bat]]
file mkdir $OUT $LOGS

# --- Helpers -----------------------------------------------------------------

proc rd {path} {
    if {![file exists $path]} { return "" }
    set fh [open $path r]
    set txt [read $fh]
    close $fh
    return $txt
}

proc progress {msg} {
    set line "\[[clock format [clock seconds] -format {%Y-%m-%d %H:%M:%S}]\] $msg"
    puts $line
    set fh [open [file join $::LOGS progress_$::VDIR.txt] a]
    puts $fh $line
    close $fh
}

# f in MHz -> component tag (F_200, F_192p5)
proc ftag {f} { return "F_[string map {. p} $f]" }

# Integer MHz when possible (200.0 -> 200), so that tags and csv stay readable
proc fnorm {f} {
    if {$f == int($f)} { return [expr {int($f)}] }
    return $f
}

# OOC frequency achieved at T_HLS by the reference point (summary.csv of the HLS flow)
proc ref_f_impl {} {
    set csv [file join $::ROOT results hls $::DESIGN summary.csv]
    set lines [split [string trim [rd $csv]] "\n"]
    if {[llength $lines] < 2} { return "" }
    set hdr [split [lindex $lines 0] ,]
    set iv [lsearch $hdr variant]
    set it [lsearch $hdr T_hls_ns]
    set ifr [lsearch $hdr f_impl_MHz]
    foreach line [lrange $lines 1 end] {
        set f [split $line ,]
        if {[lindex $f $iv] eq $::VDIR && [lindex $f $it] == $::T_HLS} { return [lindex $f $ifr] }
    }
    return ""
}

# export_impl.rpt -> dict (target, post_syn, post_route, wns, met, LUT, FF, DSP, BRAM)
#   "* Timing was met" and the Target / Post-Synthesis / Post-Route rows of the
#   timing summary table; WNS = target - post-route period
proc parse_impl {rpt} {
    set txt [rd $rpt]
    if {$txt eq ""} { return {} }
    set d [dict create met [regexp {\* Timing was met} $txt]]
    foreach {key row} {target Target post_syn Post-Synthesis post_route Post-Route} {
        set v NA
        regexp -line [format {^\|\s*%s\s*\|\s*([0-9.]+)} $row] $txt -> v
        dict set d $key $v
    }
    foreach k {LUT FF DSP BRAM} {
        set v NA
        regexp -line [format {^%s:\s*([0-9]+)} $k] $txt -> v
        dict set d $k $v
    }
    set t [dict get $d target]
    set p [dict get $d post_route]
    if {[string is double -strict $t] && [string is double -strict $p]} {
        dict set d wns [format %.3f [expr {$t - $p}]]
    } else {
        dict set d wns NA
    }
    return $d
}

# RTL of a point = Verilog of csynth without the comment lines (tool banner)
proc rtl_text {comp} {
    set out {}
    foreach v [lsort [glob -nocomplain -directory [file join $comp hls syn verilog] *.v]] {
        lappend out "## [file tail $v]"
        foreach line [split [rd $v] "\n"] {
            if {![regexp {^\s*//} $line]} { lappend out $line }
        }
    }
    return [join $out "\n"]
}

# First differing line of two texts (diagnostics of the RTL check)
proc first_diff {a b} {
    set la [split $a "\n"]
    set lb [split $b "\n"]
    for {set i 0} {$i < max([llength $la], [llength $lb])} {incr i} {
        if {[lindex $la $i] ne [lindex $lb $i]} { return "line $i: '[lindex $la $i]' vs '[lindex $lb $i]'" }
    }
    return "none"
}

# One probe: run_point.tcl with F_VIVADO = f (via run_fmax_point.bat: clean environment,
# STEPS=impl), then parse its report and archive it. Returns the parsed dict ("" on error).
proc run_probe {f} {
    set tag  [ftag $f]
    set comp [file join $::BUILD $tag]
    set log  [file nativename [file join $::LOGS ${::VDIR}_$tag.log]]
    progress "probe $f MHz: start"
    set rc 0
    if {[catch {exec cmd.exe /c $::POINT_BAT $::VARIANT $f $::CTRL $::T_HLS $log} msg]} {
        set rc 1
        progress "probe $f MHz: run_fmax_point returned an error ($msg), see $log"
    }
    set rpt [file join $comp hls impl report verilog export_impl.rpt]
    set d [parse_impl $rpt]
    if {[dict size $d] == 0} {
        progress "probe $f MHz: no export_impl.rpt (rc=$rc) -> search stopped"
        return ""
    }
    file mkdir [file join $::OUT $tag]
    file copy -force $rpt [file join $::OUT $tag]
    set rtl [rtl_text $comp]
    dict set d rtl_same [expr {$rtl eq $::REF_RTL ? "yes" : "no"}]
    if {$rtl ne $::REF_RTL} { progress "probe $f MHz: RTL differs from $::T_TAG, first difference: [first_diff $rtl $::REF_RTL]" }
    progress [format "probe %s MHz: %s, post-route %s ns (WNS %s), LUT %s FF %s, RTL same as %s: %s" \
        $f [expr {[dict get $d met] ? "PASS" : "FAIL"}] [dict get $d post_route] [dict get $d wns] \
        [dict get $d LUT] [dict get $d FF] $::T_TAG [dict get $d rtl_same]]
    return $d
}

proc write_points {} {
    set fh [open [file join $::OUT points.csv] w]
    puts $fh "f_target_MHz,T_target_ns,post_syn_ns,post_route_ns,wns_ns,timing,f_achieved_MHz,LUT,FF,DSP,BRAM,rtl_same,minutes"
    foreach f [lsort -real [dict keys $::POINTS]] {
        set d [dict get $::POINTS $f]
        set p [dict get $d post_route]
        set fa [expr {[string is double -strict $p] && $p > 0 ? [format %.1f [expr {1000.0 / $p}]] : "NA"}]
        puts $fh [join [list $f [dict get $d target] [dict get $d post_syn] $p [dict get $d wns] \
            [expr {[dict get $d met] ? "met" : "not_met"}] $fa \
            [dict get $d LUT] [dict get $d FF] [dict get $d DSP] [dict get $d BRAM] [dict get $d rtl_same] \
            [expr {[dict exists $d minutes] ? [dict get $d minutes] : "NA"}]] ,]
    }
    close $fh
}

# --- Main --------------------------------------------------------------------

if {![file isdirectory $REF]} { error "Reference point not found: $REF (run the HLS flow first)" }
set REF_RTL [rtl_text $REF]
if {$REF_RTL eq ""} { error "No csynth Verilog in $REF/hls/syn/verilog" }

set f [env_or F_START ""]
if {$F_LIST ne ""} {
    set F_LIST [lsort -real -unique $F_LIST]
    progress "=== fmax_search \[$DESIGN\] $VDIR: fixed RTL $T_TAG, grid $F_LIST MHz (stop at first fail: $STOP_AT_FAIL)"
} elseif {$f eq ""} {
    set fref [ref_f_impl]
    if {![string is double -strict $fref]} { error "No f_impl_MHz for $VDIR @ $T_HLS ns in results/hls/$DESIGN/summary.csv: set F_START" }
    set f [expr {int($fref / $RES) * $RES}]
    progress "=== fmax_search \[$DESIGN\] $VDIR: fixed RTL $T_TAG, OOC @ $T_HLS ns reached $fref MHz -> first probe $f MHz"
} else {
    progress "=== fmax_search \[$DESIGN\] $VDIR: fixed RTL $T_TAG, first probe $f MHz (F_START)"
}

set POINTS [dict create]
set lo ""          ;# highest target with timing met
set hi ""          ;# lowest target with timing not met
set stop_reason ""
if {$F_LIST ne ""} {
    # --- grid mode ---
    # [OPT] time-bounded: a probe starts only if (elapsed + longest probe so far) fits in
    #       TIME_BUDGET_MIN; if the first point fails, one probe FALLBACK_STEP MHz below it
    set queue $F_LIST
    set fallback_done 0
    while {[llength $queue] > 0} {
        set f [fnorm [lindex $queue 0]]
        set queue [lrange $queue 1 end]
        set elapsed [expr {[clock seconds] - $T0}]
        if {$TIME_BUDGET_MIN > 0 && [dict size $POINTS] > 0 && $elapsed + $DUR_MAX > $TIME_BUDGET_MIN * 60} {
            set stop_reason [format "time budget: %.0f min elapsed, next probe (%s MHz) would end after %s min" \
                [expr {$elapsed / 60.0}] $f $TIME_BUDGET_MIN]
            break
        }
        set t1 [clock seconds]
        set d [run_probe $f]
        set dur [expr {[clock seconds] - $t1}]
        if {$dur > $DUR_MAX} { set DUR_MAX $dur }
        if {$d eq ""} { set stop_reason "probe $f MHz failed to run"; break }
        dict set d minutes [format %.1f [expr {$dur / 60.0}]]
        dict set POINTS $f $d
        write_points
        if {[dict get $d rtl_same] ne "yes"} { set stop_reason "RTL of probe $f MHz differs from $T_TAG"; break }
        if {[dict get $d met]} {
            if {$lo eq "" || $f > $lo} { set lo $f }
        } else {
            if {$hi eq "" || $f < $hi} { set hi $f }
            if {$lo eq "" && !$fallback_done && $FALLBACK_STEP > 0} {
                # first grid point failed: one probe below it, then stop
                set fallback_done 1
                set queue [list [expr {$f - $FALLBACK_STEP}]]
                continue
            }
            if {$STOP_AT_FAIL} { set stop_reason "grid: first fail at $f MHz"; break }
        }
    }
    if {$stop_reason eq ""} {
        set stop_reason [expr {$fallback_done ? "grid: first point failed, fallback probe -$FALLBACK_STEP MHz run" : "grid: all points run"}]
    }
} else {
    # --- bisection mode ---
    for {set run 1} {$run <= $MAX_RUNS} {incr run} {
        set f [fnorm $f]
        if {[dict exists $POINTS $f]} { set stop_reason "probe $f MHz already run"; break }
        set d [run_probe $f]
        if {$d eq ""} { set stop_reason "probe $f MHz failed to run"; break }
        dict set POINTS $f $d
        write_points
        if {[dict get $d rtl_same] ne "yes"} { set stop_reason "RTL of probe $f MHz differs from $T_TAG"; break }

        if {[dict get $d met]} {
            if {$lo eq "" || $f > $lo} { set lo $f }
        } else {
            if {$hi eq "" || $f < $hi} { set hi $f }
        }
        # next probe
        if {$hi eq ""} {
            set f [expr {$lo + $STEP_UP}]
            if {$f > $F_CAP} { set stop_reason "no failure up to F_CAP = $F_CAP MHz"; break }
        } elseif {$lo eq ""} {
            # the first probe failed: go down towards the known pass at T_HLS
            set fl [expr {1000.0 / $T_HLS}]
            set f  [expr {round(($fl + $hi) / 2.0 / $RES) * $RES}]
            if {$hi - $fl <= $RES} { set stop_reason "fail $RES MHz above the T_HLS clock"; break }
        } else {
            if {$hi - $lo <= $RES} { set stop_reason "converged: hi - lo <= $RES MHz"; break }
            set f [expr {round(($lo + $hi) / 2.0 / $RES) * $RES}]
            if {$f <= $lo || $f >= $hi} { set f [expr {$lo + $RES}] }
        }
    }
}
if {$stop_reason eq ""} { set stop_reason "MAX_RUNS = $MAX_RUNS reached" }

# --- Result --------------------------------------------------------------------
set fa_lo NA
set lut_lo NA
set ff_lo NA
if {$lo ne ""} {
    set d [dict get $POINTS $lo]
    set fa_lo [format %.1f [expr {1000.0 / [dict get $d post_route]}]]
    set lut_lo [dict get $d LUT]
    set ff_lo [dict get $d FF]
}
set fh [open [file join $OUT fmax.txt] w]
puts $fh "design:            $DESIGN"
puts $fh "variant:           $VDIR"
puts $fh "ctrl:              $CTRL"
puts $fh "rtl:               $T_TAG (csynth @ $T_HLS ns, same RTL as the b04 IP)"
puts $fh "fmax_MHz:          [expr {$lo eq "" ? "NA" : $lo}]"
puts $fh "f_achieved_MHz:    $fa_lo"
puts $fh "LUT_at_fmax:       $lut_lo"
puts $fh "FF_at_fmax:        $ff_lo"
puts $fh "lowest_fail_MHz:   [expr {$hi eq "" ? "NA" : $hi}]"
puts $fh "method:            [expr {$F_LIST ne "" ? "grid $F_LIST MHz, stop at first fail: $STOP_AT_FAIL, fallback -$FALLBACK_STEP MHz, budget $TIME_BUDGET_MIN min" : "bisection, resolution $RES MHz"}]"
puts $fh "probes:            [dict size $POINTS]"
puts $fh "minutes:           [format %.0f [expr {([clock seconds] - $T0) / 60.0}]]"
puts $fh "stop:              $stop_reason"
puts $fh "source:            Vivado OOC post-route (export_design -flow impl, default strategies)"
close $fh
progress "=== fmax_search $VDIR DONE: Fmax = [expr {$lo eq "" ? "NA" : $lo}] MHz (achieved $fa_lo MHz), lowest fail [expr {$hi eq "" ? "NA" : $hi}] MHz; $stop_reason"
exit
