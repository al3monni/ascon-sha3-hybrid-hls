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
| [LOG-009](#log-009--benchmark-10000-iterazioni-e-isolamento-del-dispositivo-livello-1) | 2026-10-10 | Benchmark: 10 000 iterazioni e isolamento del dispositivo (livello 1) | — |
| [LOG-010](#log-010--flusso-hls-da-tcl-e-sweep-del-clock) | 2026-10-30 / 11-05 | Flusso HLS da Tcl e sweep del clock (10 / 6.67 / 5 / 4 ns) | Hybrid, SHA-3, Ascon |
| [LOG-011](#log-011--testbench-in-modalità-throughput) | 2026-11-05 | Testbench in modalità throughput (`-argv <algo>`) | Hybrid, SHA-3, Ascon |
| [LOG-012](#log-012--ristrutturazione-repo-versioni-di-design-e-flusso-di-riferimento) | 2026-11-05 | Ristrutturazione repo: versioni di design e flusso di riferimento | — |
| [LOG-013](#log-013--b-01-rimozione-della-union-sullo-stato) | 2026-11-05 | B-01: rimozione della `union` sullo stato (branch `b01-remove-union`) | Hybrid (SHA-3, Ascon: da fare) |

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

## LOG-009 — Benchmark: 10 000 iterazioni e isolamento del dispositivo (livello 1)

**Contesto.** Con la metodologia di LOG-008:
- **x86_64-wsl** è pulito: dispersione < 2.3% (un solo caso al 33%, con mediana comunque coerente). Ibrido vs standalone in cicli: SHA3-256 da +0.2 a +1.7%, SHA3-512 da −0.6 a +2.1%, Ascon da −0.9 a −0.1%.
- **x86_64-native** (portatile), pur con SMT spento e governor `performance`, ha 6 casi con dispersione fra il 10 e il 55%. Sono consecutivi nel tempo: è un disturbo durato minuti (probabile throttling termico o processi in background). Ascon, misurato per ultimo, è perfetto. L'indicatore `disp%` li ha segnalati correttamente.

**Decisioni (su proposta di A. Monni)**
1. **10 000 iterazioni** per fase invece di 100 000: statisticamente sufficienti (sotto-batch da 1 000 chiamate) e run 10 volte più corti, quindi meno esposti a riscaldamento e attività di sistema.
2. **Isolamento del dispositivo**, livello 1 (runtime, senza riavvio): sul core di misura gira solo il benchmark e il resto del sistema è a riposo.

**Modifiche**
- `run_all.sh`:
  - 10 000 iterazioni (quick: 1 000);
  - core di misura passato da `isolate.sh` (`BENCH_CORE`);
  - riga "Isolamento" nella testata;
  - avviso "dispositivo NON isolato" su Linux nativo;
  - `NCPU` letto con `getconf` (le CPU online, non quelle concesse al processo).
- Nuovo `isolate.sh` (`on` | `off` | `status` | `run`):
  - SMT spento e governor `performance`;
  - core di misura riservato: tutti gli altri processi spostati sugli altri core (`systemctl set-property --runtime … AllowedCPUs=` su `user.slice`, `system.slice`, `init.scope`);
  - interrupt spostati;
  - servizi automatici fermati;
  - benchmark eseguito come utente normale in `bench.slice`;
  - ripristino completo anche con Ctrl+C.
- Non applicabile in WSL (scheduler di Windows): lì l'unica misura possibile è chiudere le app, ed è dichiarato come limite.

**Verifica.** Sintassi e logica di selezione delle CPU verificate nel container ("0-1" → core 1, altri "0"). `isolate.sh on/off` richiede systemd come PID 1, quindi la prima esecuzione reale avviene sul portatile e sulla Kria.

---

## LOG-010 — Flusso HLS da Tcl e sweep del clock

**Contesto.** Il flusso dalla GUI non è ripetibile né scriptabile. Per la metodologia clock/Fmax serve uno sweep del clock target HLS: stringendo il clock, l'area cresce. Si cerca prima il compromesso throughput/area in HLS, poi la Fmax in Vivado sull'RTL scelto.

**Modifiche** (`scripts/hls/`, commit `987314a`, `6d7082c`, `986025e`)
- `run_point.tcl`: una variante × un clock target, `csim → csynth → cosim → export_design -flow impl` (Vivado out-of-context). Impostazioni portate da `hls_config.cfg`: part, top, `sim.O`, `ip_catalog`. `clock_uncertainty=27%` è reso esplicito.
- `enable_auto_rewind=true` per **tutte** le varianti. **[WAS]** In `Ascon-Test/hls_config.cfg` era `=0`. **[OPT]** Il confronto ibrido vs standalone deve essere fatto a parità di condizioni.
- `run_all.bat` (tre varianti a un clock), `sweep_hls.bat` (griglia di clock, default 10 / 6.67 / 5 / 4 ns = 100 / 150 / 200 / 250 MHz), `collect.tcl` (archivio dei report e `summary.csv`). Il prefisso di ogni colonna indica la fonte: `csyn_` stima HLS, `syn_` Vivado post-synthesis, `impl_` post-route, `cosim_` cicli di co-simulazione.

**Risultati post-route** (`results/hls/baseline/summary.csv`; Vivado OOC, timing chiuso in tutti i 12 punti)

| Variante | T_HLS [ns] | LUT | FF | BRAM | CP post-route [ns] | f [MHz] | Cosim latency max (KAT) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Hybrid | 10 | 3281 | 2215 | 10 | 6.169 | 162.1 | 14352 |
| Hybrid | 6.67 | 3279 | 2222 | 10 | 5.329 | 187.7 | 14448 |
| Hybrid | 5 | 3142 | 2426 | 10 | 4.502 | 222.1 | 17009 |
| Hybrid | 4 | 3044 | 2600 | 6 | 3.819 | 261.8 | 17459 |
| SHA-3 | 10 | 2214 | 1551 | 10 | 6.023 | 166.0 | 14250 |
| SHA-3 | 6.67 | 2219 | 1565 | 10 | 5.362 | 186.5 | 14346 |
| SHA-3 | 5 | 2053 | 1829 | 10 | 4.165 | 240.1 | 16907 |
| SHA-3 | 4 | 1675 | 2002 | 10 | 3.814 | 262.2 | 17358 |
| Ascon | 10 | 1148 | 1020 | 4 | 4.735 | 211.2 | 317 |
| Ascon | 6.67 | 1148 | 1020 | 4 | 4.168 | 239.9 | 317 |
| Ascon | 5 | 1189 | 1086 | 4 | 4.072 | 245.6 | 341 |
| Ascon | 4 | 1178 | 1165 | 4 | 3.529 | 283.4 | 359 |

**Note**
- La latenza di cosim aggrega i 4 vettori KAT e non è confrontabile tra varianti: il throughput per algoritmo è misurato con LOG-011.
- Ascon @ 10 ns differisce da LOG-004 (1148 vs 1200 LUT, 1020 vs 744 FF) per `auto_rewind`. Da qui in avanti i valori di riferimento sono quelli del flusso Tcl.

---

## LOG-011 — Testbench in modalità throughput

**Contesto.** Le latenze di cosim sui KAT mescolano algoritmi e lunghezze diverse. Il throughput per algoritmo richiede il costo in cicli di un blocco di rate.

**Modifiche** (`*-Test/main.c`, `scripts/hls/run_cosim.tcl`, `cosim_all.bat`; commit `fb3c1ff`)
- **Senza argomenti:** i 4 KAT come prima. **[WAS]** `return 0`: csim/cosim davano PASS anche con un digest sbagliato. **[OPT]** Exit diverso da zero in caso di mismatch.
- **Con `-argv <algo>`** (`sha3-256`, `sha3-512`, `ascon`, più `sha3-256v` / `sha3-512v` per il controllo di linearità):
  1. KAT eseguiti in C puro su `hash()`, senza transazioni RTL;
  2. **esattamente due** transazioni RTL `L(len0)` e `L(len1)`, con `len1 − len0 = k · rate`.
  
  Dal report di cosim: `cicli/blocco = (lat_max − lat_min) / k`. Ogni digest RTL è confrontato con `hash()`.
- `run_cosim.tcl` riapre un punto già sintetizzato senza `-reset` (csynth e impl restano) e lancia una cosim per algoritmo → `cosim/<algo>.rpt`. Il report KAT viene salvato come `cosim/kat.rpt`.

**Verifica.** gcc: KAT OK e tutte le configurazioni di throughput OK; un algoritmo sconosciuto dà exit 1. **Cosim non ancora eseguita.**

---

## LOG-012 — Ristrutturazione repo: versioni di design e flusso di riferimento

**Contesto.**
- Esistevano due serie di report "ufficiali": quella della GUI (`*-Test/*-Test/reports/`, LOG-004) e quella del flusso Tcl (`results/hls/`), con configurazioni diverse.
- I risultati non erano separati per versione del design: un branch di ottimizzazione avrebbe sovrascritto i risultati della baseline.

**Modifiche**
- **Versione di design.** Nuovo `scripts/hls/design.txt` (tracciato) con l'etichetta della versione (`baseline`, `b01`, …), letta da `scripts/hls/common.tcl`; la variabile d'ambiente `DESIGN` la sovrascrive. Gli output finiscono in:
  - `build/hls/<design>/<variant>/T_<ns>/` (ignorato da git);
  - `results/hls/<design>/<variant>/T_<ns>/` e `results/hls/<design>/summary.csv` (tracciati).
  
  L'etichetta si cambia nel primo commit di ogni branch: il branch non può sovrascrivere i risultati di un altro design.
- `run_point.tcl`, `run_cosim.tcl`, `collect.tcl` usano `common.tcl` (`env_or`, `ROOT`, `DESIGN`).
- `collect.tcl`:
  - colonna `design` in `summary.csv`;
  - archivia anche `cosim/<algo>.rpt`;
  - per le colonne `cosim_*` usa `cosim/kat.rpt` quando esiste. **[WAS]** Dopo `run_cosim.tcl`, `hls/sim/report/` contiene l'ultimo run di throughput, quindi venivano letti i cicli sbagliati.
- `results/hls/{Hybrid,Sha,Ascon}-Test/` e `summary.csv` → `results/hls/baseline/` (`git mv`).
- **Output della GUI non più tracciati** (`*-Test/*-Test/`: report, summary, IP impacchettato). Restano nella storia git: i valori di LOG-004 sono recuperabili dal commit `fb3c1ff`. La GUI resta per l'ispezione (schedule viewer).
- `Ascon-Test/hls_config.cfg`: `enable_auto_rewind=0` → `=1`, come nel flusso Tcl (LOG-010).

**Regole di lavoro**
- `main` contiene solo design verificati: cosim PASS e voce nel LOGBOOK.
- Ogni voce del backlog si sviluppa in un branch dedicato (`b01-remove-union`, …), con `design.txt` aggiornato nel primo commit, e si integra in `main` con un merge.
- Il flusso di riferimento è quello Tcl (`scripts/hls/`). I sorgenti restano unici in `*-Test/`.

**Verifica.** `collect.tcl` testato con `tclsh` su un albero `build/` di prova (report KAT preservato, `cosim/*.rpt` archiviati, etichetta non valida rifiutata). `run_point.tcl` e `run_cosim.tcl` testati con comandi Vitis fittizi (path e banner). Il run reale di `collect.tcl` sui build spostati rigenera `results/hls/baseline/`.

---

## LOG-013 — B-01: rimozione della `union` sullo stato

**Contesto.** La `union { uint8_t b[200]; uint64_t q[25]; } st` in `hash_ctx_t` impedisce a HLS di separare lo stato dal resto del contesto. Nella baseline tutta la struct (stato + contatori) è un'unica memoria `H_i` da 64 bit × 27, su cui non si può applicare `ARRAY_PARTITION` (backlog B-01). Il lavoro si sviluppa nel branch `b01-remove-union`, con `design.txt` = `b01`. Ordine: Hybrid, poi SHA-3 e Ascon.

### Hybrid

**Modifiche** (`Hybrid-Test/hash.h`, `hash.c`; commit `4abf98a`)
- `hash.h`: **[WAS]** union → **[OPT]** `uint64_t st[25]`.
- `hash.c`: due helper `INLINE`, `st_get_byte()` e `st_xor_byte()`, ricostruiscono la vista a byte con shift/mask. Il byte `idx` sta nella word `idx >> 3`, alla lane `idx & 7`, in little-endian come in FIPS 202 e SP 800-232. La mappatura quindi è la stessa per i due algoritmi e non dipende da `mode`.
- Accessi aggiornati:
  - 9 accessi a byte (absorb, padding, squeeze);
  - 2 accessi a word;
  - 6 call-site della permutazione.
- **Nessuna pragma nuova.** La `ARRAY_PARTITION` sullo stato resta commentata, così B-01 si misura da solo.

**Flusso** (`scripts/hls/`)
- `run_point.tcl`: nuovo parametro `AUTO_PARTITION`, default `off`, che imposta `config_array_partition -throughput_driven` (vedi sotto).
- `run_point.tcl` cancella anche la `cosim/` del punto dopo `open_component -reset`. **[WAS]** Il `kat.rpt` di una sintesi precedente sopravviveva al reset e veniva archiviato come KAT della nuova.
- `common.tcl`: `WARNING` se la variabile d'ambiente `DESIGN` non coincide con `design.txt`. Una variabile rimasta impostata nella shell aveva mandato un run b01 in `build/hls/baseline/`.
- `collect.tcl`: archivia `hls_config.cfg` senza la riga `cosim.argv=` lasciata da `run_cosim.tcl`.

**Partizionamento automatico di HLS: disattivato.** Al primo run di b01 con le impostazioni di default del tool (`throughput_driven auto`), HLS ha dedotto da solo `array_partition type=cyclic factor=3` sullo stato (`HLS 214-270`, a causa della `PIPELINE` di `theta_C`). Con 3 banchi da 9 word, ogni accesso con indice calcolato a runtime richiede banco = word % 3 e indirizzo = word / 3:
- negli helper a byte un `urem` sequenziale da 35 cicli più 4 DSP;
- `urem` da 8 cicli nei loop della permutazione, con profondità di pipeline da 2 a 9–10.

Risultato (run non archiviato; post-route e cosim @ 10 ns):

| Hybrid @ 10 ns | LUT | FF | BRAM | DSP | f [MHz] | cicli/blocco SHA3-256 / SHA3-512 / Ascon |
|---|---:|---:|---:|---:|---:|---|
| b01 + partizionamento automatico | 5300 | 4497 | 0 | 4 | 139.2 | 10338 / 7970 / 826 |

**Decisione:** `AUTO_PARTITION=off` come default del flusso. Ogni partizionamento dello stato sarà una pragma esplicita, annotata `[OPT]`. La baseline non cambia: in nessuno dei suoi 12 log c'è un messaggio `214-270`.

**Risultati** (`results/hls/b01/`, @ 10 ns; area e frequenza post-route Vivado OOC, cicli dalla cosim)

| Hybrid @ 10 ns | LUT | FF | BRAM | DSP | CP [ns] | f [MHz] | Cosim KAT min / avg / max |
|---|---:|---:|---:|---:|---:|---:|---|
| baseline | 3281 | 2215 | 10 | 0 | 6.169 | 162.1 | 2287 / 5853 / 14352 |
| b01 | 3183 | 2606 | 0 | 0 | 6.653 | 150.3 | 2281 / 5690 / 13832 |

Throughput dalla cosim in modalità throughput (LOG-011): cicli/blocco = `(L(len1) − L(len0)) / k`; TP = bit di rate × f post-route / cicli per blocco.

| Hybrid @ 10 ns | Algoritmo | L(len0) | L(len1) | k | cicli/blocco | TP [Mbps] | TP/kLUT |
|---|---|---:|---:|---:|---:|---:|---:|
| baseline | SHA3-256 | 3319 | 7241 | 1 | 3922 | 44.97 | 13.71 |
| baseline | SHA3-512 | 3351 | 14157 | 3 | 3602 | 25.92 | 7.90 |
| baseline | Ascon | 2287 | 17839 | 32 | 486 | 21.35 | 6.51 |
| b01 | SHA3-256 | 3309 | 6959 | 1 | 3650 | 44.80 | 14.08 |
| b01 | SHA3-512 | 3341 | 13715 | 3 | 3458 | 25.04 | 7.87 |
| b01 | Ascon | 2281 | 17321 | 32 | 470 | 20.47 | 6.43 |

**Osservazioni**
- **Area.** Le 10 BRAM della baseline scompaiono: in b01 Vivado mette lo stato in LUTRAM (stima csynth: `H_st` 64 bit × 25, `RAM_1WNR`). LUT −3%, FF +18%.
  - Ipotesi da verificare: senza la union le scritture a byte diventano read-modify-write su word da 64 bit, quindi non servono più i byte-enable delle BRAM.
- **Cicli.** Da −4% a −7% per blocco. Il percorso critico si allunga: f post-route da 162.1 a 150.3 MHz (−7%). Il throughput resta circa invariato (da −0.4% a −4%), il TP/kLUT è alla pari.
- **Baseline.** HLS aveva già scelto `RAM_1WNR` (RAM replicata con più porte di lettura) per la memoria della union: `theta_C` e `rho_pi_loop` arrivano già a II=1. I commenti nel codice secondo cui II=1 "non è raggiungibile per la RAM a 2 porte" non descrivono il comportamento della 2025.1: il costo era nelle BRAM replicate. I commenti vanno corretti.
- **Cosa sblocca B-01.** Lo stato ora è un array ordinario: è possibile la `ARRAY_PARTITION` esplicita (`complete` o `cyclic` con fattore potenza di 2/5) e la ristrutturazione dei loop. È il prossimo passo, misurabile rispetto a `b01`.

**Verifica**
- gcc: equivalenza (`tests/equivalence/run.sh`, 771 vettori contro hashlib/pyascon), KAT, modalità throughput, ASan/UBSan.
- HLS: csim PASS, cosim PASS (KAT e 3 run di throughput), timing chiuso.

**Note**
- I report della cosim di throughput della baseline Hybrid @ 10 ns (valori nella tabella sopra) sono andati persi con il build sovrascritto. Si rigenerano da `main` insieme a quelli della baseline SHA-3 e Ascon.

---

## Backlog

Attività pianificate, in ordine di priorità. Quando un'attività parte, riceve un `LOG-NNN`.

| ID | Attività | Varianti | Riferimento | Stato |
|---|---|---|---|---|
| B-01 | **Rimuovere la `union` su `st`** (LOG-013: Hybrid fatto): stato solo `uint64_t q[25]` (Ascon: `q[5]`), viste a byte ricostruite con shift/mask. Sblocca `ARRAY_PARTITION` sullo stato e II=1 nei loop limitati dalle porte | tutte | `docs/optimizations/*` §1 | in corso |
| B-02 | Rompere la dipendenza seriale in `rho_pi()` | Hybrid, SHA-3 | `ottimizzazioni_logiche_{hybrid,sha3}.md` §2 | da fare |
| B-03 | **Wrapper AXI4-Stream** (`hls::stream` + `ap_axiu`, TLAST sull'ultimo beat) per l'integrazione con AXI DMA. Da decidere: controllo via AXI-Lite oppure header in-band + `ap_ctrl_none` (32 bit, compatibile con il driver `uniss_dma` di [mdc-suite/AXI-based-driver-applications](https://github.com/mdc-suite/AXI-based-driver-applications)). Bozza AXI-Lite a 8 bit già verificata in C (KAT + TLAST) | Hybrid → poi tutte | `docs/optimizations/*` §3/§4 | in corso |
| B-04 | Block design Vivado: Zynq MPSoC + AXI DMA + IP, bitstream/`.xsa`, applicazione di test sulla KV260 | Hybrid → poi tutte | — | da fare |
| B-05 | Dataset di tesi: area stimata vs reale, II di sintesi vs II osservato in cosim, throughput/area per algoritmo | tutte | — | da fare |
| B-06 | Pulizia: rimuovere `keccakf_rndc` non usato in `keccakf_asconp12()` (Hybrid) e le variabili inutilizzate (`i`, `j`) | Hybrid | LOG-001 | da fare |
| B-07 | Ottimizzazioni Ascon minori: azzerare solo 5 word in `hash_init`, absorb a granularità di word | Ascon | `ottimizzazioni_logiche_ascon.md` §2–3 | da fare |
| B-08 | Rivalutare `PIPELINE II=1` su `rounds_loop`: in SHA-3 E5 ≈17× throughput/area a ≈2.8× LUT. Ha senso se la metrica è throughput/area e non area pura; da misurare dopo B-01 | SHA-3, Hybrid | LOG-006 | da fare |
| — | Dopo ogni modifica logica che cambia l'IP: incrementare `ip.version` nel package | tutte | — | regola |
