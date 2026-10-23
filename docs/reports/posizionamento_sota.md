# Posizionamento rispetto allo stato dell'arte

Acceleratori hardware su FPGA per **Ascon-Hash256** (NIST SP 800-232), **SHA3-256** e **SHA3-512** (FIPS 202).

> **Stato del documento:** bozza. Le voci ⏳ sono in attesa di dati (sintesi in corso, co-simulazioni dedicate, fonti momentaneamente non disponibili).

**Legenda.** `*` = valore **ricalcolato** da noi con TP = r · f / c (§2), non dichiarato dagli autori; ⚠ = dato da usare **con cautela** (motivo nel testo sotto la tabella); n.d. = **non dichiarato** dalla fonte; ⏳ = **da aggiornare**. `r/c` = round per ciclo; *c* = cicli per blocco; TP = throughput su messaggi lunghi.

---

## 1. Risultati

### 1.1 Ascon-Hash256

**Tabella 1 — Valori pubblicati (solo punti del fronte di Pareto per famiglia FPGA)**

| Lavoro | Arch. | FPGA | LUT | FF | Fmax (MHz) | c | TP (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|
| RECO-ASCON [5] | – | Zynq-7020 | 1083 | 681 | 200 ⚠ | 25 | 512.0* | Hash + Hasha |
| GMU finalisti, GMU2-x1 [2] | 1 r/c | Artix-7 | 1315 | 863 | 253 | 13* | 1247 | include AEAD |
| GMU Round 2, GMU2-v1h [1] | 1 r/c | Artix-7 | 1375 | 862 | 276 | 13 | 1358.8 | include AEAD |
| GMU finalisti, GMU2-x2 [2] | 2 r/c | Artix-7 | 2082 | 864 | 220 | 7* | 2009 | include AEAD |
| GMU Round 2, GMU2-v2h [1] | 2 r/c | Artix-7 | 2126 | 861 | 234 | 7 | 2139.4 | include AEAD |
| Khan et al. [3] | 1 r/c | Virtex-7 | 773 | n.d. | 272.03 | 12* | 1450.8* | solo hash ⚠ |
| Khan et al. [3] | 2 r/c | Virtex-7 | 1290 | n.d. | 224.56 | 6* | 2395.3* | solo hash ⚠ |
| Khan et al. [3] | 4 r/c | Virtex-7 | 2550 | n.d. | 162.68 | 3* | 3470.5* | solo hash ⚠ |

Il fronte Artix-7 include RECO-ASCON, implementato su una Zynq-7020, la cui logica programmabile è quella di un Artix-7. Le due edizioni del benchmark GMU usano device Artix-7 diversi (xc7a12t-3 per il Round 2, xc7a100t-2 per i finalisti); per i finalisti *c* è ricavato da TP e Fmax dichiarati. I core dei progettisti (Graz) non compaiono perché, nella stessa famiglia, sono dominati dai core GMU2; il loro successore, ascon-verilog, è presente come risintesi in Tabella 2.

Per Khan et al. il TP pubblicato (697.5, 1108.2 e 1488.4 Mbps) è misurato su un singolo messaggio di 256 bit; il *c* in tabella è ricavato dalla formula di latenza degli autori (96/z + 4 cicli), mentre tool e tipo di dato (sintesi o post-route) non sono dichiarati. Per RECO-ASCON, 200 MHz è la frequenza di funzionamento sulla PYNQ e non un'Fmax; *c* = 25 è invece dichiarato dagli autori (N = 25M + 78).

**Tabella 2 — Core di riferimento risintetizzati sulla xck26 (post-route)**

| Core | Vincolo | LUT | FF | BRAM / DSP | Fmax (MHz) | c | TP @100 MHz (Mbps) | TP @Fmax (Mbps) |
|---|---|---|---|---|---|---|---|---|
| ascon-verilog v4, solo hash | 10 ns | 1211 | 336 | 0 / 0 | 216.5 ⚠ | 13 | 492.3* | – |
| ascon-verilog v4, solo hash | 1.4 ns | 1330 | 336 | 0 / 0 | 547.3 | 13 | – | 2694.4* |
| GMU2 Ascon-128 x1 [8] | 10 ns | 1222 | 839 | 0 / 0 | 273.7 ⚠ | 13 | 492.3* | – |
| GMU2 Ascon-128 x1 [8] | 1.8 ns | 1327 | 839 | 0 / 0 | 535.3 ⚠ | 13 | – | 2635.3* |
| GMU2 Ascon-128 x2 [8] | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

Le righe a 10 ns danno area e TP alla stessa frequenza del nostro acceleratore; la loro Fmax è una stima pessimistica, perché con un vincolo largo il tool non spinge l'ottimizzazione, ed è confrontabile solo con quella del nostro acceleratore, ottenuta nelle stesse condizioni. Le righe con il vincolo più stretto danno la prestazione di picco. Per ascon-verilog la ricerca è stata spinta fino a 1.4 ns: tra 2.0 e 1.4 ns la stima oscilla tra 518.9 e 547.3 MHz per la normale variabilità del place and route, e il percorso critico è nella logica di controllo (dalla FSM al contatore), non nella permutazione. Per GMU2 la ricerca si è fermata a 1.8 ns, dove lo slack è appena negativo (−0.068 ns): 535.3 MHz è quindi vicino al limite ma non verificato oltre.

Il *c* = 13 di ascon-verilog è ricavato dai cicli pubblicati dal progettista (118 cicli per 32 B, 1730 per 1024 B); quello di GMU2 è dichiarato nel benchmark GMU Round 2. GMU2 include l'AEAD e implementa Ascon-Hash v1.2, che ha la stessa permutazione e lo stesso rate di Hash256. Il Verilog "x2" pubblicato nel repository BlueLight è generato con `UNROLL_FACTOR=1` ed è quindi identico a x1: la variante x2 richiede una rigenerazione con il compilatore Bluespec.

**Tabella 3 — Nostro acceleratore (stato attuale; Vitis HLS 2025.1, xck26, 10 ns, post-route)**

| Variante | LUT | FF | BRAM / DSP | Fmax (MHz) | Latenza co-simulazione (cicli) | c | TP (Mbps) |
|---|---|---|---|---|---|---|---|
| Ascon standalone (non è un riferimento) | 1200 | 744 | 4 / 0 | 211.8* | 183 (0 B) · 338 (23 B) | ⏳ | ⏳ |
| **Ibrido** (area condivisa) | **3281** | **2215** | **10 / 0** | **162.1*** | ⏳ | ⏳ | ⏳ |

Le tre varianti hanno ricevuto la stessa ottimizzazione con sole pragma, senza ottimizzazioni logiche, e accettano input fino a 256 B. L'Fmax è ricavata come 1000 / periodo post-route riportato da Vitis (4.722 ns per Ascon, 6.169 ns per l'ibrido). Le BRAM contengono la RAM di stato: la `union` sullo stato impedisce di mapparla su registri, mentre i core di riferimento non usano BRAM. Il report di co-simulazione dell'ibrido aggrega minimo, media e massimo su quattro messaggi di test (SHA3-256 32 B, SHA3-512 255 B, Ascon 23 B, Ascon 0 B), per cui la latenza in modalità Ascon non è separabile e richiede una co-simulazione dedicata. Il core Ascon è stato re-implementato il 23/10/2026, l'ibrido il 25/06/2026; da giugno il codice è cambiato solo nei commenti (LOGBOOK, LOG-006), e i due core standalone re-implementati riproducono esattamente i valori di giugno.

