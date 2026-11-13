# =============================================================================
# collect_vivado.tcl - B-04: one table with the system-level results of the KV260 builds
#   Reads results/vivado/<DESIGN>/<VARIANT>[-ib]/summary.txt (written by build_bd.tcl) and
#   writes results/vivado/<DESIGN>/summary.csv (one row per variant x control version).
#
# Location : <repo>/scripts/vivado/collect_vivado.tcl
# Launch   : vivado -mode batch -nojournal -nolog -source scripts/vivado/collect_vivado.tcl
#            (plain Tcl: also from the Vivado Tcl console or tclsh)
#
# Values are post-route, in the full system (PS + 2 AXI DMA + SmartConnect + IP) at pl_clk0:
# the ip_* columns are the hash_0 instance only (hierarchical utilization), total_* the whole PL.
# They differ from the out-of-context numbers of results/hls/ (IP alone, no I/O constraints).
# =============================================================================

source [file join [file dirname [file normalize [info script]]] .. hls common.tcl]

set OUT [file join $ROOT results vivado $DESIGN]
set COLS {variant ctrl pl_clk0_MHz timing_met wns_ns whs_ns
          ip_LUT ip_FF ip_BRAM ip_DSP total_LUT total_FF total_BRAM total_DSP bit_bin board_part}

set rows {}
foreach d [lsort [glob -nocomplain -type d -directory $OUT *]] {
    set f [file join $d summary.txt]
    if {![file exists $f]} { continue }
    set fh [open $f r]
    set txt [read $fh]
    close $fh
    array unset v
    foreach line [split $txt "\n"] {
        if {[regexp {^([A-Za-z0-9_]+):\s*(.*)$} $line -> k val]} { set v($k) [string trim $val] }
    }
    set row {}
    foreach c $COLS { lappend row [expr {[info exists v($c)] ? [string map {, ;} $v($c)] : "NA"}] }
    lappend rows $row
}

set csv [file join $OUT summary.csv]
set fh [open $csv w]
puts $fh [join $COLS ,]
foreach r $rows { puts $fh [join $r ,] }
close $fh

puts [format "%-12s %-5s %7s %7s | %6s %6s %5s | %7s %7s %6s" variant ctrl WNS timing ip_LUT ip_FF ip_BRAM tot_LUT tot_FF tot_BRAM]
foreach r $rows {
    lassign $r variant ctrl mhz tm wns whs il if ib id tl tf tb td
    puts [format "%-12s %-5s %7s %7s | %6s %6s %5s | %7s %7s %6s" $variant $ctrl $wns $tm $il $if $ib $tl $tf $tb]
}
puts "=== collect_vivado: \[$DESIGN\] [llength $rows] build(s) -> $csv ==="
