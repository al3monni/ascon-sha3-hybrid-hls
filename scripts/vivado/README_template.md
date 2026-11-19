# template.tcl — Vivado batch flow for a stream IP on a Zynq UltraScale+ board

`template.tcl` builds a complete PS + PL system around one user IP with an AXI4-Stream
input and an AXI4-Stream output, from project creation to the files needed on the board.
It rebuilds, as a parametric script, the system of the reference designs in
[mdc-suite/AXI-based-driver-applications](https://github.com/mdc-suite/AXI-based-driver-applications)
(`kv260/applications/aes/vivado/design_1*.tcl`, `kv260/applications/vpa/vivado/hlsPlatformDesign.tcl`),
for use with the `uniss_dma` Linux driver of the same repository.

## Usage

1. Edit section 0 of `template.tcl` (IP repository and VLNV, stream width, clock, addresses, names).
2. Run `vivado -mode batch -nojournal -source template.tcl`.
3. Collect the results from `OUT_DIR`: reports, `.bit`, `.bit.bin`, `.xsa`, `pl.dtsi`.

## Status

Written for Vivado 2025.1.1, with no version check. It derives from a flow that was run
with Vivado 2025.1.1 and tested on a KV260 with `uniss_dma`. The template itself has only
been dry-run with stubbed Vivado commands, not yet in Vivado.

## Main differences from the reference scripts

| Topic | Reference scripts (AES, VPA) | `template.tcl` | Reason |
|---|---|---|---|
| Kind of script | Tool-generated exports (`write_bd_tcl`, `write_project_tcl`) of one specific design | Hand-written, parametric: all settings in section 0 | An export recreates one design only (cell names, IP, paths, addresses); reusing it for another IP means editing it by hand |
| Vivado and IP versions | Fixed to the release that wrote the export (AES 2021.2 stops on any other version) | No version check; catalog IPs taken in the latest available version | Usable across releases; version-sensitive points are tagged `[VERSION]` in the comments |
| PS configuration | Board preset written out in full (about 800 properties) | Board preset loaded from the board files, plus the few design settings | Shorter, and not tied to one board or one PS IP version; board files are required |
| Control path | `M_AXI_HPM0` + `M_AXI_HPM1` → AXI Interconnect | `M_AXI_HPM0` only → SmartConnect | One master port reaches all registers; SmartConnect needs one clock/reset and converts AXI4 to AXI4-Lite by itself |
| User IP registers | Not present | Optional AXI4-Lite slave (`USE_AXILITE`) | Covers IPs with and without control registers |
| AXI DMA | AES: 3 (2 MM2S + 1 S2MM); VPA: 2 (1 MM2S + 1 S2MM) | 2 (1 MM2S + 1 S2MM), as VPA | One input stream and one output stream |
| Stream width | 32 bit | `STREAM_WIDTH` (default 64) | Must match the TDATA width of the user IP |
| DMA length register | VPA: 14 bit | 26 bit | 14 bit limits a transfer to 16 KB; with 26 bit the limit is the one of the driver |
| PL → PS interrupt | Enabled, not connected | Disabled | Not used: the driver polls the DMA status registers |
| Build steps | Synthesis, implementation and bitstream launched from the GUI | Whole flow in batch | Repeatable builds, no manual steps |
| `.bit.bin` for `fpgautil` | Provided ready-made | Generated with `bootgen` | Always matches the bitstream just built |
| Device tree overlay | Written by hand | Generated, in the format of `pl_vpa.dtsi` | Addresses and stream width come from the same settings as the block design, so they cannot diverge |
| Reports, `.xsa`, block design export | Not produced | Produced | Timing/area data for every build; `.xsa` and the export are optional extras |

## Unchanged

- AXI DMA in direct register mode (no scatter-gather), one DMA per direction.
- Driver channel order: channel 0 = MM2S (DDR → IP), channel 1 = S2MM (IP → DDR).
- DMA register addresses of the VPA design (`0xA000_0000`, `0xA001_0000`).
- DMA data path through SmartConnect to `S_AXI_HPC0_FPD`, as in the AES design.
- One clock (`pl_clk0`) and one reset domain (`proc_sys_reset`) for the whole PL.

## Limits

- The device tree overlay covers this architecture only (two DMAs, one per direction).
- The user IP must have exactly one AXI4-Stream slave and one AXI4-Stream master, and
  must drive TLAST on the last output beat.
- The user IP registers are not described in the overlay: an application reaches them at
  `ADDR_IP` by mapping `/dev/mem`.