### 1.2 SHA3-256 / SHA3-512

**Tabella 4 — Valori pubblicati (solo punti del fronte di Pareto per famiglia FPGA)**

| Lavoro | Arch. | FPGA | LUT | FF | Fmax (MHz) | c | TP SHA3-256 (Mbps) | TP SHA3-512 (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|---|
| Kerckhof et al. [9] | per lane, compatto | Virtex-6 | 519 | 429 | 250 | 2137 | 128 | 67.4* | ⚠ |
| GMU SHA3-v1 [1] | ripiegato ×8 | Artix-7 | 1263 | 277 | 195 | 233 | 910.6 | – | senza padding |
| Choudhary et al. [10] | 1 r/c | Artix-7 | 2970 | 1607 | 244.31 | 25* | 10632.4* | 5628.9* | solo permutazione ⚠ |
| Choudhary et al. [10] | 1 r/c | Kintex-7 | 2970 | 1607 | 313.47 | 25* | 13642.2* | 7222.3* | solo permutazione ⚠ |

Ogni famiglia ha un solo punto, tranne Artix-7, dove GMU SHA3-v1 e Choudhary rappresentano i due estremi del compromesso area/TP. Per Kerckhof et al. il tool è ISE 12.1, non è dichiarato se i valori siano post-route e l'Fmax si riferisce all'opzione di sintesi "area". Choudhary et al. implementano la sola permutazione Keccak-p[1600,24], all'interno di un AEAD Ascon-Keccak, senza padding né I/O; il loro TP dichiarato (20062.6 Mbps su Kintex-7) usa 1600 bit per permutazione, cioè l'intero stato invece del rate, e il *c* in tabella è ricavato da quel valore. Con così pochi punti confrontabili, il riferimento principale per SHA-3 è il core risintetizzato in Tabella 5.

**Tabella 5 — Core di riferimento risintetizzato sulla xck26 (Keccak team high-speed core v3.1, post-route)**

| Core | Vincolo | LUT | FF | BRAM / DSP | Fmax (MHz) | c | TP @100 MHz (Mbps) | TP @Fmax (Mbps) |
|---|---|---|---|---|---|---|---|---|
| SHA3-256 | 10 ns | 4100 | 2708 | 0 / 0 | 171.5 ⚠ | 24 | 4533.3* | – |
| SHA3-256 | 2.2 ns | 4444 | 2708 | 0 / 0 | 401.6 | 24 | – | 18205.9* |
| SHA3-512 | 10 ns | 3634 | 2194 | 0 / 0 | 185.1 ⚠ | 24 | 2400.0* | – |
| SHA3-512 | 2.0 ns | 4162 | 2246 | 0 / 0 | 395.4 | 24 | – | 9489.6* |

Il core lavora a 1 round per ciclo, con rate parametrico e padding a carico dell'host; *c* = 24 è misurato in simulazione. La lettura delle righe a 10 ns è la stessa della Tabella 2. Il timing non chiude da 2.5 ns in giù: l'Fmax è il miglior valore stimato tra le passate, e il percorso critico è un round della permutazione (4 livelli di logica). I FF corrispondono ai 1600 bit di stato, più il buffer d'ingresso da *r* bit, più il controllo.

**Tabella 6 — Nostro acceleratore (stesse condizioni della Tabella 3)**

| Variante | LUT | FF | BRAM / DSP | Fmax (MHz) | Latenza co-simulazione (cicli) | c | TP (Mbps) |
|---|---|---|---|---|---|---|---|
| SHA-3 standalone (non è un riferimento) | 2214 | 1551 | 10 / 0 | 166.0* | 3449 (SHA3-256, 32 B) · 14250 (SHA3-512, 255 B) | ⏳ | ⏳ |
| **Ibrido** (area condivisa) | **3281** | **2215** | **10 / 0** | **162.1*** | 14352 (SHA3-512, 255 B) | ⏳ | ⏳ |

L'Fmax è ricavata dal periodo post-route (6.023 ns per SHA-3, 6.169 ns per l'ibrido); il core SHA-3 è stato re-implementato il 23/10/2026. La latenza è dell'ordine delle migliaia di cicli per messaggio, contro i 24 cicli per blocco del core di riferimento, ed è coerente con il collo di bottiglia della RAM di stato a due porte (backlog B-01). La latenza dell'ibrido per SHA3-256 non è separabile, per lo stesso motivo indicato sotto la Tabella 3.

