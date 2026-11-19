# Porting del flusso Vivado dei relatori a Vivado 2025.1.1

Confronto tra gli script Vivado per la KV260 del repository [mdc-suite/AXI-based-driver-applications](https://github.com/mdc-suite/AXI-based-driver-applications) (commit `da60275`) e lo script del progetto, `scripts/vivado/build_bd.tcl` (B-04, LOG-015).

> **Stato del documento:** 13/11/2026. Le righe citate si riferiscono ai file del commit indicato. Il confronto passo per passo è anche nell'intestazione di `build_bd.tcl`.

---

## 1. Script di partenza

| File (`kv260/applications/…`) | Tipo | Vivado | Design |
|---|---|---|---|
| [`aes/vivado/design_1.tcl`](https://github.com/mdc-suite/AXI-based-driver-applications/blob/main/kv260/applications/aes/vivado/design_1.tcl) | dump `write_bd_tcl`: progetto + block design | 2021.2 | AES-256 (IP RTL `aes256_accelerator_1t`) |
| [`aes/vivado/design_1_fromVivado2023.tcl`](https://github.com/mdc-suite/AXI-based-driver-applications/blob/main/kv260/applications/aes/vivado/design_1_fromVivado2023.tcl) | stesso dump, rigenerato | 2023 | AES-256 |
| [`vpa/vivado/hlsPlatformDesign.tcl`](https://github.com/mdc-suite/AXI-based-driver-applications/blob/main/kv260/applications/vpa/vivado/hlsPlatformDesign.tcl) | dump `write_project_tcl`: intero progetto `line_detector` | 2021.2 | VPA (IP HLS `VPA_IP`) |

Sono script **generati** dal tool, non scritti per essere riusati. Ricreano il progetto e il block design di un design specifico. Sintesi, implementazione e bitstream restano invece da lanciare dalla GUI: lo dice il punto 5 del README di `vpa/vivado`.

## 2. Perché non si possono rieseguire così come sono

- **`design_1.tcl`**
  - Controllo bloccante sulla versione: `scripts_vivado_version 2021.2`, righe 23–28. Con un'altra versione di Vivado lo script si ferma con un errore.
  - `zynq_ultra_ps_e:3.3`, mentre il catalogo di Vivado 2025.1.1 contiene la 3.5 (`results/vivado/b04/*/hash_bd.tcl`).
- **`design_1_fromVivado2023.tcl`**
  - Non ha il controllo di versione e usa già il PS 3.5. È il più vicino al nostro caso.
  - Resta però legato all'IP AES, che ha due stream di ingresso e quindi tre DMA, al suo `ip_repo` (`./aes256_ip`) e alla sua mappa degli indirizzi.
- **`hlsPlatformDesign.tcl`**
  - Percorsi assoluti della macchina di origine (`/home/marco/…`, righe 26–30).
  - Importa un checkpoint di sintesi incrementale (`line_detector_bd_wrapper.dcp`, righe 46 e 1362).
  - `ip_repo` relativo a un progetto Vitis HLS 2021.2 (`../vitis_hls/prj_VPA_IP/solution1/impl/ip`, riga 232).
  - Board part `kv260_som:part0:1.2` (riga 156) e PS 3.3.
- **In tutti e tre**
  - Nomi delle celle, IP utente, numero di DMA e indirizzi sono quelli di AES o di VPA.
  - Nessuno script produce `.bit.bin`, `.xsa`, report o device tree.

Per questo `build_bd.tcl` non adatta un dump. Ricostruisce **lo stesso sistema** da zero, per Vivado 2025.1.1 e per le 6 IP del progetto (3 varianti × `lite` / `ib`).

## 3. Architettura del sistema

| Elemento | AES (2023) | VPA (2021.2) | Nostro (`build_bd.tcl`) |
|---|---|---|---|
| PS (`zynq_ultra_ps_e`) | 3.5, preset KV260 nel dump (793 proprietà) | 3.3, preset nel dump | versione più recente del catalogo (3.5); preset della board `kv260_som:part0:1.4` se i board file sono installati, altrimenti lo stesso dizionario di AES (`ps_kv260_preset.tcl`) |
| `pl_clk0` | 100 MHz | 230 MHz | `PL_MHZ`, default 100 MHz |
| Controllo | HPM0 + HPM1 (128 bit) → `axi_interconnect` → `S_AXI_LITE` dei DMA | HPM0 + HPM1 (32 bit) → `axi_interconnect` | solo HPM0 (128 bit) → SmartConnect → `S_AXI_LITE` dei DMA (+ `s_axi_control` dell'IP, solo `lite`) |
| Dati | DMA → SmartConnect → `S_AXI_HPC0` (128 bit) | DMA → SmartConnect → `S_AXI_HP0` (32 bit) | DMA → SmartConnect → `S_AXI_HPC0` (128 bit), come AES |
| AXI DMA | 3: 2 MM2S + 1 S2MM, senza SG | 2: 1 MM2S + 1 S2MM, senza SG, burst MM2S 16, `c_sg_length_width` 14 | 2: 1 MM2S + 1 S2MM, senza SG, burst 16, `c_sg_length_width` 26 |
| Larghezza stream | 32 bit (default) | 32 bit (default) | **64 bit**: un beat = una word di stato |
| Registri DMA | `0xA000_0000`, `0xA010_0000`, `0xA030_0000` | `0xA000_0000`, `0xA001_0000` | come VPA: `0xA000_0000` (canale 0, MM2S), `0xA001_0000` (canale 1, S2MM); IP `lite` a `0xA002_0000` |
| Interrupt PL→PS | `IRQ0` abilitato, non collegato | `IRQ0` abilitato, non collegato | disabilitato (il driver fa polling) |
| Clock e reset | un dominio, `proc_sys_reset` | un dominio, `proc_sys_reset` | un dominio, `proc_sys_reset` |

## 4. Flusso

| Passo | Relatori | Nostro |
|---|---|---|
| Avvio | `source` dello script nel Tcl di Vivado (`vivado -mode tcl`) | `vivado -mode batch -source build_bd.tcl`, con parametri da variabili d'ambiente (`VARIANT`, `CTRL`, `PL_MHZ`, …) |
| Progetto e IP | `create_project`, `ip_repo_paths` fisso | `create_project -force`, `ip_repo_paths` = export HLS del punto (`build/hls/<design>/<variante>/T_10/hls/impl/ip`), IP cercata per VLNV |
| Block design | ricreato dal dump (celle, proprietà, posizioni) | creato con comandi espliciti: celle → connessioni → indirizzi → `validate_bd_design` |
| Copia del BD | — | `write_bd_tcl` → `results/vivado/<design>/<variante>/hash_bd.tcl` |
| Sintesi | dalla GUI; VPA: `SYNTH_CHECKPOINT_MODE None`, strategie di default | batch, `synth_checkpoint_mode None`, strategie di default |
| Implementazione e bitstream | dalla GUI | batch (`launch_runs impl_1 -to_step write_bitstream`) |
| Bitstream per `fpgautil` | `.bin` fornito già pronto (non generato dagli script) | `bootgen -arch zynqmp -process_bitstream bin` → `.bit.bin`, nello stesso formato del loro `.bin` |
| Piattaforma | — | `write_hw_platform -fixed -include_bit` → `.xsa` |
| Report | — | timing, utilization (totale e per istanza), `summary.txt` |
| Device tree overlay | `pl.dtsi` scritto a mano | `pl.dtsi` generato dalla mappa degli indirizzi, nel formato di `pl_vpa.dtsi` |

## 5. Device tree

Il `pl.dtsi` generato ha la stessa struttura di `vpa/sw/pl_vpa.dtsi`: un nodo `xlnx,axi-dma` per DMA con un canale, più il nodo `uniss,dmas` con `dev-handles`. Cambiano solo due cose:
- `xlnx,datawidth = <0x40>` (64 bit) invece di `<0x20>`;
- gli indirizzi, presi dal block design.

Come nei loro overlay, `pl_clk0` non viene impostato. Sulla scheda il clock resta a 100 MHz anche dopo `xmutil unloadapp` (LOG-015).

> **Nota sul materiale dei relatori.** Per AES, `ubuntu-driver/axidma-driver/pl.dtsi` riporta i DMA a `0xa001_0000` e `0xa003_0000`. Il block design AES e `aes/sw/pl_aes.dtsi` li mettono invece a `0xA010_0000` e `0xA030_0000`. Il nostro overlay non ne risente, perché viene generato dal block design.

## 6. Esito

Lo script ricostruisce in batch il sistema dei relatori, con le differenze indicate al §3. Le principali sono gli stream a 64 bit, un solo master di controllo e nessun interrupt. Rispetto ai loro script, in più produce bitstream, `.bit.bin`, `.xsa`, report e overlay.

Le 6 configurazioni b04 chiudono il timing a 100 MHz e danno digest corretti sulla KV260 con il driver `uniss_dma` dei relatori (LOG-015, `results/vivado/b04/`, `results/board/b04/`).
