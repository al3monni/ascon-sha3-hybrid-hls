# =============================================================================
# launch_b04.tcl - Start the B-04 batch flows in the background from a Tcl console
#   (Vivado GUI Tcl console or a Vivado Tcl shell)
#
# Location : <repo>/scripts/launch_b04.tcl
# Usage    : source C:/Users/monni/Desktop/Vitis-HLS_Components/scripts/launch_b04.tcl
#            b04 hls    lite|ib  [variants]   -> scripts\hls\run_b04.bat
#            b04 vivado lite|ib  [variants]   -> scripts\vivado\run_b04.bat
#            b04 all    lite|ib  [variants]   -> HLS then Vivado, one variant after the other
#
# [OPT] B-04: each call opens its own minimized console (cmd start) and returns at once,
#       so the Tcl console stays free; progress in logs\b04\progress_<ctrl>.txt.
#       The batch files clean the inherited AMD environment (scripts\clean_env.bat).
# =============================================================================

set B04_ROOT [file dirname [file dirname [file normalize [info script]]]]

proc b04 {what ctrl args} {
    set bat [dict get {hls scripts/hls/run_b04.bat vivado scripts/vivado/run_b04.bat all scripts/run_b04_all.bat} $what]
    set bat [file nativename [file join $::B04_ROOT $bat]]
    if {![file exists $bat]} { error "not found: $bat" }
    # "start" title must be quoted: a title with spaces is quoted by exec.
    # [WAS] exec without '&': the started console inherits the pipes of exec, so the Tcl
    #       console stayed busy ("Running source...") until the whole batch ended
    # [OPT] trailing '&' + output to NUL: exec returns at once, the batch runs on its own
    exec cmd.exe /c start "b04 $what $ctrl" /min $bat $ctrl {*}$args >& NUL &
    puts "started: $bat $ctrl $args"
}