### 1.3 HLS

**Tabella 7 — Implementazioni HLS pubblicate**

| Lavoro | Alg. | FPGA | LUT | FF | Fmax (MHz) | c | TP (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|
| Urbano-Molano & Velasco-Medina [15] | SHA3-512 | Virtex-7 | 6234 | 2597 | 162.31 | 49 | 1908 | ⚠ |
| AMD Vitis Security Library [16] | SHA3-256 | n.d. ⚠ | 44975 | 36787 | 306.65 ⚠ | 1104 | 302.2* | vendor |
| AMD Vitis Security Library [16] | SHA3-512 | n.d. ⚠ | 39794 | 32994 | 316.25 ⚠ | 1096 | 166.2* | vendor |
| AMD Vitis Security Library, sulla xck26 | SHA3-256/512 | xck26 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | risintesi prevista |

Urbano-Molano & Velasco-Medina usano Vivado HLS 2020.1, e il testo e la Tab. III del loro lavoro riportano riduzioni di area incoerenti tra loro. La libreria AMD non dichiara il device (il progetto di test è configurato per la scheda vck190, Versal), né se il periodo di clock sia una stima di sintesi o un valore post-route: non può quindi essere collocata su un fronte di Pareto e resta come riferimento HLS del vendor, in attesa della risintesi sulla xck26. Non è stata trovata alcuna implementazione HLS di Ascon-Hash. È noto in letteratura che un core RTL scritto a mano supera la stessa architettura in HLS: per Keccak su Virtex-6 il rapporto di throughput/area RTL/HLS è 1.84 [17].

---

## 2. Definizioni

- **AEAD** (Authenticated Encryption with Associated Data): cifratura autenticata, cioè cifratura più un tag di integrità. Molti core Ascon implementano AEAD e hash insieme, e la loro area include logica che un core solo hash non ha.
- **Rate *r*:** bit di messaggio assorbiti per blocco: 64 per Ascon-Hash, 1088 per SHA3-256, 576 per SHA3-512.
- **Cicli per blocco *c*:** cicli di clock tra l'assorbimento di due blocchi consecutivi, a regime.
- **Throughput su messaggi lunghi (TP):** TP = r · f / c, con *f* frequenza di clock. È la definizione dei benchmark GMU.
- **Latenza:** cicli necessari per elaborare un intero messaggio di lunghezza data.
- **Fmax:** massima frequenza di clock alla quale il design rispetta i vincoli di timing. Nelle nostre risintesi è il massimo di 1000 / (T − WNS) su una serie di periodi di clock T, dove WNS è lo slack peggiore del percorso critico.
- **Post-route:** valori ottenuti dopo place and route, cioè i più affidabili.
- **Out-of-context (OOC):** implementazione del core senza assegnare i pin di I/O.

## 3. Cosa offre la letteratura

La letteratura abbonda di implementazioni di SHA-3 su famiglie Virtex-5/6, con l'area espressa solo in slice. Per Ascon prevalgono le implementazioni AEAD, e i dati più solidi vengono dai benchmark delle competizioni (SHA-3, CAESAR, NIST LWC).

Scarseggiano invece i lavori su SHA-3 con area in LUT su famiglie 7-series e UltraScale+, i core dedicati al solo Ascon-Hash (quasi sempre integrato in un core AEAD), i lavori su Ascon-Hash256 secondo SP 800-232, standardizzato solo nell'agosto 2025, e le implementazioni HLS curate. Mancano del tutto un datapath condiviso Keccak-f / Ascon-p per l'hash e un'implementazione HLS di Ascon-Hash.

Su più punti la letteratura è poco convincente. Il throughput è definito in modi diversi, spesso non è dichiarato se i valori siano di sintesi o post-route, e in alcuni lavori la frequenza di funzionamento viene presentata come se fosse un'Fmax (Crypto-RV, RECO-ASCON, *Unified SHA-3/SHAKE*). Alcuni lavori, infine, sono incoerenti tra abstract, testo e tabelle, e una survey attribuisce all'FPGA dati ASIC (RECO-HCON) (Appendice A).

## 4. Metodologia

Sono inclusi solo i lavori che riportano l'area in LUT. Il numero di slice dipende da come il tool impacchetta LUT e FF e dalla famiglia FPGA (4 LUT per slice su 7-series, 8 LUT per CLB su UltraScale+), e non è convertibile in LUT. Si preferiscono i valori post-route, indicando il tipo di dato quando è dichiarato; i lavori con dati discordanti restano inclusi ma sono marcati ⚠. Non si fanno conversioni tra famiglie FPGA.

Tra i valori pubblicati restano solo i punti non dominati nel piano area (LUT) contro TP, all'interno della stessa famiglia FPGA. Un punto è dominato se un altro punto della stessa famiglia ha LUT minori o uguali e TP maggiore o uguale, con almeno una delle due differenze stretta. I lavori senza TP su messaggi lunghi non sono collocabili sul fronte e sono esclusi (Appendice B). Il filtro non si applica alle risintesi sulla xck26 né al nostro acceleratore, riportati per intero.

Come riferimenti standard usiamo i benchmark del Cryptographic Engineering Research Group della George Mason University (GMU) e i core dei progettisti. I benchmark GMU valutano in modo uniforme i candidati delle competizioni NIST: stessa FPGA e stesso tool per tutti, frequenza ottimizzata automaticamente (strumenti Minerva e Xeda), valori post-route e TP su messaggi lunghi. Per Ascon usiamo il Round 2 [1] e i finalisti [2]; il benchmark SHA-3 (ePrint 2012/368) riporta solo slice ed è escluso. I core dei progettisti sono implementazioni di riferimento pubbliche, conformi alla specifica e della stessa classe architetturale del nostro acceleratore (iterativa, 1 round per ciclo): non sono necessariamente i più efficienti, ma sono verificabili.

Tutti i riferimenti disponibili come sorgente sono implementati con Vivado 2025.1.1 sul part `xck26-sfvc784-2LV-c` (KV260), out-of-context, con valori post-route, su periodi di clock da 10 a 2.0 ns (per Ascon fino a 1.4 ns); in nessuna passata ci sono violazioni di hold o di larghezza d'impulso del clock. Per Ascon-Hash256 usiamo ascon-verilog (variante v4, bus a 64 bit, 1 round per ciclo) del progettista R. Primas, ridotto al solo hash fissando la modalità a `M_HASH256` e collegando a costanti gli ingressi AEAD, con permutazione e controllo invariati; i FF scendono a 336 già dopo la sintesi, a conferma che il registro della chiave AEAD (128 bit) viene rimosso. Per SHA-3 usiamo il high-speed core del Keccak team (v3.1, 1 round per ciclo), con rate e lunghezza del digest resi parametrici e round, stato e controllo invariati; il core, verificato in simulazione con GHDL contro `hashlib`, dà 45/45 digest corretti per ciascuna variante. Il core GMU2 è sintetizzato dal Verilog generato pubblicato in BlueLight [8], senza modifiche.

## 5. Ibridi e crypto-agili esistenti

Le soluzioni crypto-agili trovate seguono quattro strade. La più semplice affianca core separati selezionati da un multiplexer: Elomda et al. [18] combinano AES, Ascon e Keccak con un buffer condiviso, ma validano Ascon e Keccak solo in simulazione. Più comune è l'unificazione dentro la stessa famiglia di algoritmi: Crypto-RV [11] unifica SHA-3/SHAKE, SHA-2/SM3 e AES/Haraka, *Unified SHA-3/SHAKE* [13] copre tutte le modalità Keccak, RECO-HCON [4] unisce AEAD e hash di Ascon, RECO-ASCON [5] supporta Hash e Hasha, e ascon-verilog tutte le modalità di SP 800-232. Kundi et al. [19] condividono invece il datapath tra primitive diverse, AES e SHA-3. Infine, Ascon-Keccak AEAD [10][20] inserisce la permutazione Keccak dentro la modalità Ascon: usa una sola permutazione e non è un hash duale.

Non è stato trovato un datapath condiviso Keccak-f / Ascon-p per l'hash, con l'algoritmo selezionabile a runtime. L'equivalenza affine tra la S-box di Ascon e il χ di Keccak è dichiarata nella specifica di Ascon [21], ma non risulta sfruttata in hardware.

---

## Appendice A — Incoerenze rilevate

| Fonte | Incoerenza |
|---|---|
| Choudhary et al. [10] | TP calcolato sull'intero stato (1600 bit), non sul rate |
| Khan et al. [3] | TP misurato su un messaggio fisso di 256 bit |
| Kandi et al. [6] | Ascon-Hash descritto con b = 6 round, contro i 12 della specifica |
| Urbano-Molano & Velasco-Medina [15] | Riduzioni di area incoerenti tra testo e Tab. III |
| Sideris et al. (*Information* 2023) | TP nell'abstract doppio rispetto al testo |
| Abdulsamad & Répás (*Computers* 2026) | Numero di BRAM incoerente tra le tabelle |

## Appendice B — Lavori esclusi

Non dichiarano un TP su messaggi lunghi, e non sono quindi collocabili sul fronte di Pareto: Kandi et al. [6], CAST ASCON-F [7], Crypto-RV [11], Gavrilan et al. [12] (⏳ originale momentaneamente non disponibile), *Unified SHA-3/SHAKE* [13] e CAST SHA-3 [14].

Riportano l'area solo in slice o in percentuale: Gaj et al. (ePrint 2012/368), Sravani & Durai (*TVLSI* 2022), Sideris et al. (*Information* 2023; *JCEN* 2024), Assad et al. (*IJECE* 2022), Wong et al. (ISCAS 2018), El Moumni et al. (*Microelectronics J.* 2019), Rao et al. (*IJSSIS* 2019), Sundal & Chaves (ISVLSI 2017), Kundi et al. (*TCAS-I* 2020), Jacinto et al. (MWSCAS 2017), Homsirikamol & Gaj (ARC 2015) e Abdulsamad & Répás (*Computers* 2026).

## Appendice C — Fonti

| # | Riferimento | Stato |
|---|---|---|
| [1] | K. Mohajerani et al., *FPGA Benchmarking of Round 2 Candidates in the NIST LWC Standardization Process*, ePrint 2020/1207 | ✔ |
| [2] | K. Mohajerani et al., *SCA Evaluation and Benchmarking of Finalists in the NIST LWC Standardization Process*, ePrint 2023/484 | ✔ |
| [3] | S. Khan et al., *Area–Time Efficient Implementation of NIST Lightweight Hash Functions Targeting IoT Applications*, IEEE IoT-J 10(9), 2023 | ✔ |
| [4] | X. Wei et al., *RECO-HCON: A High-Throughput Reconfigurable Compact ASCON Processor for Trusted IoT*, IEEE SOCC 2022 | ✔ |
| [5] | M. El-Hadedy et al., *RECO-ASCON: Reconfigurable ASCON hash functions for IoT applications*, Integration 93, 2023 | ✔ |
| [6] | A. Kandi et al., *Side-Channel and Fault Resistant ASCON Implementation*, IEEE ISVLSI 2024 (ePrint 2024/984) | ✔ |
| [7] | CAST Inc., *ASCON-F IP core*, datasheet | ✔ |
| [8] | K. Mohajerani, *BlueLight* (Bluespec LWC implementations), github.com/kammoh/bluelight | ✔ |
| [9] | S. Kerckhof et al., *Compact FPGA Implementations of the Five SHA-3 Finalists*, CARDIS 2011 | ✔ |
| [10] | R. Choudhary et al., *ASCON-KECCAK AEAD: A Novel Hardware Architecture for Cryptographic Processing*, TechRxiv, 2026 (preprint) | ✔ |
| [11] | A. K. Pham et al., *Crypto-RV: High-Efficiency FPGA-Based RISC-V Cryptographic Co-Processor for IoT Security*, arXiv 2602.04415, 2026 | ✔ |
| [12] | I. Gavrilan et al., *Impeccable Keccak: Towards Fault Resilient SPHINCS+ Implementations*, TCHES 2024(2) | ⏳ momentaneamente non disponibile |
| [13] | *Lightweight Unified Sha-3/Shake Architecture with a Fault-Resilient State*, arXiv 2512.03616, 2025 | ✔ |
| [14] | CAST Inc., *SHA-3 IP core*, datasheet | ✔ |
| [15] | F. A. Urbano-Molano, J. Velasco-Medina, *SHA-3 Implementation for Post-Quantum Cryptography using High-Level Synthesis*, SPL 2023 | ✔ |
| [16] | AMD, *Vitis Security Library — SHA-3*, docs v2020.1 / v2026.1 | ✔ |
| [17] | E. Homsirikamol, K. Gaj, *Hardware Benchmarking of Cryptographic Algorithms Using High-Level Synthesis Tools: The SHA-3 Contest Case Study*, ARC 2015 | ✔ |
| [18] | M. Elomda et al., *Crypto-Agile FPGA Architecture with Single-Cycle Switching for OFDM-Based Vehicular Networks*, Signals 7(2), 2026 | ✔ |
| [19] | D. Kundi et al., *Resource-Shared Crypto-Coprocessor of AES Enc/Dec With SHA-3*, IEEE TCAS-I 67(12), 2020 | ✔ |
| [20] | S. Müller, *Ascon-Keccak AEAD Algorithm*, ePrint 2024/858 | ✔ |
| [21] | C. Dobraunig et al., *Ascon v1.2: Lightweight Authenticated Encryption and Hashing*, J. Cryptology 34, 2021 | ✔ |
| – | GMU ATHENa, sorgenti RTL SHA-3 (Round 3) | ⏳ momentaneamente non disponibile |
| – | Keccak team, *Keccak hardware implementations* v3.1 (high-speed core) | ✔ |
| – | R. Primas, *ascon-verilog* (SP 800-232), github.com/rprimas/ascon-verilog | ✔ |
