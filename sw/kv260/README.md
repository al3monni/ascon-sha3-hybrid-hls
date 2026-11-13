# Test su scheda KV260 (B-04)

Procedura per caricare uno dei 6 bitstream e verificarne i digest sulla Kria KV260 con Ubuntu 22.04 e il driver `uniss_dma` dei relatori ([mdc-suite/AXI-based-driver-applications](https://github.com/mdc-suite/AXI-based-driver-applications), `kv260/ubuntu-driver`). I passi sono gli stessi delle loro istruzioni (*AXI DMA Driver Test Instructions*); cambiano solo bitstream, overlay e app di test.

## Cartella da copiare sulla scheda

Sul PC la cartella è `build/deploy/kv260_b04/` (non versionata). Ha questa struttura, con `<cfg>` ∈ {`hybrid_lite`, `hybrid_ib`, `sha3_lite`, `sha3_ib`, `ascon_lite`, `ascon_ib`} e `<V>` = `Hybrid-Test` / `Sha-Test` / `Ascon-Test`:

| File | Origine nel repo |
|---|---|
| `<cfg>/kv260_<cfg>.bit.bin` | `results/vivado/b04/<V>[-ib]/hw/` |
| `<cfg>/pl.dtsi` | `results/vivado/b04/<V>[-ib]/` |
| `hash_dma_test.c`, `load_cfg.sh`, `test_all.sh`, `build_driver.sh` | `sw/kv260/` |
| `golden.txt` | `tests/equivalence/` |
| `axidma-driver/axidma.c` | driver dei relatori (`kv260/ubuntu-driver`) |

Dal PC (PowerShell): `scp -r <repo>\build\deploy\kv260_b04 ubuntu@<ip-scheda>:~/`

## Comandi sulla scheda

```bash
cd ~/kv260_b04
# 0. una volta per boot: scaricare l'app di default (libera il PL e rimuove il suo overlay)
sudo xmutil unloadapp
# 1. una volta: permessi persi con scp da Windows, driver (servono gli header del kernel) e app
chmod +x *.sh
./build_driver.sh
gcc -O2 -Wall -o hash_dma_test hash_dma_test.c
# 2. una configurazione: pulizia della precedente, bitstream, overlay (configfs "b04"), driver
./load_cfg.sh hybrid_lite      # atteso: overlay "applied", MM2S = Channel 0, S2MM = Channel 1
sudo ./hash_dma_test hybrid lite golden.txt      # varianti: hybrid | sha3 | ascon, versioni: lite | ib
# 3. oppure: tutte e 6 in sequenza, log in results_b04.txt e results_b04_summary.txt
./test_all.sh
```

Esito atteso: `PASS: 771 / 771 digests correct` per l'Hybrid, 514 per SHA-3 e 257 per Ascon.

Il cambio di configurazione non richiede il riavvio. `load_cfg.sh` rimuove prima il driver (`rmmod`, che libera `/dev/uniss_dma`), poi l'overlay `b04`, e infine riprogramma il PL. Nel `dmesg` compaiono i warning `OF: overlay: WARNING: memory leak will occur if overlay removed`: sono attesi con gli overlay che hanno `__symbols__`.

## Differenze tra le due versioni

| | `lite` (AXI4-Lite, interfaccia b03) | `ib` (in-band, senza AXI4-Lite) |
|---|---|---|
| Parametri | registri `inlen` `0x18`, `mdlen` `0x20`, `mode` `0x28` di `hash_0` a `0xA002_0000` (via `/dev/mem`) | primo beat da 8 byte del buffer MM2S: `[31:0]` inlen, `[47:32]` mdlen, `[55:48]` mode |
| Avvio | `ap_start` (registro `0x00`) | nessuno: l'IP è free-running (`ap_ctrl_none`) |
| Esito | `ap_return` (`0x10`): 0 = OK, 1 = TLAST ≠ inlen | 8 byte dopo il digest nel buffer S2MM, stesso significato |
| Trasferimento MM2S | `inlen` byte; nessun trasferimento se `inlen = 0` | `8 + inlen` byte |
| Trasferimento S2MM | digest | digest + 8 byte di stato |

In entrambe le versioni il canale 0 del driver è l'MM2S (`axi_dma_0`, `0xA000_0000`) e il canale 1 è l'S2MM (`axi_dma_1`, `0xA001_0000`). Gli stream sono a 64 bit.

## Note

- **Reset dei DMA tra un messaggio e l'altro.** Il driver avvia un trasferimento solo su un canale fermo (*halted*). Per questo l'app chiama `IOCTL_DMA_RESET_ALL` prima di ogni messaggio. Il reset dei DMA non tocca l'IP.
- **Tempo per messaggio.** Il tempo stampato dall'app comprende `ioctl`, copie da 64 KB del driver e polling: è un tempo di sistema, non il throughput del core. I cicli del core restano quelli della cosim, sulla versione `lite`.
- **Clock.** `pl_clk0` = 100 MHz. Il dtsi, come quello dei relatori, non imposta il clock e si affida al valore del boot firmware della Kria. Sulla scheda il valore è confermato: `sudo cat /sys/kernel/debug/clk/pl0_ref/clk_rate` dà 99999999, e il clock resta attivo anche dopo `xmutil unloadapp`. Se si osservasse un clock diverso, va aggiunto al dtsi il nodo `clocking0` (`assigned-clock-rates = <99999001>`).
- **Verifica eseguita (LOG-015).**
  - **Sul PC:** l'app è verificata contro il modello C dell'IP con un driver simulato.
  - **Sulla KV260** (Ubuntu 22.04.5, kernel `5.15.0-1080-xilinx-zynqmp`, 2026-11-13): tutte e 6 le configurazioni danno digest corretti (771 / 514 / 257). Il log è in `results/board/b04/`.
