# Posizionamento rispetto allo stato dell'arte

Acceleratori hardware per **Ascon-Hash256** (NIST SP 800-232), **SHA3-256** e **SHA3-512** (FIPS 202) su FPGA.

> **Stato del documento:** bozza. Le voci ⏳ sono in attesa di dati: report del nostro acceleratore in rigenerazione, sintesi in corso, fonti momentaneamente non disponibili.

**Legenda**

| Simbolo | Significato |
|---|---|
| `*` | valore **ricalcolato** da noi con la formula di §1, non dichiarato dagli autori |
| ⚠ | dato da usare **con cautela**: incoerenze, metodologia non dichiarata o dato non verificabile (motivo in nota) |
| n.d. | **non dichiarato** nella fonte |
| ⏳ | **da aggiornare**: dato in arrivo o fonte momentaneamente non disponibile |

---

## 1. Definizioni

- **AEAD (Authenticated Encryption with Associated Data):** cifratura autenticata, cioè cifratura più tag di integrità. Molti core Ascon implementano AEAD e hash insieme, quindi la loro area include logica che un core solo hash non ha.
- **Rate *r*:** bit di messaggio assorbiti per blocco. Ascon-Hash: 64; SHA3-256: 1088; SHA3-512: 576.
- **Cicli/blocco *c*:** cicli di clock tra l'assorbimento di due blocchi consecutivi, a regime.
- **Throughput su messaggi lunghi:** **TP = r · f / c**, con *f* la frequenza di clock. È la definizione dei benchmark GMU; trascura i costi di inizializzazione e finalizzazione.
- **Latenza:** cicli necessari per elaborare un intero messaggio di lunghezza data.
- **Fmax:** massima frequenza di clock alla quale il design rispetta i vincoli di timing. Nelle nostre risintesi è il massimo di 1000 / (T − WNS) su una serie di periodi di clock T. WNS è lo slack peggiore sul percorso critico.
- **Post-route:** valori dopo place and route, cioè i più affidabili. **Out-of-context (OOC):** il core viene implementato senza assegnare i pin di I/O.

## 2. Cosa offre la letteratura

**Dove abbonda**
- SHA-3 ad alto throughput (round srotolati, pipelining, elaborazione di più messaggi in parallelo) su **Virtex-5/6**, con area espressa **solo in slice**.
- Ascon in modalità **AEAD**.
- **Benchmark delle competizioni** (SHA-3, CAESAR, NIST LWC).

**Dove scarseggia**
- SHA-3 su 7-series e UltraScale+ con area in LUT.
- Ascon-Hash come core dedicato: quasi sempre è integrato in un core AEAD.
- Ascon-Hash256 secondo SP 800-232 (standard di agosto 2025).
- Implementazioni HLS curate. Quelle trovate richiedono fino a migliaia di cicli per blocco.

**Cosa manca**
- Un **datapath condiviso Keccak-f / Ascon-p** per l'hash, in RTL o HLS (§7).
- **Ascon-Hash in HLS.**

**Dove non è convincente** (dettaglio in Appendice A)
- **Definizioni di throughput diverse:** su un messaggio fisso (Khan), sull'intero stato invece che sul rate (Choudhary), su "bit per ciclo" esclusa l'inizializzazione (CAST).
- **Metodologia non dichiarata:** dati di sintesi o post-route non distinguibili.
- **Frequenza di funzionamento presentata al posto dell'Fmax:** Crypto-RV, RECO-ASCON, *Unified SHA-3/SHAKE*.
- **Incoerenze interne** tra abstract, testo e tabelle, e dati ASIC attribuiti all'FPGA da una survey (RECO-HCON).

## 3. Metodologia

### 3.1 Criteri di inclusione
- **Inclusi solo i lavori con area in LUT.** I lavori in sole slice sono esclusi: il numero di slice dipende da come il tool impacchetta LUT e FF e dalla famiglia FPGA, e non è convertibile in LUT. L'elenco degli esclusi è in Appendice B.
- Valori **post-route** preferiti. Il tipo di dato è indicato quando dichiarato.
- I lavori con dati discordanti restano inclusi, marcati ⚠.
- Nessuna conversione tra famiglie FPGA: ogni valore è riportato sulla famiglia d'origine.

