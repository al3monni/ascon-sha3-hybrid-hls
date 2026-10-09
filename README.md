# ascon-sha3-hybrid-hls — Crypto-Agile FPGA Hashing (SHA-3 / Ascon)

Implementazione **crypto-agile** su FPGA di algoritmi di hash crittografici — **SHA-3/Keccak** e **Ascon-Hash** — realizzata in **Vitis HLS** con target **AMD Kria KV260** (Zynq UltraScale+ `xck26`).

> La root di questa repo **coincide con il workspace Vitis Unified (2025.1)** usato in locale: ogni cartella `*-Test/` è un componente HLS apribile direttamente dall'IDE. Lo storico delle modifiche al codice è tracciato in [`LOGBOOK.md`](LOGBOOK.md).

Un **acceleratore ibrido** (un unico datapath che calcola entrambi gli algoritmi, selezionabili a runtime) è una scelta giustificata rispetto a due implementazioni *standalone*, misurando il costo dell'astrazione unificata su area, throughput e latenza?

---

## Indice

- [Contesto e motivazione](#contesto-e-motivazione)
- [Perché un'implementazione ibrida](#perché-unimplementazione-ibrida)
- [Struttura del repository](#struttura-del-repository)
- [Le tre varianti e le due basi di codice](#le-tre-varianti-e-le-due-basi-di-codice)
- [API e interfaccia](#api-e-interfaccia)
- [Filosofia di ottimizzazione e metrica](#filosofia-di-ottimizzazione-e-metrica)
- [Il collo di bottiglia della union](#il-collo-di-bottiglia-della-union)
- [Verifica funzionale (KAT)](#verifica-funzionale-kat)
- [Risultati HLS attuali](#risultati-hls-attuali)
- [Come compilare ed eseguire i test](#come-compilare-ed-eseguire-i-test)
- [Workflow Vitis ↔ Git](#workflow-vitis--git)
- [Materiale bibliografico](#materiale-bibliografico)
- [Stato del progetto e roadmap](#stato-del-progetto-e-roadmap)
- [Riferimenti](#riferimenti)

---

## Contesto e motivazione

Il progetto si inserisce in **MYRTUS**, dove un **Security Manager** definisce il livello di sicurezza richiesto e un **Node Manager** decide se eseguire una primitiva di sicurezza in **hardware** o in **software**. I nodi devono quindi offrire le primitive di sicurezza in entrambe le forme, e gli acceleratori possono esistere in più versioni a seconda dei requisiti (latenza / area).

La cornice teorica è la **crypto agility** così come definita da **NIST CSWP 39**: la capacità di sostituire e adattare gli algoritmi crittografici preservando sicurezza e continuità operativa, includendo la selezione a runtime tra algoritmi disponibili. Due punti del documento NIST sono particolarmente rilevanti per questa tesi:

- Le **FPGA** sono l'eccezione tra le piattaforme hardware: la loro natura riconfigurabile consente gradi di crypto-agility che chip e moduli dedicati (es. TPM) non offrono.
- La sfida più grande alla crypto-agility sono i **vincoli hardware**; il **riuso di acceleratori** è un'area di ricerca importante, soprattutto per completare la transizione **Post-Quantum Cryptography (PQC)**.

## Perché un'implementazione ibrida

Argomenti a favore dell'acceleratore ibrido (dal materiale di analisi del progetto):

- **Risparmio di area** condividendo la logica costosa.
- **Protocol agility sullo stesso SoC** (caso dei nodi MYRTUS): un unico acceleratore evita il *context switching* e il deployment separato di più container/acceleratori.
- **Transizione PQC**: alcuni standard NIST usano SHA-3, altri Ascon-Hash; un coprocessore crittografico in fase di transizione necessita di entrambi.
- **Certificazione side-channel**: un solo acceleratore è più semplice da certificare rispetto a due.
- **Core crypto-agile** come proprietà progettata in anticipo.

L'osservazione tecnica centrale che rende l'ibrido conveniente: il layer non lineare **χ (chi)** è l'unico componente *gate-expensive* in entrambe le permutazioni; le altre operazioni (rotazioni, XOR, addizione delle costanti) hanno costo molto basso su FPGA. **Condividere l'unico blocco che costa gate** consente di ottenere un'area quasi ottimale pur supportando due algoritmi.

Contro-argomenti considerati (e in gran parte **smentiti** dai dati raccolti finora): il timore di un throughput inferiore e l'impossibilità di usare i due algoritmi contemporaneamente (non rilevante nel caso d'uso MYRTUS).

## Struttura del repository

```
ascon-sha3-hybrid-hls/            # = workspace Vitis Unified (Vitis-HLS_Components)
├── README.md
├── LOGBOOK.md                    # storico dettagliato delle modifiche al codice
├── .gitignore  .gitattributes    # esclusi build/cache Vitis; testo normalizzato LF
│
├── Hybrid-Test/                  # componente HLS — variante ibrida (SHA-3 + Ascon, `mode` a runtime)
│   ├── hash.h  hash.c  main.c    #   sorgenti + testbench KAT
│   ├── hls_config.cfg            #   configurazione HLS (part, top, flow, file)
│   ├── vitis-comp.json           #   descrittore del componente per l'IDE
│   └── Hybrid-Test/              #   work_dir: solo gli output utili sono versionati
│       ├── reports/              #     hls_compile / hls_cosim / hls_impl_syn / hls_impl_pnr
│       ├── *.hls*_summary        #     riepiloghi per step (csim, compile, cosim, package, impl)
│       └── hash_top.zip          #     IP packaged (Vivado IP catalog)
├── Sha-Test/                     # componente HLS — SHA-3 standalone (stessa struttura)
├── Ascon-Test/                   # componente HLS — Ascon-Hash standalone (stessa struttura)
│
├── benchmarks/
│   └── software-latency/         # benchmark di latenza SW hybrid vs standalone (gcc -O2 -march=native)
│       ├── run_all.sh  bench_common.h  README.txt
│       ├── {hybrid,sha3,ascon}-test/   # hash.c/h senza pragma + main.c di misura
│       └── result.txt            # risultati (tempo, cicli, throughput, Δ%)
│
├── docs/
│   ├── optimizations/            # candidati di ottimizzazione logica, uno per variante
│   ├── reports/                  # report su pragma HLS ed esplorazione delle prestazioni (E0…E12, NE0)
│   ├── presentations/            # slide (latenza software)
│   └── notes/                    # Notes.txt (motivazioni, pro/contro) e TODO.txt
│
├── reference/
│   └── original-hybrid-code/     # base di partenza dell'ibrido (Makefile + sorgenti, pre-HLS)
│
└── paper/                        # materiale bibliografico
    ├── Integrating_FPGA-Based_Acceleration_in_Industrial_Motion_Control_System.pdf
    ├── MYRTUS-D3.1-MYRTUSReferenceInfrastructure-v1.pdf
    ├── ascon/                    # paper e standard su Ascon (incl. NIST SP 800-232)
    ├── sha3/                     # paper e standard su SHA-3 (incl. NIST FIPS 202)
    └── crypto-agility/           # NIST CSWP 39, sintesi e slide sulla crypto-agility
```

Le cartelle `vivado/` (block design KV260) e `sw/` (applicazioni di test sulla board) verranno aggiunte con l'integrazione di sistema.

## Le tre varianti e le due basi di codice

**Tre varianti**, una per componente Vitis:

| Componente | Top-level | Descrizione |
|---|---|---|
| **`Hybrid-Test/`** | `hash_top` | Core unificato SHA-3 + Ascon con parametro `mode` a runtime. |
| **`Sha-Test/`** | `hash_top` | Implementazione dedicata SHA-3/Keccak. |
| **`Ascon-Test/`** | `hash_top` | Implementazione dedicata Ascon-Hash. |

Ogni variante è composta da `hash.h`, `hash.c`, `main.c` e `hls_config.cfg`; i candidati di ottimizzazione a livello logico, ordinati per impatto, sono in `docs/optimizations/ottimizzazioni_logiche_{hybrid,sha3,ascon}.md`.

**Due basi di codice:**

- `reference/original-hybrid-code/` — la base di partenza (con `Makefile`), non ancora ottimizzata per HLS.
- `*-Test/` — le varianti dopo il **pass di ottimizzazione pragma-only**: ogni pragma è accompagnata da un commento (in inglese) che ne spiega il razionale e i limiti, ad es. perché un loop non raggiunge `II=1`.

La convenzione `mode` nel core unificato è:

```c
// mode == 0  -->  Ascon  (permutazione a 12 round, Chi su 5 word)
// mode  > 0  -->  SHA-3  (permutazione a 24 round, Chi su 25 word)
```

## API e interfaccia

Interfaccia in stile **OpenSSL**, con separazione tra la funzione software riusabile (`hash`, basata su puntatori) e il **wrapper hardware sintetizzabile** (`hash_top`):

```c
// state context
typedef struct {
    union {                 // due viste sulla stessa area di memoria:
        uint8_t  b[200];    //   8-bit bytes
        uint64_t q[25];     //   64-bit words
    } st;
    int pt, rsiz, mdlen, mode;
} hash_ctx_t;

// funzione di compressione condivisa (permutazione Keccak-f / Ascon-p12)
void keccakf_asconp12(uint64_t st[25], uint8_t mode);

// interfaccia OpenSSL-like
int hash_init  (hash_ctx_t *c, int mdlen, uint8_t mode);
int hash_update(hash_ctx_t *c, const void *data, size_t len, uint8_t mode);
int hash_final (void *md, hash_ctx_t *c, uint8_t mode);

// funzione software riusabile (basata su puntatori)
void hash(const void *in, size_t inlen, void *md, int mdlen, uint8_t mode);

// wrapper hardware sintetizzabile (confine per la sintesi HLS)
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode);
```

Il wrapper `hash_top` è il **confine hardware sintetizzabile** attorno alla funzione `hash` basata su puntatori, che resta disponibile come funzione software riusabile.

## Filosofia di ottimizzazione e metrica

**Metrica di confronto:** *throughput-to-area ratio*.

**Approccio HLS (area-first):** l'ottimizzazione privilegia l'area, con il throughput come obiettivo secondario. Le scelte principali:

- **Datapath di permutazione unico e condiviso** tramite `#pragma HLS INLINE off` su `keccakf_asconp12`: tutte le call-site (init/update/final) condividono una sola istanza fisica, riusata da entrambi gli algoritmi. Il report post-route conferma un'unica istanza `grp_keccakf_asconp12_1` in tutte e tre le varianti, quindi `ALLOCATION instances=1` non è necessaria (vedi `LOGBOOK.md`, LOG-005).
- **Leaf transform inline** per ridurre l'overhead di chiamata.
- **`bc[5]` e tabelle costanti partizionate completamente**.
- **Loop mantenuti rolled** per risparmiare area; `II=1` solo dove la RAM di stato a 2 porte può fisicamente sostenerlo.

Nella variante unificata i **conteggi di round variabili a runtime** (dipendenti da `mode`) impediscono l'unrolling che è invece possibile nelle varianti standalone, dove i limiti sono costanti a compile-time.

## Il collo di bottiglia della union

Il vincolo strutturale centrale — e uno degli argomenti forti della tesi — è la **`union`** in `hash_ctx_t` che permette di vedere lo stato come `uint8_t b[200]` e `uint64_t q[25]`. Le due viste servono perché lo stato viene indirizzato in due modi (ad es. nell'absorbing loop), ma:

- costringono lo stato in una **singola RAM a 2 porte**;
- lo rendono **non partizionabile** da HLS (non è partizionabile nemmeno un singolo campo della union);
- impediscono ad **`ARRAY_PARTITION` di sbloccare l'unrolling** dei loop consumatori;
- limitano l'**II** raggiungibile dai cicli interni quando servono più di due letture/scritture per ciclo.

La direzione di soluzione, documentata in `ottimizzazioni_logiche.md`, è **indirizzare lo stato solo per word** (`uint64_t q[25]`) e ricostruire le viste a byte via shift/mask, così da abilitare ad esempio:

```c
#pragma HLS ARRAY_PARTITION variable=q type=cyclic factor=5 dim=1  // colonne su array separati
#pragma HLS ARRAY_PARTITION variable=q type=complete dim=1        // parallelismo totale [+ area]
```

Questo impatterebbe l'absorbing loop in `hash_update()` e il padding/squeezing in `hash_final()`.

## Verifica funzionale (KAT)

La correttezza è verificata con **Known Answer Test (KAT)** tramite gcc. Il testbench (`main.c`) copre **SHA3-256** (messaggio corto), **SHA3-512** (messaggio multiblocco) e **Ascon-Hash** (output a 256 bit); confronta il digest calcolato con quello atteso (`memcmp`) e stampa `All Self-Tests OK!` in caso di successo.

## Risultati HLS attuali

Flusso completo eseguito in Vitis HLS 2025.1 su tutte e tre le varianti (C Simulation → Synthesis → C/RTL Co-simulation → Package → Implementation out-of-context), part `xck26-sfvc784-2LV-c`, clock target **10 ns (100 MHz)**. Valori post-route, dai report in `*/*/reports/`:

| Variante | LUT | FF | BRAM | DSP | Period post-route | Cosim latency min / avg / max (cicli) |
|---|---:|---:|---:|---:|---:|---|
| Hybrid | 3281 | 2215 | 10 | 0 | 6.169 ns | 2287 / 5853 / 14352 |
| SHA-3 | 2214 | 1551 | 10 | 0 | 6.023 ns | 3449 / 8849 / 14250 |
| Ascon | 1200 | 744 | 4 | 0 | 4.722 ns | 183 / 260 / 338 |

Timing chiuso in tutte le varianti. Le latenze di cosim aggregano i vettori KAT del testbench (messaggi e algoritmi diversi), quindi non sono direttamente confrontabili tra varianti: il confronto *throughput-to-area* per algoritmo è in roadmap.

> ⚠️ In questa versione le `#pragma HLS INTERFACE` di `hash_top` sono commentate: l'IP esportato ha porte `ap_memory`/`ap_none` e **non è ancora integrabile** in un block design con lo Zynq PS. Il wrapper AXI è il prossimo step (vedi `LOGBOOK.md`).

## Come compilare ed eseguire i test

**Benchmark di latenza software** — hybrid vs standalone, da WSL/Linux:

```bash
cd benchmarks/software-latency
./run_all.sh          # oppure ./run_all.sh quick (~10 s); output in results/result.txt
```

**Software (gcc)** — codice di riferimento pre-HLS:

```bash
cd reference/original-hybrid-code
make            # gcc -Wall -O3; `make clean` rimuove i binari
./hashtest      # atteso: All Self-Tests OK!
```

Le varianti `*-Test/` compilano anche con gcc (`gcc -O2 hash.c main.c`): le pragma HLS vengono ignorate.

**Vitis HLS (IDE)** — aprire la root della repo come workspace in Vitis Unified 2025.1, selezionare il componente e lanciare in sequenza *C Simulation*, *C Synthesis*, *C/RTL Co-simulation*, *Package*, *Implementation*. Tutta la configurazione è in `hls_config.cfg` del componente.

**Vitis HLS (riga di comando)** — equivalente, dalla cartella del componente:

```bash
vitis-run --mode hls --csim  --config ./hls_config.cfg --work_dir <Componente>
v++ -c    --mode hls         --config ./hls_config.cfg --work_dir <Componente>
vitis-run --mode hls --cosim --config ./hls_config.cfg --work_dir <Componente>
vitis-run --mode hls --package --config ./hls_config.cfg --work_dir <Componente>
vitis-run --mode hls --impl  --config ./hls_config.cfg --work_dir <Componente>
```

## Workflow Vitis ↔ Git

- Il lavoro si svolge in Vitis su Windows (`C:\Users\monni\Desktop\Vitis-HLS_Components`); i commit si fanno da **WSL** sulla stessa cartella (`/mnt/c/Users/monni/Desktop/Vitis-HLS_Components`).
- Sono versionati **sorgenti, `hls_config.cfg`, `vitis-comp.json`, report, summary e IP packaged**; build, cache, log e metadati dell'IDE sono esclusi dal `.gitignore`.
- Ogni modifica al codice o alla configurazione corrisponde a una voce in `LOGBOOK.md` (motivazione, file toccati, risultati prima/dopo) e a un commit che la cita (es. `LOG-007: remove union from hash_ctx_t`).
- Prima di committare nuovi risultati si rilancia il flusso HLS, così che report e sorgenti restino coerenti.

## Materiale bibliografico

La cartella `paper/` raccoglie la letteratura di riferimento, organizzata per tema:

- **`sha3/`** — implementazioni FPGA di SHA-3 e lo standard **NIST FIPS 202**.
- **`ascon/`** — implementazioni hardware di Ascon e lo standard **NIST SP 800-232**.
- **`crypto-agility/`** — **NIST CSWP 39** più una sintesi ragionata e slide sul tema.
- Livello superiore — **MYRTUS D3.1** (reference infrastructure) e un paper sull'accelerazione FPGA nel motion control industriale.

## Stato del progetto e roadmap

**Fatto (presente nel repo)**
- [x] Base di codice ibrida funzionante con verifica KAT (SHA-3 + Ascon).
- [x] Tre varianti (`hybrid`, `sha3`, `ascon`) dopo il **pass di ottimizzazione pragma-only**.
- [x] Candidati di ottimizzazione logica documentati per variante (`ottimizzazioni_logiche*.md`).
- [x] Flusso Vitis HLS completo (csim → synth → cosim → package → impl OOC) sulle tre varianti, timing chiuso a 100 MHz.
- [x] Benchmark software hybrid vs standalone (parità: SHA3-256 Δ ≈ −0.9%, Ascon ≈ −0.1%, SHA3-512 ≈ +3.8%) — `benchmarks/software-latency/`.
- [x] Esplorazione delle pragma documentata (`docs/reports/`) e riportata nel codice come commenti `[EXPLORED]`.
- [x] Repo allineata al workspace Vitis locale + `LOGBOOK.md`.
- [x] Inquadramento teorico della crypto-agility (NIST CSWP 39) e raccolta bibliografica.

**Roadmap (da `TODO.txt`)**

*Priorità / test hardware*
- [ ] Test dell'implementazione ibrida su ARM Kria.
- [ ] Ottimizzazioni logiche pendenti, a partire dalla rimozione della `union` su `st` (`docs/optimizations/`).
- [ ] **Wrapping dell'acceleratore ibrido** (interfaccia AXI4-Stream + AXI DMA) + test su FPGA Kria.
- [ ] Block design Vivado (Zynq MPSoC + AXI DMA + IP), bitstream e applicazione di test sulla KV260.
- [ ] Dataset post-implementation completo: area stimata vs reale, II di sintesi vs II osservato in cosim.

*Teoria scritta*
- [ ] Motivare la tesi: MYRTUS Security Manager + Security Levels + Crypto-Agility.
- [ ] Stato dell'arte delle implementazioni SHA-3 / Ascon e delle implementazioni **ibride**.
- [ ] Stato dell'arte delle **metriche** usate.

*Pragma*
- [x] Trovare la configurazione ottimale delle pragma e preparare il **report** relativo
- [ ] Organizzare le prestazioni per configurazione.

*Latenza software*
- [ ] Misurare la latenza software su ARM Kria e su PC + report e grafici.
- [ ] Verificare se i risultati rientrano nella deviazione standard.
- [ ] Valutare l'impatto del contorno (uso CPU/RAM del processo) sull'esecuzione del solo acceleratore.
- [ ] Confrontare le prestazioni con lo stato dell'arte.

## Riferimenti

- **NIST FIPS 202** — SHA-3 Standard: Permutation-Based Hash and Extendable-Output Functions.
- **NIST SP 800-232** — Ascon-Based Lightweight Cryptography Standards.
- **NIST CSWP 39** — Considerations for Achieving Crypto Agility.
- **AMD UG1399** — Vitis HLS pragma reference.
- Documentazione **AMD Kria KV260** / Zynq UltraScale+ `xck26`.
- **MYRTUS D3.1** — Reference Infrastructure (in `paper/`).

