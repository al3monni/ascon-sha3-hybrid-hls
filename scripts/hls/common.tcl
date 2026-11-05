# =============================================================================
# common.tcl - Settings shared by the Tcl HLS scripts (sourced, not launched)
#
# Defines:
#   env_or   proc: environment variable or default
#   ROOT     repo root
#   DESIGN   design version label, namespaces the outputs:
#              build/hls/<DESIGN>/<VARIANT>/T_<ns>/    (gitignored)
#              results/hls/<DESIGN>/<VARIANT>/T_<ns>/  (tracked)
#            Read from scripts/hls/design.txt (tracked, so the label travels
#            with the git branch); the DESIGN environment variable overrides it.
# =============================================================================

proc env_or {name default} {
    if {[info exists ::env($name)] && $::env($name) ne ""} { return $::env($name) }
    return $default
}

# common.tcl -> scripts/hls -> scripts -> repo root
set ROOT [file dirname [file dirname [file dirname [file normalize [info script]]]]]

# First non-empty, non-comment line of design.txt
proc read_design {root} {
    set path [file join $root scripts hls design.txt]
    if {![file exists $path]} { error "Missing $path (one line: design label, e.g. baseline)" }
    set fh [open $path r]
    set txt [read $fh]
    close $fh
    foreach line [split $txt "\n"] {
        set line [string trim $line]
        if {$line ne "" && [string index $line 0] ne "#"} { return $line }
    }
    error "No design label in $path"
}

set DESIGN_FILE [read_design $ROOT]
set DESIGN [env_or DESIGN $DESIGN_FILE]
if {![regexp {^[A-Za-z0-9_-]+$} $DESIGN]} { error "Invalid DESIGN label: '$DESIGN'" }

# [OPT] a DESIGN left set in the shell silently redirects every output to another
#       design (it happened once: a b01 run landed in build/hls/baseline/) -> say it loudly
if {$DESIGN ne $DESIGN_FILE} {
    puts "WARNING: DESIGN overridden by the environment: '$DESIGN' (scripts/hls/design.txt: '$DESIGN_FILE')"
}