### 3.2 Riferimenti standard
- **Benchmark GMU:** il Cryptographic Engineering Research Group della George Mason University valuta in modo uniforme i candidati delle competizioni NIST. Usa la stessa FPGA e lo stesso tool per tutti, ottimizza automaticamente la frequenza (strumenti Minerva e Xeda), riporta valori post-route e TP su messaggi lunghi. Per Ascon usiamo il Round 2 (ePrint 2020/1207) e i finalisti (ePrint 2023/484). Il benchmark SHA-3 (ePrint 2012/368) riporta solo slice ed è escluso.
- **Core dei progettisti:** implementazioni di riferimento pubbliche, conformi alla specifica e della stessa classe architetturale del nostro (iterativa, 1 round per ciclo). Non sono necessariamente le più efficienti; sono il riferimento verificabile.

### 3.3 Risintesi sulla xck26
- **Flusso:** Vivado 2025.1.1, part `xck26-sfvc784-2LV-c` (KV260), out-of-context, valori post-route. Periodi di clock: 10, 3.0, 2.5, 2.2, 2.0 ns; per Ascon anche 1.8, 1.6, 1.5, 1.4 ns. Controllati hold e larghezza d'impulso del clock: nessuna violazione.
- **Ascon-Hash256:** `rprimas/ascon-verilog`, variante v4 (bus a 64 bit, 1 round per ciclo), del progettista R. Primas. **Ridotto al solo hash** fissando la modalità a `M_HASH256` e collegando a costanti gli ingressi AEAD; permutazione e controllo restano invariati. Verifica: i FF passano a 336 già dopo la sintesi, quindi il registro della chiave AEAD (128 bit) è stato rimosso.
- **SHA3-256/512:** high-speed core del Keccak team, v3.1 (1 round per ciclo). **Rate e lunghezza del digest resi parametrici** (r = 1088 / 576; digest di 4 / 8 word da 64 bit); round, stato e controllo restano invariati. **Padding a carico dell'host**, come nell'originale. Verifica in simulazione con GHDL contro `hashlib`: 45/45 digest corretti per ciascuna variante; **24 cicli/blocco** misurati.
- **Ascon v1.2 e Hash256:** stessa permutazione e stesso rate, cambiano IV e ordine dei byte. Il costo hardware è equivalente.

### 3.4 Fronte di Pareto
Area (LUT) contro TP su messaggi lunghi. È rigoroso solo **a parità di famiglia FPGA**; tra famiglie diverse è indicativo.

---

## 4. Ascon-Hash256

### 4.1 Valori pubblicati

