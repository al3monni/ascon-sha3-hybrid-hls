# LOGBOOK — ascon-sha3-hybrid-hls

Storico delle modifiche al codice e alla configurazione, dalla base di partenza (`reference/original-hybrid-code/`) fino allo stato corrente dei componenti Vitis (`Hybrid-Test/`, `Sha-Test/`, `Ascon-Test/`).

**Convenzioni**
- Ogni voce ha un identificativo `LOG-NNN`, citato nel messaggio del commit corrispondente.
- Struttura di ogni voce: *Contesto* → *Modifiche* (file toccati) → *Verifica* → *Risultati / note*.
- Le voci **LOG-000…LOG-004** ricostruiscono il lavoro svolto prima dell'adozione del logbook, a partire dal diff tra `reference/original-hybrid-code/` e i componenti Vitis e dai report HLS versionati.
- Le attività pianificate sono in fondo, nella sezione [Backlog](#backlog).

---

## Indice

| ID | Data | Titolo | Varianti |
|---|---|---|---|
| [LOG-000](#log-000--base-di-partenza) | — | Base di partenza: core ibrido derivato da `tiny_sha3` | Hybrid |
| [LOG-001](#log-001--ristrutturazione-del-codice-per-hls) | ≤ 2026-06 | Ristrutturazione del codice per HLS | Hybrid, SHA-3, Ascon |
| [LOG-002](#log-002--varianti-standalone-sha-3-e-ascon) | ≤ 2026-06 | Varianti standalone SHA-3 e Ascon | SHA-3, Ascon |
| [LOG-003](#log-003--pass-di-ottimizzazione-pragma-only) | ≤ 2026-06 | Pass di ottimizzazione pragma-only | Hybrid, SHA-3, Ascon |
| [LOG-004](#log-004--flusso-vitis-hls-completo) | 2026-06-25/26 | Flusso Vitis HLS completo (csim → impl OOC) | Hybrid, SHA-3, Ascon |
| [LOG-005](#log-005--allineamento-repo--workspace-vitis) | 2026-10-09 | Allineamento repo ↔ workspace Vitis | — |
| [LOG-006](#log-006--import-del-materiale-da-tesi-work--esplorazione-pragma-nel-codice) | 2026-10-09 | Import del materiale da `tesi-work` + esplorazione pragma nel codice | Hybrid, SHA-3, Ascon |
| [LOG-007](#log-007--benchmark-software-portabile-x86_64--aarch64-fonte-unica-pmu-footprint) | 2026-10-09 | Benchmark software portabile (x86_64 / aarch64): fonte unica, PMU, footprint | Hybrid, SHA-3, Ascon |
| [LOG-008](#log-008--benchmark-misura-robusta-a-clocksource-lenta-e-smt) | 2026-10-09 | Benchmark: misura robusta a clocksource lenta e SMT | — |

---

## LOG-000 — Base di partenza

**Contesto.** Il punto di partenza è `reference/original-hybrid-code/`: un'implementazione software in C che deriva da `tiny_sha3` di Markku-Juhani O. Saarinen (Makefile datato 19-Nov-11) ed è stata estesa per calcolare **sia SHA-3 sia Ascon-Hash** con un'unica permutazione, selezionata dal parametro `mode`.

**Caratteristiche del codice originale**
- `hash_ctx_t` con `union { uint8_t b[200]; uint64_t q[25]; } st`: due viste sulla stessa memoria, per il byte-addressing in absorb/squeeze e il word-addressing nella permutazione.
- `keccakf_asconp12(st, mode)` è **un'unica funzione monolitica** con tutti i round:
  - `nrounds = 12` (Ascon) oppure `24` (SHA-3), `chirounds = 5` oppure `25`;
  - il layer χ è condiviso, mentre i passi specifici sono selezionati con `if (mode > 0)`: θ, ρ+π e ι per SHA-3; costanti, layer lineare d'ingresso/uscita e diffusione per Ascon.
- API in stile OpenSSL: `hash_init`, `hash_update`, `hash_final`, più la funzione one-shot `void *hash(...)`, che restituisce `md`.
- Testbench `main.c` con 4 KAT: SHA3-256 (msg corto), SHA3-512 (msg multiblocco da 255 B), Ascon-Hash256 (23 B) e Ascon-Hash256 (msg vuoto).

**Verifica.** `make && ./hashtest` → `All Self-Tests OK!` (ri-verificato il 2026-10-09).

---

## LOG-001 — Ristrutturazione del codice per HLS

**Contesto.** Il codice originale non è sintetizzabile come top-level (usa puntatori `void*` e non ha dimensioni limitate) e la permutazione monolitica non permette di applicare direttive mirate a ogni passo.

**Modifiche** (`Hybrid-Test/hash.c`, `hash.h`, `main.c`, rispetto a `reference/original-hybrid-code/`)
1. **Scomposizione della permutazione** in funzioni foglia: `theta()`, `rho_pi()`, `chi(st, chirounds)`, `iota(st, r)` (SHA-3) e `diffusion()` (Ascon). `keccakf_asconp12()` diventa un orchestratore che chiama i passi in base a `mode`. La logica è invariata.
2. **Label su tutti i loop e su tutte le call-site** (`theta_C`, `theta_D_loop`, `rho_pi_loop`, `top_chi`, `bc_cp_chi`, `chi_nlin`, `bc_cp_diff`, `rounds_loop`, `init_loop`, `absorbing_loop`, `output_loop`, `squeezing_1..4`, `init_perm`, `absorb_perm`, `final_perm_0..3`). Servono a leggere i report di sintesi e ad applicare direttive mirate.
3. **Squeezing Ascon esplicitato** in 4 fasi (`squeezing_1..4` intervallate da `final_perm_1..3`).
4. `void *hash(...)` → `void hash(...)`: il valore di ritorno non era usato e un top con puntatore di ritorno non è sintetizzabile.
5. **Nuovo wrapper sintetizzabile** `hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode)`: tipi concreti e dimensioni limitate, inoltra la chiamata a `hash()`. Prototipo aggiunto in `hash.h`.
6. `main.c` chiama `hash_top()` al posto di `hash()`.

**Verifica.** KAT invariati; `gcc -O2 hash.c main.c` → `All Self-Tests OK!`.

**Note.**
- In `keccakf_asconp12()` del componente Hybrid è rimasta una copia della tabella `keccakf_rndc[24]` che non viene usata: le costanti vengono lette in `iota()`. È codice morto, rimosso dalla sintesi, ma da eliminare (vedi Backlog).

---

## LOG-002 — Varianti standalone SHA-3 e Ascon

**Contesto.** Il confronto ibrido vs. dedicato richiede due implementazioni standalone con la stessa struttura di codice, così che le differenze misurate dipendano solo dalla condivisione del datapath.

**Modifiche**
- `Sha-Test/`: solo il ramo SHA-3 (θ, ρ+π, χ su 25 word, ι, 24 round). API senza parametro `mode`: `hash_top(in, inlen, md, mdlen)`.
- `Ascon-Test/`: solo il ramo Ascon (χ su 5 word, diffusione, 12 round, rate 8 B, IV `0x0000080100cc0002`). API senza `mode`.
- Stessa interfaccia `hash_top` e stessi KAT, filtrati per algoritmo.

**Verifica.** `gcc -O2 hash.c main.c` → `All Self-Tests OK!` per entrambe (ri-verificato il 2026-10-09).

**Risultati (benchmark software, gcc `-O2 -march=native`).** L'ibrido è sostanzialmente alla pari con le versioni dedicate: SHA3-256 Δ ≈ −0.9%, Ascon ≈ −0.1%, SHA3-512 ≈ +3.8% (l'unico caso con un overhead misurabile).

---

## LOG-003 — Pass di ottimizzazione pragma-only

**Contesto.** Prima ottimizzazione *senza toccare la logica*. L'obiettivo è **area-first** (metrica finale: throughput/area), con `II=1` solo dove la RAM di stato a 2 porte lo permette.

**Modifiche principali** (stesse scelte nelle tre varianti, dove applicabili)

| Dove | Direttiva | Razionale |
|---|---|---|
| `keccakf_asconp12` | `INLINE off` | Una sola istanza fisica della permutazione, condivisa da init/update/final |
| `hash`, `hash_init/update/final` | `INLINE` | Tutte le call-site della permutazione nello stesso scope del top |
| `theta`, `rho_pi`, `chi`, `iota`, `diffusion` | `INLINE` | Funzioni foglia: una sola copia nel datapath, nessun overhead di chiamata |
| `bc[5]`, `keccakf_rndc`, `keccakf_rotc`, `keccakf_piln`, `asconp_rndc` | `ARRAY_PARTITION complete` | Registri invece di ROM/RAM: porte libere |
| `theta_D_loop`, `bc_cp_chi`, `chi_nlin`, `bc_cp_diff`, `init_loop`, `output_loop`, `squeezing_*` | `PIPELINE II=1` | Al massimo 1–2 accessi allo stato per iterazione |
| `theta_C`, `rho_pi_loop` | `PIPELINE` (II libero) | Più di 2 accessi allo stato per iterazione: II=1 non è raggiungibile con la RAM a 2 porte |
| `rounds_loop`, `top_chi`, `absorbing_loop`, `output_loop` | `LOOP_TRIPCOUNT` | Bound per i report di latenza (round e lunghezze variabili a runtime) |

- Ogni pragma è commentata in linea con il razionale e, dove serve, con il motivo per cui un loop non arriva a II=1.
- `ARRAY_PARTITION` su `H.st` è **disabilitata di proposito**: la `union` impedisce di partizionare lo stato (vedi Backlog B-01).
- `ALLOCATION instances=1` **non è usata**: `INLINE off` basta a ottenere un'unica istanza (vedi LOG-005).

---

## LOG-004 — Flusso Vitis HLS completo

**Contesto.** Prima caratterizzazione hardware delle tre varianti in Vitis Unified 2025.1.

**Configurazione** (`*/hls_config.cfg`): part `xck26-sfvc784-2LV-c`, `flow_target=vivado`, `package.output.format=ip_catalog`, top `hash_top`, clock di default a 10 ns. Su Hybrid-Test c'è in più `vivado.flow=impl`.

**Flusso eseguito:** C Simulation → C Synthesis → C/RTL Co-simulation → Package → Implementation (out-of-context).

**Risultati post-route**

| Variante | LUT | FF | BRAM | Permutazione (LUT/FF) | Period post-route | Cosim latency min/avg/max |
|---|---:|---:|---:|---|---:|---|
| Hybrid | 3281 | 2215 | 10 | 2514 / 1743 | 6.169 ns | 2287 / 5853 / 14352 |
| SHA-3 | 2214 | 1551 | 10 | 1832 / 1203 | 6.023 ns | 3449 / 8849 / 14250 |
| Ascon | 1200 | 744 | 4 | 826 / 332 | 4.722 ns | 183 / 260 / 338 |

- Timing chiuso in tutte le varianti, con un margine ampio sul target di 10 ns. Nel caso Hybrid il percorso critico parte dalla BRAM dello stato e attraversa θ/ρπ/χ.
- Cosim: **Pass** su tutte le varianti.

**Limite noto.** Le `#pragma HLS INTERFACE` in `hash_top` sono **commentate**. L'IP ha quindi porte grezze: `in`/`md` sono `ap_memory`, `inlen`/`mdlen`/`mode` sono `ap_none`, il controllo è `ap_ctrl_hs`. L'IP non è collegabile allo Zynq PS in un block design finché non si aggiunge un wrapper AXI (Backlog B-03).

---

## LOG-005 — Allineamento repo ↔ workspace Vitis

**Contesto.** La repo GitHub (`Master-Thesis`, ora `ascon-sha3-hybrid-hls`) e il workspace Vitis locale erano disallineati. L'obiettivo è che la repo **coincida con il workspace**, così da versionare anche configurazioni e report.

**Modifiche**
- Repo rinominata in `ascon-sha3-hybrid-hls`. La root della repo è ora il workspace Vitis.
- `code/opt-pragma/{hybrid,sha3,ascon}` → `Hybrid-Test/`, `Sha-Test/`, `Ascon-Test/`: git ha riconosciuto i rename, quindi la storia è conservata.
- `code/original-hybrid-code/` → `reference/original-hybrid-code/`: rimossi i binari `hash.o`, `main.o`, `hashtest.exe`.
- `ottimizzazioni_logiche*.md` → `docs/optimizations/`; `Notes.txt`, `TODO.txt` → `docs/notes/`.
- Aggiunti `.gitignore` (esclude build, cache, `_ide/`, `*/*/hls/`, log, `reports/.db/`) e `.gitattributes` (testo normalizzato LF).
- README aggiornato sulla nuova struttura; creato questo logbook.

**Divergenze GitHub ↔ locale trovate e come sono state risolte**

| File | GitHub | Locale |
|---|---|---|
| `sha3/hash.c` | `#pragma HLS ALLOCATION instances=keccakf_asconp12 limit=1 function` **attiva** | assente |
| `hybrid/hash.c` | stessa pragma, **commentata**; refuso nei commenti `ascon_rndc[12]` | assente; commento `ascon_rndc[24]` |
| `ascon/*`, `main.c`, `hash.h` | identici a parte i fine riga (LF su GitHub, CRLF in locale) | — |

- **Verifica di quale versione è stata sintetizzata.** Ho confrontato le pragma attive nei sorgenti locali con quelle delle copie che Vitis salva a ogni co-simulazione (`*/hls/sim/wrapc/hash.c_pre.c.tb.c`). Coincidono in tutte e tre le varianti, e nessuna contiene `ALLOCATION`. La sintesi SHA-3 (2026-06-26 12:26) è successiva all'ultima modifica di `Sha-Test/hash.c` (12:18). Per Hybrid, le modifiche fatte dopo la sintesi del 25/06 toccano solo commenti e righe vuote.
- **Decisione:** fa fede la versione **locale**. I report versionati sono coerenti con i sorgenti.
- **Osservazione:** i report post-route mostrano **una sola istanza** `grp_keccakf_asconp12_1` in tutte le varianti. `INLINE off` basta quindi per condividere il datapath, e `ALLOCATION` è ridondante.

---

## LOG-006 — Import del materiale da `tesi-work` + esplorazione pragma nel codice

**Contesto.** Parte del lavoro era solo nella cartella locale `tesi-work`: il benchmark di latenza software, i report sulle pragma e sull'esplorazione delle prestazioni, e le versioni del codice usate per quell'esplorazione (`code/HLS-pragma/`). L'obiettivo è avere **un'unica copia di lavoro**, cioè questa repo, senza perdere informazioni.

**Modifiche**
- `tesi-work/code/statistics-test/` → `benchmarks/software-latency/` (senza gli eseguibili `bench`).
- Report → `docs/reports/`: `ASCON-SHA_PERFORMANCE.docx` (esperimenti E0…E12, NE0 con screenshot di sintesi), `HLS_Pragma_Report.docx`, `pragma-analysis.docx` e `report_pragma_HLS_hash.md` (versione Markdown dell'analisi).
- `presentazione_latenza.pptx` → `docs/presentations/`.
- `docs/notes/TODO.txt` aggiornato: rimosse le voci sul MYRTUS Security Manager, che è diventato un progetto separato.
- **Non importato:** il codice di `tesi-work/code/HLS-pragma/{hybrid,sha3,ascon}-test`, cioè le versioni dell'esplorazione superate da `*-Test/`. Le sue lezioni sono riportate nel codice attuale come commenti (vedi sotto). Non importati nemmeno `opt-pragma/` e `original-hybrid-code/`, identici a quanto già presente.
- **Commenti nel codice** (`*-Test/hash.c`, **solo commenti**: verificato che, tolti i commenti, il codice è identico; KAT OK su tutte le varianti):
  - `[EXPLORED]`: pragma provate e scartate, con il motivo e i numeri (stime HLS) dagli esperimenti;
  - `[FIX comment]`: corretta la frase imprecisa secondo cui `ARRAY_PARTITION` srotola da sola i loop. In realtà rimuove soltanto il limite delle 2 porte; l'unroll lo fanno `UNROLL` o `PIPELINE` sul loop padre;
  - Ascon: chiarito che, a differenza di Keccak, la `PIPELINE` su `rounds_loop` è mantenuta (round su 5 word, NE0). Corretto il commento `nrounds=12 for sha3` → `for ascon`.

**Lezioni dall'esplorazione delle pragma** (stime di sintesi HLS, clock 10 ns)

| Esperimento | Pragma | Effetto | Esito |
|---|---|---|---|
| Ascon E1–E2 | copia locale partizionata dello stato (`stl[25]`) + copie srotolate | permutazione in 7 cicli, **28.1k LUT** (vs 4.7k) | scartata: B-01 è la via a basso costo d'area |
| Ascon E1 | `PIPELINE` su `absorbing_loop` | dipendenza di memoria su `st`, II=12 non rispettato | rimossa |
| Ascon E9 | rimozione di `INLINE off` su `keccakf_asconp12` | una permutazione per call-site: 4.7k → **9.5k LUT** | `INLINE off` mantenuta |
| Ascon E10–E12 | `UNROLL` su `squeezing_1..4` | `hash_final` diventa un modulo con una seconda permutazione: 4.7k → 6.6k LUT | mantenuta `PIPELINE II=1` |
| SHA-3 E2 | `PIPELINE` su `top_chi` vs solo sui loop interni | 5.0k LUT / BRAM 6 / 2641 cicli vs **4.2k LUT** / BRAM 18 / 3865 cicli | solo loop interni (area-first) |
| SHA-3 E5 + `rounds_loop` | `PIPELINE II=1` su `rounds_loop` | permutazione 2641 → **53 cicli**, 3.1k → 8.9k LUT (≈17× throughput/area) | scartata per il target area-first; **da rivalutare** (B-08) |

> I valori sono **stime di C Synthesis** delle versioni di esplorazione e non sono direttamente confrontabili con i valori post-route di LOG-004. Il trend però è indicativo.

---

## LOG-007 — Benchmark software portabile (x86_64 / aarch64): fonte unica, PMU, footprint

**Contesto.** Il benchmark di latenza va eseguito anche su ARM (Kria KV260) e rifatto su WSL e su Ubuntu nativo, con metriche aggiuntive, senza disturbare l'esecuzione degli algoritmi e con una definizione esplicita di cosa si misura.

**Verifica preliminare (codice benchmark vs Vitis).** Ho confrontato i sorgenti token per token, ignorando commenti, pragma, label e stile delle graffe. Il risultato è **funzionalmente equivalente**, con due differenze:
- Hybrid: Vitis contiene una tabella `keccakf_rndc` non usata in `keccakf_asconp12()` (B-06);
- SHA-3/Ascon: il benchmark usava le variabili locali `nrounds` e `chirounds` e passava `chirounds` come parametro a `chi()`, mentre Vitis usa costanti letterali. Questo può cambiare il codice generato da gcc.

**Modifiche** (`benchmarks/software-latency/`)
- **Fonte unica:** eliminate le copie `*/hash.c`, `*/hash.h`. `run_all.sh` compila `../../{Hybrid,Sha,Ascon}-Test/hash.c` in `build/<impl>/`.
- **Regione cronometrata** (documentata nel report e nel codice): una chiamata a `hash(msg, len, md, mdlen[, mode])`, cioè `hash_init` + `hash_update` + `hash_final`, chiamata diretta (macro `BENCH_MEASURE`, nessun puntatore a funzione).
- **Due fasi per caso:**
  - **A, per chiamata:** distribuzione (ns + tick). Nel loop ci sono solo letture di timestamp;
  - **B, batch:** chiamate consecutive, contatori PMU (`perf_event_open`: cicli core reali e istruzioni, solo user-space) letti solo prima e dopo il batch. Danno ns/hash senza overhead del timer, cicli/hash, IPC e **cicli/byte**.
- **Tick portabili:** `rdtsc` su x86_64 (chiamato esplicitamente TSC, non cicli reali), `cntvct_el0` + `cntfrq_el0` su aarch64. In precedenza su ARM i cicli valevano 0.
- **Calibrazione:** l'overhead del timer viene misurato e riportato, non sottratto.
- **Footprint statico:** `sizeof(hash_ctx_t)`, stima dello stack peggiore (`-fstack-usage`), `.text`/`.rodata` di `hash.o`, confronto ibrido vs (SHA-3 + Ascon).
- **Piattaforma:**
  - rilevata automaticamente (`<arch>-<native|wsl>`), flag `-march=native` / `-mcpu=native`;
  - processo fissato sull'ultimo core (`taskset`);
  - intestazione del report con CPU, kernel, governor, compilatore e stato della PMU.
- **Output:** `results/<piattaforma>/result.txt` (stesso formato a tabelle, più la tabella PMU e il footprint) e `data.tsv` (dati grezzi per i grafici). Rimosso il vecchio `result.txt` misurato in WSL: va rifatto.

**Verifica**
- x86_64 (container cloud): build, KAT, report e `data.tsv` OK. La PMU non è disponibile in quell'ambiente (fallback "n/a" verificato).
- aarch64: cross-compilazione con `aarch64-linux-gnu-gcc` ed esecuzione sotto `qemu-aarch64`. KAT OK, percorso `cntvct_el0`/`cntfrq_el0` OK.
- I valori misurati nel container **non sono rappresentativi**: VM condivisa, variazioni fino al 60% fra run identici. Le misure valide sono quelle sulle macchine reali (WSL, Ubuntu nativo, Kria).

**Da fare.** Eseguire `./run_all.sh` su WSL, Ubuntu nativo (portatile) e Kria, poi committare `results/*`.

---

## LOG-008 — Benchmark: misura robusta a clocksource lenta e SMT

**Contesto.** I primi run completi (LOG-007):
- **x86_64-wsl** (Ryzen 5 5600G) pulito e coerente: ibrido vs standalone in cicli core, SHA-3 da +0.7 a +3.1%, Ascon da −0.6 a −0.1%. cpb a 4096 B: SHA3-256 ≈ 48, SHA3-512 ≈ 87, Ascon ≈ 147.
- **x86_64-native** (portatile, Ryzen 5 3500U) **non affidabile**:
  - overhead del timer di 1467 ns per misura, perché il kernel ha scartato il TSC ("TSC found unstable after boot") e usa **HPET**: `clock_gettime` costa circa 700 ns;
  - cicli PMU incoerenti nello stesso run (+26% e −36% su singoli casi) per **SMT attivo**: la CPU 7 condivide il core fisico con la CPU 6;
  - governor `schedutil`.

**Modifiche** (`bench_common.h`, `run_all.sh`)
- **Fase A** con sole letture dei tick (`rdtsc` / `cntvct_el0`), nessuna `clock_gettime` nel loop; ns = tick / f_tick, con la frequenza calibrata una volta all'avvio. Il misurato non dipende più dalla clocksource del kernel. Su un singolo core fissato il TSC è affidabile anche quando il kernel lo scarta come clocksource globale.
- **Fase B** divisa in 10 sotto-batch: si riporta la **mediana** e la **dispersione** `disp%` = (max − min) / mediana, che serve da indicatore di qualità.
- Testata del report: clocksource e costo di `clock_gettime`, stato SMT e CPU sorelle. Nuova sezione **"Avvisi sulla qualità della misura"**: clocksource lenta, SMT con fratello attivo, governor diverso da `performance`, PMU multiplexata, `disp%` > 5%.
- `data.tsv`: aggiunte le colonne `spread_pct` e `pmu_scaled`.

**Verifica.** Container x86: quick run OK e avvisi corretti (il container è rumoroso, quindi scatta l'avviso `disp%`). aarch64 sotto qemu: KAT OK, formato corretto.

**Da fare.** Rifare i run con la nuova metodologia su **tutte e tre** le piattaforme, così sono confrontabili. Sul portatile: SMT disattivato e governor `performance` durante la misura.

---

## Backlog

Attività pianificate, in ordine di priorità. Quando un'attività parte, riceve un `LOG-NNN`.

| ID | Attività | Varianti | Riferimento | Stato |
|---|---|---|---|---|
| B-01 | **Rimuovere la `union` su `st`**: stato solo `uint64_t q[25]` (Ascon: `q[5]`), viste a byte ricostruite con shift/mask. Sblocca `ARRAY_PARTITION` sullo stato e II=1 nei loop limitati dalle porte | tutte | `docs/optimizations/*` §1 | da fare |
| B-02 | Rompere la dipendenza seriale in `rho_pi()` | Hybrid, SHA-3 | `ottimizzazioni_logiche_{hybrid,sha3}.md` §2 | da fare |
| B-03 | **Wrapper AXI4-Stream** (`hls::stream` + `ap_axiu`, TLAST sull'ultimo beat) per l'integrazione con AXI DMA. Da decidere: controllo via AXI-Lite oppure header in-band + `ap_ctrl_none` (32 bit, compatibile con il driver `uniss_dma` di [mdc-suite/AXI-based-driver-applications](https://github.com/mdc-suite/AXI-based-driver-applications)). Bozza AXI-Lite a 8 bit già verificata in C (KAT + TLAST) | Hybrid → poi tutte | `docs/optimizations/*` §3/§4 | in corso |
| B-04 | Block design Vivado: Zynq MPSoC + AXI DMA + IP, bitstream/`.xsa`, applicazione di test sulla KV260 | Hybrid → poi tutte | — | da fare |
| B-05 | Dataset di tesi: area stimata vs reale, II di sintesi vs II osservato in cosim, throughput/area per algoritmo | tutte | — | da fare |
| B-06 | Pulizia: rimuovere `keccakf_rndc` non usato in `keccakf_asconp12()` (Hybrid) e le variabili inutilizzate (`i`, `j`) | Hybrid | LOG-001 | da fare |
| B-07 | Ottimizzazioni Ascon minori: azzerare solo 5 word in `hash_init`, absorb a granularità di word | Ascon | `ottimizzazioni_logiche_ascon.md` §2–3 | da fare |
| B-08 | Rivalutare `PIPELINE II=1` su `rounds_loop`: in SHA-3 E5 ≈17× throughput/area a ≈2.8× LUT. Ha senso se la metrica è throughput/area e non area pura; da misurare dopo B-01 | SHA-3, Hybrid | LOG-006 | da fare |
| — | Dopo ogni modifica logica che cambia l'IP: incrementare `ip.version` nel package | tutte | — | regola |
