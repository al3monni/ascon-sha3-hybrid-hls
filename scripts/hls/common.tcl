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

# -----------------------------------------------------------------------------
# [OPT] B-04: control version of the IP and its identity (shared by the HLS scripts and by
#       scripts/vivado/build_bd.tcl, which sources this file: one place for paths and VLNVs)
#
#   CTRL  lite  AXI4-Lite control, ap_ctrl_hs (b03 interface)        build/hls/<D>/<VARIANT>/
#         ib    in-band: -DHASH_INBAND, ap_ctrl_none, header beat     build/hls/<D>/<VARIANT>-ib/
#               + status beat (see <VARIANT>/hash.h)
#   VLNV  unica.it:hls:hash_<hybrid|sha3|ascon>_<lite|ib>:1.0
# -----------------------------------------------------------------------------

set CTRL [env_or CTRL lite]
if {$CTRL ni {lite ib}} { error "CTRL must be lite or ib: '$CTRL'" }
set CTRL_CFLAGS [expr {$CTRL eq "ib" ? "-DHASH_INBAND" : ""}]

set IP_VENDOR  unica.it
set IP_LIBRARY hls
set IP_VERSION 1.0

# Hybrid-Test -> hybrid, Sha-Test -> sha3, Ascon-Test -> ascon
proc variant_short {variant} {
    set m [dict create Hybrid-Test hybrid Sha-Test sha3 Ascon-Test ascon]
    if {![dict exists $m $variant]} { error "Unknown VARIANT: '$variant'" }
    return [dict get $m $variant]
}

# Build / results folder of a variant: the in-band version gets the -ib suffix
proc variant_dir {variant ctrl} {
    variant_short $variant                ;# validates the name
    return [expr {$ctrl eq "ib" ? "$variant-ib" : $variant}]
}

proc ip_name {variant ctrl} {
    return "hash_[variant_short $variant]_$ctrl"
}

proc ip_display_name {variant ctrl} {
    set n [dict get [dict create Hybrid-Test "Hybrid SHA-3/Ascon" Sha-Test "SHA-3" Ascon-Test "Ascon-Hash256"] $variant]
    return [expr {$ctrl eq "ib" ? "$n hash (AXIS, in-band control)" : "$n hash (AXIS + AXI-Lite)"}]
}

proc ip_vlnv {variant ctrl} {
    return "$::IP_VENDOR:$::IP_LIBRARY:[ip_name $variant $ctrl]:$::IP_VERSION"
}