| Lavoro | Arch. | FPGA | LUT | FF | Fmax (MHz) | c | TP lunghi (Mbps) | TP 64 B (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|---|
| GMU R2, GMU2-v1h [1] | 1 r/c | Artix-7 | 1375 | 862 | 276 | 13 | 1358.8 | n.d. | include AEAD |
| GMU R2, GMU2-v2h [1] | 2 r/c | Artix-7 | 2126 | 861 | 234 | 7 | 2139.4 | n.d. | include AEAD |
| GMU R2, GMU2-v3h [1] | 3 r/c | Artix-7 | 2493 | 860 | 142 | 5 | 1817.6 | n.d. | include AEAD |
| GMU R2, Graz-v1 [1] | 1 r/c | Artix-7 | 1465 | 666 | 191 | 14 | 873.1 | n.d. | progettisti; include AEAD |
| GMU R2, Graz-v3 [1] | 2 r/c | Artix-7 | 2142 | 665 | 201 | 8 | 1608.0 | n.d. | progettisti; include AEAD |
| GMU R2, Graz-v5 [1] | 3 r/c | Artix-7 | 2797 | 666 | 150 | 6 | 1600.0 | n.d. | progettisti; include AEAD |
| GMU fin., GMU2-x1 [2] | 1 r/c | Artix-7 | 1315 | 863 | 253 | 13* | 1247 | 741 | include AEAD |
| GMU fin., GMU2-x2 [2] | 2 r/c | Artix-7 | 2082 | 864 | 220 | 7* | 2009 | 1160 | include AEAD |
| GMU fin., Graz-x1 [2] | 1 r/c | Artix-7 | 2113 | 582 | 205 | 14* | 937 | 561 | progettisti; include AEAD; ⚠ (a) |
| GMU fin., Graz-x2 [2] | 2 r/c | Artix-7 | 2967 | 588 | 130 | 6* | 1387 | 802 | progettisti; include AEAD |
| Khan et al. [3] | 1 r/c | Virtex-7 | 773 | n.d. | 272.03 | 12* | 1450.8* | n.d. | solo hash; ⚠ (b) |
| Khan et al. [3] | 2 r/c | Virtex-7 | 1290 | n.d. | 224.56 | 6* | 2395.3* | n.d. | ⚠ (b) |
| Khan et al. [3] | 4 r/c | Virtex-7 | 2550 | n.d. | 162.68 | 3* | 3470.5* | n.d. | ⚠ (b) |
| Khan et al. [3] | 6 r/c | Virtex-7 | 3855 | n.d. | 108.45 | 2* | 3470.4* | n.d. | ⚠ (b) |
| RECO-HCON [4] | 1 r/c | Artix-7 | 1548 | 1045 | 244 | 13* | 1201.2* | n.d. | AEAD + Hash + Hasha; ⚠ (c) |
| RECO-ASCON [5] | – | Zynq-7020 | 1083 | 681 | 200 ⚠ | 25 | 512.0* | n.d. | Hash + Hasha; ⚠ (d) |
| Kandi et al. [6] | – | Kintex-7 | 847 | 911 | 192.31 | n.d. | n.d. | n.d. | ⚠ (e) |
| CAST ASCON-F [7] | – | Kintex US+ (-3) | 1211 | n.d. | 900 | n.d. | n.d. | n.d. | datasheet commerciale; include AEAD; ⚠ (f) |

`r/c` = round per ciclo. Note:
- **(a)** Lo stesso design dei progettisti passa da 1465 LUT (Round 2) a 2113 (finalisti). Versione del core e device sono diversi (xc7a12t-3 contro xc7a100t-2).
- **(b)** Il TP pubblicato (697.5 / 1108.2 / 1488.4 / 1391.3 Mbps) è misurato su un messaggio di 256 bit. Il valore `c*` è ricavato dalla formula di latenza degli autori (96/z + 4 cicli). Tool e tipo di dato non dichiarati.
- **(c)** I 3.16 Gbps citati in letteratura sono il valore **ASIC** a 667 MHz. `c` è ricavato dalla formula di latenza del paper; i 244 MHz si riferiscono al core senza FIFO (sistema completo: 2733 LUT a 213 MHz).
- **(d)** 200 MHz è la frequenza di funzionamento sulla PYNQ, non un'Fmax. `c` = 25 è dichiarato (N = 25M + 78).
- **(e)** Il throughput non è dichiarato. La descrizione dell'algoritmo riporta b = 6 round, in contrasto con la specifica (12).
- **(f)** La fonte non è revisionata. Dichiara 5.3 bit/ciclo per Ascon-Hash, esclusi inizializzazione e padding.

### 4.2 Core di riferimento risintetizzati (xck26)

| Core | Vincolo | LUT | FF | BRAM / DSP | Fmax (MHz) | c | TP @100 MHz (Mbps) | TP @Fmax (Mbps) |
|---|---|---|---|---|---|---|---|---|
| ascon-verilog v4, solo hash | 10 ns | 1211 | 336 | 0 / 0 | 216.5 ⚠ (g) | 13 (h) | 492.3* | – |
| ascon-verilog v4, solo hash | 1.4 ns | 1330 | 336 | 0 / 0 | **547.3** | 13 (h) | – | 2694.4* |
| GMU2 Ascon-128 x1 [8] | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| GMU2 Ascon-128 x2 [8] | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

- **(g)** Stima pessimistica: con 10 ns il vincolo è largo e il tool non spinge l'ottimizzazione. Tra 2.0 e 1.4 ns l'Fmax stimata oscilla tra 518.9 e 547.3 MHz, per la variabilità del place and route (circa ±3%).
- **(h)** Ricavato dai cicli pubblicati dai progettisti: 118 cicli per 32 B e 1730 per 1024 B. Latenza Hash256: 66 cicli con messaggio vuoto, 118 per 32 B.
- Percorso critico: logica di controllo (FSM → contatore), non la permutazione.

### 4.3 Nostro acceleratore (stato attuale)

Ottimizzazione con **sole pragma**, stesso livello per le tre varianti; **nessuna ottimizzazione logica**. Vitis HLS 2025.1, xck26, 10 ns, post-route. Input massimo: 256 B.

| Variante | LUT | FF | BRAM / DSP | Fmax (MHz) | c | TP lunghi (Mbps) |
|---|---|---|---|---|---|---|
| Ascon standalone (non è un riferimento) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| **Ibrido, modalità Ascon** | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

### 4.4 Fronte di Pareto (LUT, TP su messaggi lunghi)
- **Artix-7:** GMU2-x1 (1315; 1247) → GMU2-v1h (1375; 1358.8) → GMU2-x2 (2082; 2009) → GMU2-v2h (2126; 2139.4). Le due edizioni GMU sono su device diversi. I core Graz, GMU2-v3h e RECO-HCON sono dominati.
- **Virtex-7:** Khan x1 → x2 → x4. Khan x6 è dominato da x4.
- **Zynq-7020:** RECO-ASCON è l'unico punto della famiglia.
- **xck26:** ⏳ (ascon-verilog v4, GMU2 x1/x2, nostro acceleratore).

---

## 5. SHA3-256 / SHA3-512

### 5.1 Valori pubblicati

| Lavoro | Arch. | FPGA | LUT | FF | Fmax (MHz) | c | TP SHA3-256 (Mbps) | TP SHA3-512 (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|---|
| GMU SHA3-v1 [1] | ripiegato ×8 | Artix-7 | 1263 | 277 | 195 | 233 | 910.6 | – | senza padding |
| Kerckhof et al. [9] | per lane, compatto | Virtex-6 | 519 | 429 | 250 | 2137 | 128 | 67.4* | Fmax con opzione "area"; ⚠ (i) |
| Choudhary et al. [10] | 1 r/c | Kintex-7 | 2970 | 1607 | 313.47 | 25* | 13642.2* | 7222.3* | solo permutazione; ⚠ (j) |
| Choudhary et al. [10] | 1 r/c | Artix-7 | 2970 | 1607 | 244.31 | 25* | 10632.4* | 5628.9* | solo permutazione; ⚠ (j) |
| Crypto-RV [11] | 2 r/c | ZCU102 (US+) | 5329 | 3724 | 160 ⚠ | n.d. | 313.7* (64 B) | – | unità SHA-3/SHAKE in un coprocessore RISC-V; ⚠ (k) |
| Gavrilan et al. [12] | 1 r/c | Artix-7 | 3953 | 1621 | 222.2 | n.d. | n.d. | n.d. | ⏳ originale non disponibile; ⚠ (l) |
| *Unified SHA-3/SHAKE* [13] | 1 r/c | Virtex-7 | 3566 | 1645 | 20 ⚠ | n.d. | n.d. | n.d. | tutte le 6 modalità SHA-3/SHAKE; ⚠ (m) |
| CAST SHA-3 [14] | 1 r/c | Kintex US+ (-1) | 4808 | n.d. | 375 ⚠ | n.d. | n.d. | n.d. | datasheet commerciale; ⚠ (n) |

Note:
- **(i)** Tool ISE 12.1; post-P&R non dichiarato. Il TP SHA3-256 (128 Mbps) è dichiarato; quello SHA3-512 è ricalcolato da noi.
- **(j)** Fa parte di un AEAD Ascon-Keccak. Il TP dichiarato (20062.6 Mbps) usa 1600 bit per permutazione, cioè l'intero stato e non il rate. `c` è ricavato dal TP dichiarato. Senza padding né I/O.
- **(k)** 160 MHz è la frequenza del sistema. Il TP è ricavato dai 4.08 cicli/byte dichiarati su un messaggio di 64 B (261 cicli totali).
- **(l)** Valori citati da [13]. Variante SHA-3 non dichiarata.
- **(m)** 20 MHz è la frequenza di funzionamento; TP FPGA non dichiarato.
- **(n)** Il datasheet avverte che le frequenze non sono le massime raggiungibili; dichiara "fino a 50 Mbit/MHz" con 1 round per ciclo.

### 5.2 Core di riferimento risintetizzati (xck26)

Keccak team high-speed core v3.1, rate parametrico, padding a carico dell'host, `c` = 24 misurato.

| Core | Vincolo | LUT | FF | BRAM / DSP | Fmax (MHz) | TP @100 MHz (Mbps) | TP @Fmax (Mbps) |
|---|---|---|---|---|---|---|---|
| SHA3-256 | 10 ns | 4100 | 2708 | 0 / 0 | 171.5 ⚠ (g) | 4533.3* | – |
| SHA3-256 | 2.2 ns | 4444 | 2708 | 0 / 0 | **401.6** | – | 18205.9* |
| SHA3-512 | 10 ns | 3634 | 2194 | 0 / 0 | 185.1 ⚠ (g) | 2400.0* | – |
| SHA3-512 | 2.0 ns | 4162 | 2246 | 0 / 0 | **395.4** | – | 9489.6* |

- FF = 1600 bit di stato + buffer d'ingresso da r bit + controllo.
- Il timing non chiude da 2.5 ns in giù; l'Fmax è il massimo stimato.
- Percorso critico: un round della permutazione (4 livelli di logica).

### 5.3 Nostro acceleratore (stato attuale)

| Variante | LUT | FF | BRAM / DSP | Fmax (MHz) | c | TP SHA3-256 (Mbps) | TP SHA3-512 (Mbps) |
|---|---|---|---|---|---|---|---|
| SHA-3 standalone (non è un riferimento) | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| **Ibrido, modalità SHA-3** | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

### 5.4 Fronte di Pareto
Sui valori pubblicati non è costruibile: i punti con TP comparabile sono troppo pochi e non omogenei (permutazione sola, frequenze non massime, famiglie diverse). Il riferimento è il core risintetizzato (§5.2). xck26: ⏳.

---

## 6. HLS

| Lavoro | Alg. | FPGA | LUT | FF | Fmax (MHz) | c | TP (Mbps) | Note |
|---|---|---|---|---|---|---|---|---|
| Urbano-Molano & Velasco-Medina [15] | SHA3-512 | Virtex-7 | 6234 | 2597 | 162.31 | 49 | 1908 | Vivado HLS 2020.1; ⚠ (o) |
| AMD Vitis Security Library [16] | SHA3-256 | n.d. ⚠ | 44975 | 36787 | 306.65 ⚠ | 1104 | 302.2* | ⚠ (p) |
| AMD Vitis Security Library [16] | SHA3-512 | n.d. ⚠ | 39794 | 32994 | 316.25 ⚠ | 1096 | 166.2* | ⚠ (p) |
| AMD Vitis Security Library, sulla xck26 | SHA3-256/512 | xck26 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | risintesi prevista |

- **(o)** Il testo e la Tab. III riportano riduzioni di area incoerenti tra loro.
- **(p)** Il device non è dichiarato (il progetto di test è configurato per la vck190, Versal). Non è dichiarato se il periodo di clock sia una stima di sintesi o un valore post-route.
- **Ascon-Hash in HLS:** nessun lavoro trovato.
- In letteratura un core RTL scritto a mano supera la stessa architettura in HLS. Un caso documentato: Keccak su Virtex-6, con throughput/area RTL pari a 1.84 volte quello HLS [17], valore in slice e quindi citato solo qui.

---

## 7. Ibridi e crypto-agili esistenti

| Approccio | Lavori |
|---|---|
| Core separati con multiplexer | Elomda et al. 2026 [18]: AES, Ascon e Keccak con buffer condiviso; Ascon e Keccak validati solo in simulazione |
| Unificazione dentro la stessa famiglia | Crypto-RV [11] (SHA-3/SHAKE; SHA-2/SM3; AES/Haraka); *Unified SHA-3/SHAKE* [13]; RECO-HCON [4] (Ascon AEAD + Hash); RECO-ASCON [5] (Hash/Hasha); ascon-verilog (tutte le modalità SP 800-232) |
| Datapath condiviso tra primitive diverse | Kundi et al. 2020 [19]: AES + SHA-3 |
| Permutazione Keccak dentro la modalità Ascon | Ascon-Keccak AEAD [10][20]: una sola permutazione, non un hash duale |

**Non trovato:** un datapath condiviso **Keccak-f / Ascon-p** per l'hash, con l'algoritmo selezionabile a runtime. L'equivalenza affine tra la S-box di Ascon e il χ di Keccak è dichiarata nella specifica di Ascon [21], ma non risulta sfruttata in hardware.

---

## Appendice A — Incoerenze rilevate

| Fonte | Incoerenza |
|---|---|
| RECO-HCON [4] | Il TP ASIC a 667 MHz viene attribuito all'FPGA (Artix-7) da una survey (arXiv 2304.06222) |
| Choudhary et al. [10] | TP calcolato sull'intero stato (1600 bit), non sul rate |
| Khan et al. [3] | TP misurato su un messaggio fisso di 256 bit |
| Kandi et al. [6] | Ascon-Hash descritto con b = 6 round, contro i 12 della specifica |
| GMU R2 / finalisti [1][2] | Area del core Graz-x1 diversa tra le due edizioni (1465 contro 2113 LUT) |
| Sideris et al. (*Information* 2023) | TP nell'abstract doppio rispetto al testo (escluso: solo slice) |
| Abdulsamad & Répás (*Computers* 2026) | Area solo in percentuale; BRAM incoerenti tra tabelle (escluso) |

## Appendice B — Lavori esclusi (area solo in slice o in percentuale)

Gaj et al., ePrint 2012/368 · Sravani & Durai, *TVLSI* 2022 · Sideris et al., *Information* 2023 e *JCEN* 2024 · Assad et al., *IJECE* 2022 · Wong et al., ISCAS 2018 · El Moumni et al., *Microelectronics J.* 2019 · Rao et al., *IJSSIS* 2019 · Sundal & Chaves, ISVLSI 2017 · Kundi et al., *TCAS-I* 2020 · Jacinto et al., MWSCAS 2017 · Homsirikamol & Gaj, ARC 2015 · Abdulsamad & Répás, *Computers* 2026.

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
