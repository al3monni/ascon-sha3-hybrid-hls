# Posizionamento rispetto allo stato dell'arte

Acceleratori hardware su FPGA (AMD, Intel, Lattice, Microchip) per **Ascon-Hash256** (NIST SP 800-232) e **SHA3-256** (FIPS 202).

> **Stato del documento:** bozza aggiornata al 30/10/2026, allineata a `stato_arte_fpga.xlsx` (stessa cartella), che contiene tutti i valori, le formule e i commenti di cella. Le voci ⏳ sono in attesa di dati (co-simulazioni dedicate, fonti non ancora lette).

**Legenda**
- **ID** identificativo della riga in `stato_arte_fpga.xlsx`.
- `*` valore **ricalcolato** da noi (TP = r · f / c, Fmax = 1000 / T, area da percentuale), non dichiarato dagli autori.
- † dato da usare **con cautela**; il motivo è nelle note sotto la tabella.
- n.d. dato **non dichiarato** dalla fonte.
- ⏳ dato **da aggiornare**.
- `r/c` round per ciclo; *c* cicli per blocco; TP throughput su messaggi lunghi.
- (1), (2), (3) criteri di confronto diretto (§4) non soddisfatti, nelle tabelle dell'Appendice B.

---

## 1. Definizioni

- **AEAD** (Authenticated Encryption with Associated Data): cifratura autenticata, cioè cifratura più un tag di integrità. Molti core Ascon implementano AEAD e hash insieme, e la loro area include logica che un core solo hash non ha.
- **Rate *r*:** bit di messaggio assorbiti per blocco: 64 per Ascon-Hash, 1088 per SHA3-256.
- **Cicli per blocco *c*:** cicli di clock tra l'assorbimento di due blocchi consecutivi dello stesso messaggio, a regime. Per i design multi-messaggio si usa il *c* del messaggio singolo.
- **Throughput su messaggi lunghi (TP):** TP = r · f / c, con *f* = Fmax. È la definizione dei benchmark GMU. Se *f* o *c* non sono dichiarati si usa il TP dichiarato dagli autori.
- **Unità d'area:**
  - **LUT**: LUT a 6 ingressi AMD.
  - **slice (AMD)**: 4 LUT6 e 8 FF su 7-series. Su UltraScale+ il CLB contiene una sola slice da 8 LUT6 e 16 FF.
  - **ALM** (Intel Cyclone V, Arria, Stratix): blocco logico con LUT frazionabile e registri. **ALUT** è la metà di un ALM (Stratix III).
  - **LE** (Intel Cyclone II–IV, Cyclone 10 LP, MAX 10): LUT a 4 ingressi più un registro.
  - **LUT4**: LUT a 4 ingressi (Lattice ECP5, Microchip PolarFire).
- **TP/area:** TP diviso per l'area nell'unità dichiarata, in kbps/unità; sempre calcolato da noi. Confrontabile solo all'interno della stessa famiglia e della stessa unità. I blocchi di memoria (BRAM, M20K) non entrano nell'area: dove un lavoro li usa è indicato nelle note.
- **Latenza:** cicli necessari per elaborare un intero messaggio di lunghezza data.
- **Fmax:** massima frequenza di clock alla quale il design rispetta i vincoli di timing. Se un lavoro dichiara un periodo o un ritardo T, Fmax = 1000 / T. Nelle nostre risintesi è il massimo di 1000 / (T − WNS) su una serie di periodi di clock T, dove WNS è lo slack peggiore del percorso critico.
- **Post-route:** valori ottenuti dopo place and route, cioè i più affidabili.
- **Out-of-context (OOC):** implementazione del core senza assegnare i pin di I/O.

---

## 2. Risultati

Una sezione per vendor e unità d'area, come i fogli di `stato_arte_fpga.xlsx`. Nel testo compaiono solo i punti del fronte di Pareto scelti per il **confronto diretto** (criteri in §4); gli altri punti del fronte sono in Appendice B, i punti dominati e gli esclusi in Appendice C. Per SHA-3 tutti i valori si riferiscono a SHA3-256.

### 2.1 AMD — area in LUT

Confronto diretto: architetture iterative, al più 1 round per ciclo, senza unrolling né pipelining (1); una sola edizione per benchmark ripetuto, cioè GMU Round 2, che dichiara *c* (2).

**Tabella 1 — Punti del fronte di Pareto per il confronto diretto**

| ID | Alg. | Lavoro | Famiglia | Architettura | LUT | FF | Fmax (MHz) | c | TP (Mbps) | TP/LUT (kbps/LUT) |
|---|---|---|---|---|---|---|---|---|---|---|
| A09 | Ascon-Hash | Rezvani et al. [6] | Spartan-6 | 1 r/c, include AEAD | 2188 | n.d. | 158.9 | 15 | 678.0* | 309.9 |
| A01 | Ascon-Hash | RECO-ASCON [4] | Artix-7 | Hash + Hasha | 1083 | 681 | 200 † | 25 | 512.0* | 472.8 |
| A03 | Ascon-Hash | GMU Round 2, GMU2-v1h [1] | Artix-7 | 1 r/c, include AEAD | 1375 | 862 | 276 | 13 | 1358.8* | 988.2 |
| A06 | Ascon-Hash | Khan et al. [3] | Virtex-7 | 1 r/c, solo hash | 773 | n.d. | 272.03 | 12* | 1450.8* † | 1876.9 |
| S01 | SHA3-256 | Kerckhof et al. [7] | Virtex-6 | compatto, per lane | 519 | 429 | 250 | 2137 | 127.3* † | 245.2 |
| S02 | SHA3-256 | GMU Round 2, SHA3-v1 [1] | Artix-7 | ripiegato ×8, senza padding | 1263 | 277 | 195 | 233 | 910.6* | 720.9 |
| S03 | SHA3-256 | Choudhary et al. [8] | Artix-7 | 1 r/c, solo permutazione | 2970 | 1607 | 244.31 | 25* | 10632.4* † | 3579.9 |
| S04 | SHA3-256 | Choudhary et al. [8] | Kintex-7 | 1 r/c, solo permutazione | 2970 | 1607 | 313.47 | 25* | 13642.2* † | 4593.3 |

- Rezvani et al.: *c* = 15 per blocco hash (TP = f · 64 / 15, Tab. 2 degli autori); Ascon v1.2.
- RECO-ASCON è su Zynq-7020, la cui logica programmabile è un Artix-7: per questo sta nel gruppo Artix-7. † 200 MHz è la frequenza di funzionamento sulla PYNQ, non un'Fmax; *c* = 25 è dichiarato (N = 25M + 78).
- GMU2 implementa Ascon-Hash v1.2 (stessa permutazione e stesso rate di Hash256). I core dei progettisti (Graz) sono dominati dai GMU2 su Artix-7; il loro successore, ascon-verilog, è in Tabella 8 come risintesi.
- † Khan et al.: il TP pubblicato (697.5 Mbps) è su un singolo messaggio di 256 bit. *c* è ricavato dalla formula di latenza degli autori (96/z + 4 cicli). Tool e tipo di dato non dichiarati.
- † Kerckhof et al.: tool ISE 12.1; post-route non dichiarato; Fmax con l'opzione di sintesi "area".
- † Choudhary et al.: sola permutazione Keccak-p[1600,24] dentro un AEAD Ascon-Keccak, senza padding né I/O. Il TP dichiarato (20062.6 Mbps su Kintex-7) usa 1600 bit per permutazione invece del rate; *c* è ricavato da quel valore.
- Con così pochi punti SHA-3 in LUT, il riferimento principale per SHA-3 resta il core risintetizzato in Tabella 8; i lavori in slice (§2.2) ampliano il quadro della letteratura.

### 2.2 AMD — area in slice

Nessun lavoro su Ascon-Hash dichiara l'area in slice. Confronto diretto: criteri (1) e (2) come in §2.1 (Sideris et al.: solo *JCEN* 2024); (3) solo famiglie 7-series (Artix-7, Kintex-7, Virtex-7), la generazione più vicina alla xck26.

**Tabella 2 — Punti del fronte di Pareto per il confronto diretto (SHA3-256)**

| ID | Lavoro | Famiglia | Architettura | slice | Fmax (MHz) | c | TP (Mbps) | TP/slice (kbps/slice) |
|---|---|---|---|---|---|---|---|---|
| S96 | Sideris et al. [20] | Artix-7 | 1 r/c | 902 | 397.41 | 24 | 18015.9* | 19973.3 |
| SD3 | Rao et al. [29] | Kintex-7 | 1 r/c (LUT6 con INIT condiviso) | 1185 | 213.17 | 24 | 9663.7* | 8155.0 |
| S94 | Sideris et al. [20] | Virtex-7 | 1 r/c | 1094 | 498.27 | 24 | 22588.2* | 20647.4 |

- FF non dichiarati in nessuno dei lavori in slice.
- Sideris et al. [20]: device e tipo di dato non dichiarati.
- Rao et al. [29]: tool e tipo di dato non dichiarati (potenza riportata post place and route); dichiarato solo SHA3-256.
- TP/slice non è confrontabile con TP/LUT (§2.1) né con TP/LUT della xck26 (§2.6).

### 2.3 Intel

Area nell'unità dichiarata (ALM, ALUT, LE, "slice"), con fronti separati per unità. Confronto diretto:
- criteri (1) e (2) come in §2.1;
- (3) solo famiglie a 28 nm o più recenti (Cyclone V, Stratix V, Arria 10), la stessa generazione della 7-series.

Due esclusioni conseguono dai criteri:
- I lavori di Sideris et al. su Arria 10 non soddisfano (1): usano una pipeline a 2 stadi. Inoltre dichiarano l'area in "slice", unità non definita su Intel.
- La Cyclone 10 LP non soddisfa (3): ha la stessa architettura LE della Cyclone IV.

**Tabella 3 — Punti del fronte di Pareto per il confronto diretto (area in ALM)**

| ID | Alg. | Lavoro | Famiglia | Architettura | ALM | FF | Fmax (MHz) | c | TP (Mbps) | TP/ALM (kbps/ALM) |
|---|---|---|---|---|---|---|---|---|---|---|
| A20 | Ascon-Hash | Rezvani et al. [6] | Cyclone V | 1 r/c, include AEAD | 1064 | n.d. | 210.5 | 15 | 898.1* | 844.1 |
| I03 | SHA3-256 | Honda et al. [11] | Cyclone V | 1 r/c + interfaccia 64 bit | 2679 | n.d. | 60.11 | 41 | 1595.1* | 595.4 |
| I30 | SHA3-256 | Maache & Kalache [30] | Cyclone V | 1 r/c, ingresso 128 bit/ciclo | 3415 | n.d. | 271.69 | n.d. | 11000 † | 3221.1 |
| I07 | SHA3-256 | Honda et al. [11] | Stratix V | 1 r/c + interfaccia 64 bit | 3647 | n.d. | 328.62 | 41 | 8720.5* | 2391.1 |

- Rezvani et al.: stesso RTL della riga A09 (Tabella 1); *c* = 15 per blocco hash.
- Honda et al.: stesso RTL delle righe Xilinx (Appendice B.2); *c* include i cicli di ingresso del messaggio (41 per r = 1088). Padding pre-FIPS 202, stesso costo hardware.
- † Maache & Kalache: 8947 LE (3415 ALM); *c* non dichiarato, usato il TP dichiarato; core Keccak inserito in un sistema AES + Keccak.
- TP/ALM non è confrontabile con TP/LUT AMD né con TP/LE (§4 e Tabella 6).

### 2.4 HLS

Confronto diretto: criteri (1) e (2) come in §2.1; (3) famiglia dichiarata, 7-series / 28 nm o più recente.

**Tabella 4 — Implementazioni HLS per il confronto diretto**

| ID | Alg. | Lavoro | Famiglia | Architettura | LUT | FF | Fmax (MHz) | c | TP (Mbps) | TP/LUT (kbps/LUT) |
|---|---|---|---|---|---|---|---|---|---|---|
| H01 | SHA3-256 | Urbano-Molano & Velasco-Medina [9] | Virtex-7 | HLS (C) | 6234 | 2597 | 162.31 | 49 | 3603.9* † | 578.1 |

- † Urbano-Molano & Velasco-Medina: Vivado HLS 2020.1; riduzioni di area incoerenti tra testo e Tab. III. Gli autori dichiarano solo SHA3-512: *c* di SHA3-256 assunto uguale a quello di SHA3-512 (49 cicli).
- Restano fuori dal confronto diretto (Appendice B.4) Bensalem et al. [31] su Arria 10 (architettura e *c* non dichiarati) e la AMD Vitis Security Library [10] (device non dichiarato). ⏳ Per la libreria AMD è prevista la risintesi sulla xck26.
- Nessuna implementazione HLS di Ascon-Hash trovata.
- Un core RTL scritto a mano supera la stessa architettura in HLS: per Keccak su Virtex-6, throughput/area RTL/HLS = 1.84 [45].

### 2.5 Altri vendor (Lattice, Microchip)

Area in LUT a 4 ingressi, non confrontabile con i LUT6 AMD né con gli ALM/LE Intel: dati di contesto. Confronto diretto: criteri (1) e (2) come in §2.1; nessun criterio sulla famiglia, perché solo la Lattice ECP5 ha dati completi.

**Tabella 5 — Lattice ECP5, punti del fronte di Pareto per il confronto diretto (GMU Round 2 [1])**

| ID | Alg. | Design | Architettura | LUT4 | FF | Fmax (MHz) | c | TP (Mbps) | TP/LUT4 (kbps/LUT4) |
|---|---|---|---|---|---|---|---|---|---|
| L13 | Ascon-Hash | Graz-v1 | 1 r/c, include AEAD Ascon-128 | 2544 | 676 | 59.3 | 14 | 271.1* | 106.6 |
| L14 | Ascon-Hash | Graz-v2 | 1 r/c, include AEAD Ascon-128a | 2603 | 674 | 64.0 | 14 | 292.6* | 112.4 |
| L10 | Ascon-Hash | GMU2-v1h | 1 r/c, include AEAD | 2928 | 864 | 110.1 | 13 | 542.0* | 185.1 |
| L20 | SHA3-256 | SHA3-v1 | ripiegato ×8, senza padding | 1804 | 249 | 90.3 | 233 | 421.7* | 233.7 |

**Tabella 6 — Stesso RTL su tre famiglie di tre vendor (GMU Round 2 [1], stesso flusso di benchmark)**

| Design | Alg. | LUT Artix-7 | LE Cyclone 10 LP | LUT4 ECP5 | LE / LUT | LUT4 / LUT | TP Artix-7 (Mbps) | TP Cyclone 10 LP (Mbps) | TP ECP5 (Mbps) | TP Artix-7 / Cyclone 10 LP | TP Artix-7 / ECP5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| GMU2-v1h | Ascon-Hash | 1375 | 2415 | 2928 | 1.76 | 2.13 | 1358.8* | 864.5* | 542.0* | 1.57 | 2.51 |
| GMU2-v2h | Ascon-Hash | 2126 | 3215 | 3764 | 1.51 | 1.77 | 2139.4* | 1232.5* | 815.5* | 1.74 | 2.62 |
| SHA3-v1 | SHA3-256 | 1263 | 5417 | 1804 | 4.29 | 1.43 | 910.6* | 394.6* | 421.7* | 2.31 | 2.16 |

- *c* è lo stesso sulle tre famiglie: il rapporto dei TP coincide con il rapporto delle Fmax.
- Medie GMU su tutti i design del Round 2 (sez. 5.1 di [1]):

  | Rapporto | Valore |
  |---|---|
  | LE Cyclone 10 LP / LUT Artix-7 | 1.88 |
  | LUT ECP5 / LUT Artix-7 | 1.77 |
  | Fmax Artix-7 / Cyclone 10 LP | 1.72 |
  | Fmax Artix-7 / ECP5 | 2.59 |
- SHA3-v1 su Cyclone 10 LP usa 4.29× i LE e 12.43× i FF rispetto ad Artix-7. Nostra interpretazione: su Artix-7 lo stato da 1600 bit sta in RAM distribuita, mentre la Cyclone 10 LP non ne dispone e GMU esclude i blocchi M9K, quindi lo stato finisce in FF.
- I rapporti d'area variano da 1.43 a 4.29 a seconda del design: non esiste un fattore di conversione unico tra vendor (§4).

**Tabella 7 — IP commerciali su Lattice e Microchip (datasheet del vendor, solo area)**

| ID | Alg. | IP | Famiglia | Configurazione | LUT4 |
|---|---|---|---|---|---|
| C20 | SHA-3 | Xiphera XIP3030C, compatto [38] | Lattice ECP5 | SHA-3 + SHAKE + cSHAKE | 1278 |
| C21 | SHA-3 | Xiphera XIP3030C, compatto [38] | Lattice CertusPro-NX | SHA-3 + SHAKE + cSHAKE | 1245 |
| C22 | SHA-3 | Xiphera XIP3030C, compatto [38] | Microchip PolarFire | SHA-3 + SHAKE + cSHAKE | 1853 |
| C23 | Ascon-Hash | Xiphera XIP2201B, bilanciato [39] | Lattice (n.d.) | AEAD + Hash + XOF | 4665 |

- Fmax e TP non dichiarati nelle pagine del vendor (product brief non letto).
- XIP2201B: device indicato solo come FPGA Lattice tipica; il vendor dichiara oltre 803.77 Mbps senza specificare la modalità; supporta Ascon v1.2.

### 2.6 AMD UltraScale+ — xck26 (KV260): risintesi e nostro acceleratore

Risintesi dei core di riferimento (Vivado 2025.1.1, OOC, post-route, prestazione di picco) e nostro acceleratore [35] (Vitis HLS 2025.1, vincolo 10 ns, post-route). Tabella non filtrata.

**Tabella 8 — Risultati sulla xck26-sfvc784-2LV-c**

| ID | Alg. | Core | Flusso, vincolo | LUT | FF | CLB | RAMB18 | Fmax (MHz) | c | TP @Fmax (Mbps) | TP/LUT (kbps/LUT) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| K01 | Ascon-Hash | ascon-verilog v4, solo hash [32] | RTL, 1.4 ns | 1330 | 336 | 192 | 0 | 547.3 | 13 | 2694.4* | 2025.9 |
| K02 | Ascon-Hash | GMU2 Ascon-128 x1 [33] | RTL, 1.8 ns | 1327 | 839 | 203 | 0 | 535.3 † | 13 | 2635.3* | 1985.9 |
| K03 | SHA3-256 | Keccak team high-speed v3.1 [34] | RTL, 2.2 ns | 4444 | 2708 | 655 | 0 | 401.6 | 24 | 18205.9* | 4096.7 |
| K05 | Ascon-Hash | Nostro: Ascon standalone (non è un riferimento) | HLS, 10 ns | 1200 | 744 | 210 | 4 | 211.8* | ⏳ | ⏳ | ⏳ |
| K06 | SHA3-256 | Nostro: SHA-3 standalone (non è un riferimento) | HLS, 10 ns | 2214 | 1551 | 474 | 10 | 166.0* | ⏳ | ⏳ | ⏳ |
| K07 | Ascon-Hash + SHA3-256 | **Nostro: ibrido** (area condivisa) | HLS, 10 ns | **3281** | **2215** | **667** | **10** | **162.1*** | ⏳ | ⏳ | ⏳ |

Core di riferimento
- ascon-verilog: tra 2.0 e 1.4 ns l'Fmax stimata oscilla tra 518.9 e 547.3 MHz (variabilità del place and route). Percorso critico nella logica di controllo (FSM → contatore), non nella permutazione. *c* = 13 è ricavato dai cicli pubblicati dal progettista (118 per 32 B, 1730 per 1024 B).
- † GMU2 x1: ricerca fermata a 1.8 ns, con slack appena negativo (−0.068 ns). Valore vicino al limite, non verificato oltre. Include l'AEAD e implementa Ascon-Hash v1.2; *c* = 13 è dichiarato nel benchmark GMU Round 2.
- Keccak team: 1 round per ciclo, rate parametrico, padding a carico dell'host; *c* = 24 misurato in simulazione. Il timing non chiude da 2.5 ns in giù: l'Fmax è il miglior valore stimato tra le passate. Percorso critico: un round della permutazione (4 livelli di logica). FF = 1600 bit di stato + buffer d'ingresso da *r* bit + controllo.

Nostro acceleratore
- Stessa ottimizzazione con sole pragma per le tre varianti, nessuna ottimizzazione logica. Input massimo 256 B.
- Fmax* = 1000 / periodo post-route riportato da Vitis: 4.722 ns (Ascon), 6.023 ns (SHA-3), 6.169 ns (ibrido). Con vincolo a 10 ns non è un'Fmax cercata. ⏳ Previsti lo sweep del clock target HLS e la ricerca dell'Fmax in Vivado.
- Le RAMB18 contengono la RAM di stato: la `union` impedisce di mapparla su registri (backlog B-01). I core di riferimento non usano BRAM. DSP: 0 in tutti i casi. La memoria non entra in TP/LUT.
- Latenza di co-simulazione:

  | Variante | Messaggio | Cicli |
  |---|---|---|
  | Ascon standalone | 0 B | 183 |
  | Ascon standalone | 23 B | 338 |
  | SHA-3 standalone | SHA3-256, 32 B | 3449 |

  Per SHA-3 sono migliaia di cicli per messaggio, contro i 24 cicli per blocco del riferimento: coerente con il collo di bottiglia della RAM di stato a 2 porte (B-01).
- Il report di co-simulazione dell'ibrido aggrega min, media e max su 4 messaggi. La latenza per modalità richiede co-simulazioni dedicate ⏳.
- Date di implementazione:
  - Ascon e SHA-3: re-implementati il 23/10/2026; riproducono i valori di giugno.
  - Ibrido: 25/06/2026. Da giugno il codice è cambiato solo nei commenti (LOGBOOK, LOG-006).

### 2.7 IP commerciali su AMD e Intel

Dati di datasheet, non peer-reviewed, non filtrati. TP calcolato solo dove il vendor dichiara i bit per ciclo. Gli IP su Lattice e Microchip sono in Tabella 7.

**Tabella 9 — IP commerciali (CAST Inc.)**

| ID | Alg. | IP | Famiglia | Configurazione | Area | Unità | f (MHz) | bit/ciclo | TP (Mbps) | TP/area (kbps/unità) |
|---|---|---|---|---|---|---|---|---|---|---|
| C01 | SHA-3 | CAST SHA-3 [36] | Spartan-7 | 64 bit, 1 round/ciclo, 1 buffer | 4767 | LUT | 125 | n.d. | n.d. | n.d. |
| C02 | SHA-3 | CAST SHA-3 [36] | Artix-7 | 64 bit, 1 round/ciclo, 1 buffer | 4772 | LUT | 150 | n.d. | n.d. | n.d. |
| C03 | SHA-3 | CAST SHA-3 [36] | UltraScale+ | 64 bit, 1 round/ciclo, 1 buffer | 4808 | LUT | 375 | n.d. | n.d. | n.d. |
| C04 | SHA-3 | CAST SHA-3 [36] | UltraScale+ | 128 bit, 2 round/ciclo, 2 buffer | 10340 | LUT | 200 | n.d. | n.d. | n.d. |
| C05 | SHA-3 | CAST SHA-3 [36] | UltraScale+ | 512 bit, 4 round/ciclo, 2 buffer | 20672 | LUT | 100 | n.d. | n.d. | n.d. |
| C06 | SHA-3 | CAST SHA-3 [36] | Cyclone V | 32 bit, 1 round/ciclo, 0 buffer | 3291 | ALM | 100 | n.d. | n.d. | n.d. |
| C07 | SHA-3 | CAST SHA-3 [36] | Arria 10 | 64 bit, 1 round/ciclo, 1 buffer | 3852 | ALM | 300 | n.d. | n.d. | n.d. |
| C08 | SHA-3 | CAST SHA-3 [36] | Agilex | 64 bit, 1 round/ciclo, 1 buffer | 4087 | ALM | 500 | n.d. | n.d. | n.d. |
| C09 | SHA-3 | CAST SHA-3 [36] | Agilex | 128 bit, 2 round/ciclo, 2 buffer | 6537 | ALM | 250 | n.d. | n.d. | n.d. |
| C10 | SHA-3 | CAST SHA-3 [36] | Agilex | 512 bit, 4 round/ciclo, 2 buffer | 14228 | ALM | 125 | n.d. | n.d. | n.d. |
| C11 | Ascon-Hash | CAST ASCON-F [37] | Kintex-7 | AEAD + Hash | 1202 | LUT | 475 | 5.3 | 2517.5* | 2094.4 |
| C12 | Ascon-Hash | CAST ASCON-F [37] | UltraScale | AEAD + Hash | 1223 | LUT | 700 | 5.3 | 3710.0* | 3033.5 |
| C13 | Ascon-Hash | CAST ASCON-F [37] | UltraScale+ | AEAD + Hash | 1211 | LUT | 900 | 5.3 | 4770.0* | 3938.9 |
| C14 | Ascon-Hash | CAST ASCON-F [37] | Versal | AEAD + Hash | 1481 | LUT | 625 | 5.3 | 3312.5* | 2236.7 |
| C15 | Ascon-Hash | CAST ASCON-F [37] | Agilex | AEAD + Hash | 1295 | ALM | 700 | 5.3 | 3710.0* | 2864.9 |
| C16 | Ascon-Hash | CAST ASCON-F [37] | Arria 10 | AEAD + Hash | 1225 | ALM | 525 | 5.3 | 2782.5* | 2271.4 |
| C17 | Ascon-Hash | CAST ASCON-F [37] | Cyclone V | AEAD + Hash | 1207 | ALM | 250 | 5.3 | 1325.0* | 1097.8 |
| C18 | Ascon-Hash | CAST ASCON-F [37] | MAX 10 | AEAD + Hash | 2058 | LE | 175 | 5.3 | 927.5* | 450.7 |
| C19 | Ascon-Hash | CAST ASCON-F [37] | Stratix V | AEAD + Hash | 1173 | ALM | 500 | 5.3 | 2650.0* | 2259.2 |

- *f* è la frequenza dichiarata dal vendor, non un'Fmax: per entrambi gli IP le frequenze non sono le massime ottenibili.
- CAST SHA-3: il datasheet dichiara fino a 50 Mbit/MHz con una permutazione per ciclo, ma non i cicli per blocco: TP non calcolabile.
- CAST ASCON-F: TP* = 5.3 bit/ciclo × f, esclusi padding e inizializzazione.
- Device e speed grade di ogni riga sono nei commenti di cella dell'`.xlsx`.

---

## 3. Cosa offre la letteratura

La letteratura abbonda di implementazioni di SHA-3 su famiglie Virtex-5/6, con l'area espressa solo in slice. Per Ascon prevalgono le implementazioni AEAD, e i dati più solidi vengono dai benchmark delle competizioni (SHA-3, CAESAR, NIST LWC).

Fuori da AMD il quadro è più povero. Su Intel i dati sono numerosi ma per lo più su famiglie anteriori ai 28 nm (Cyclone II–IV, Stratix II–IV): i punti di confronto diretto sono quattro, su Cyclone V e Stratix V. Su Arria 10 l'unico gruppo di lavori (Sideris et al.) usa pipeline e un'unità d'area non definita. Su Lattice e Microchip l'unica fonte con dati completi è il benchmark GMU Round 2 su ECP5 [1]; gli IP commerciali dichiarano solo l'area. Lo stesso RTL misurato su tre famiglie (Tabella 6) mostra rapporti d'area e di TP che cambiano da design a design.

Scarseggiano invece i lavori su SHA-3 con area in LUT su famiglie 7-series e UltraScale+, i core dedicati al solo Ascon-Hash (quasi sempre integrato in un core AEAD), i lavori su Ascon-Hash256 secondo SP 800-232, standardizzato solo nell'agosto 2025, e le implementazioni HLS curate. Mancano del tutto un datapath condiviso Keccak-f / Ascon-p per l'hash e un'implementazione HLS di Ascon-Hash.

Su più punti la letteratura è poco convincente. Il throughput è definito in modi diversi, e in diversi lavori il TP dichiarato non corrisponde a r · f / c; spesso non è dichiarato se i valori siano di sintesi o post-route, e in alcuni lavori la frequenza di funzionamento viene presentata come se fosse un'Fmax (Crypto-RV, RECO-ASCON, *Unified SHA-3/SHAKE*). Alcuni lavori su FPGA Intel riportano l'area in "slice", unità non definita per quel vendor. Alcuni lavori, infine, sono incoerenti tra abstract, testo e tabelle, e una survey attribuisce all'FPGA dati ASIC (RECO-HCON) (Appendice A).

## 4. Metodologia

**Area.** L'area è riportata nell'unità nativa dichiarata dagli autori: LUT o slice per AMD, ALM, ALUT o LE per Intel, LUT4 per Lattice e Microchip.
- Non si fanno conversioni tra unità né tra famiglie FPGA. Questo vale anche tra slice e LUT, per cui non si applica la conversione proposta in [40], eq. (10).
- Il numero di slice dipende da come il tool impacchetta LUT e FF. Il rapporto tra unità di vendor diversi dipende dal design: per lo stesso RTL va da 1.43 a 4.29 (Tabella 6).
- I blocchi di memoria non entrano nell'area e sono indicati nelle note.
- Si preferiscono i valori post-route, indicando il tipo di dato quando è dichiarato.
- I lavori con dati discordanti restano inclusi ma sono marcati †.

**SHA-3.** Il confronto usa solo SHA3-256. SHA3-256 e SHA3-512 usano la stessa permutazione: con lo stesso *c*, TP SHA3-512 = TP SHA3-256 × 576 / 1088. Nei dati raccolti *c* è uguale per le due varianti in tutti i lavori tranne Honda et al. [11]: 41 cicli per SHA3-256 e 33 per SHA3-512, perché il blocco è caricato su 64 bit. I lavori che dichiarano solo SHA3-512 sono esclusi (Appendice C), salvo Urbano-Molano & Velasco-Medina [9] (Tabella 4).

**Fronte di Pareto.** Tra i valori pubblicati restano solo i punti non dominati all'interno del proprio gruppo.
- Un gruppo riunisce i punti con stesso metodo (RTL o HLS), stesso algoritmo, stessa famiglia FPGA e stessa unità d'area. Zynq-7000 conta come Artix-7 (stessa logica programmabile).
- Un punto è dominato se un altro punto del gruppo ha area minore o uguale e TP maggiore o uguale, con almeno una delle due differenze stretta.
- I lavori senza TP su messaggi lunghi non sono collocabili sul fronte e sono esclusi (Appendice C).
- Il filtro non si applica alle risintesi sulla xck26, al nostro acceleratore e agli IP commerciali, riportati per intero.

**Confronto diretto.** Tra i punti del fronte, il testo riporta solo quelli che soddisfano i criteri seguenti. Gli altri punti del fronte sono in Appendice B.
1. **Stessa classe del nostro core:** architetture iterative, al più 1 round per ciclo, senza unrolling né pipelining.
2. **Una sola edizione per benchmark ripetuto:** GMU Round 2, che dichiara *c*; Sideris et al. *JCEN* 2024.
3. **Famiglia vicina alla xck26.** Il criterio non si applica ai LUT AMD; per gli altri gruppi:
   - slice AMD: solo 7-series;
   - Intel: solo 28 nm o più recenti (Cyclone V, Stratix V, Arria 10);
   - HLS: famiglia dichiarata, 7-series / 28 nm o più recente.

**Riferimenti standard.** Come riferimenti standard usiamo i benchmark del Cryptographic Engineering Research Group della George Mason University (GMU) e i core dei progettisti.
- I benchmark GMU valutano in modo uniforme i candidati delle competizioni NIST: stessa FPGA e stesso tool per tutti, frequenza ottimizzata automaticamente (strumenti Minerva e Xeda), valori post-route e TP su messaggi lunghi.
- Per Ascon usiamo il Round 2 [1] e i finalisti [2]. Il Round 2 riporta lo stesso RTL su Artix-7, Cyclone 10 LP ed ECP5 (Appendice A di [1]).
- Il benchmark SHA-3 (ePrint 2012/368) è in attesa di lettura (Appendice D).
- I core dei progettisti sono implementazioni di riferimento pubbliche, conformi alla specifica e della stessa classe architetturale del nostro acceleratore (iterativa, 1 round per ciclo). Non sono necessariamente i più efficienti, ma sono verificabili.

**Risintesi sulla xck26.** Tutti i riferimenti disponibili come sorgente sono implementati con Vivado 2025.1.1 sul part `xck26-sfvc784-2LV-c` (KV260), out-of-context, con valori post-route.
- Periodi di clock da 10 a 2.0 ns, per Ascon fino a 1.4 ns. In nessuna passata ci sono violazioni di hold o di larghezza d'impulso del clock.
- **Ascon-Hash256:** ascon-verilog (variante v4, bus a 64 bit, 1 round per ciclo) del progettista R. Primas [32]. Il core è ridotto al solo hash fissando la modalità a `M_HASH256` e collegando a costanti gli ingressi AEAD; permutazione e controllo restano invariati. I FF scendono a 336 già dopo la sintesi, a conferma che il registro della chiave AEAD (128 bit) viene rimosso.
- **SHA-3:** high-speed core del Keccak team (v3.1, 1 round per ciclo) [34], con rate e lunghezza del digest resi parametrici e round, stato e controllo invariati. Verificato in simulazione con GHDL contro `hashlib`, dà 45/45 digest corretti per ciascuna variante.
- **GMU2:** sintetizzato dal Verilog generato pubblicato in BlueLight [33], senza modifiche.

## 5. Ibridi e crypto-agili esistenti

Le soluzioni crypto-agili trovate seguono quattro strade. La più semplice affianca core separati selezionati da un multiplexer: Elomda et al. [46] combinano AES, Ascon e Keccak con un buffer condiviso, ma validano Ascon e Keccak solo in simulazione. Più comune è l'unificazione dentro la stessa famiglia di algoritmi: Crypto-RV [42] unifica SHA-3/SHAKE, SHA-2/SM3 e AES/Haraka, *Unified SHA-3/SHAKE* [44] copre tutte le modalità Keccak, RECO-HCON [5] unisce AEAD e hash di Ascon, RECO-ASCON [4] supporta Hash e Hasha, e ascon-verilog [32] tutte le modalità di SP 800-232. Kundi et al. [28] condividono invece il datapath tra primitive diverse, AES e SHA-3; su Virtex-7 il loro core unificato (1250 slice più 16 BRAM per le tabelle AES) è dominato da un core SHA-3 dedicato della stessa famiglia (S94, 1094 slice; Appendice C). Infine, Ascon-Keccak AEAD [8][47] inserisce la permutazione Keccak dentro la modalità Ascon: usa una sola permutazione e non è un hash duale.

Non è stato trovato un datapath condiviso Keccak-f / Ascon-p per l'hash, con l'algoritmo selezionabile a runtime. L'equivalenza affine tra la S-box di Ascon e il χ di Keccak è dichiarata nella specifica di Ascon [48], ma non risulta sfruttata in hardware.

---

## Appendice A — Incoerenze rilevate

| Fonte | Incoerenza |
|---|---|
| RECO-HCON [5] | Il TP ASIC a 667 MHz (3.16 Gbps) viene attribuito all'FPGA da una survey (arXiv 2304.06222) |
| Choudhary et al. [8] | TP calcolato sull'intero stato (1600 bit), non sul rate |
| Khan et al. [3] | TP misurato su un messaggio fisso di 256 bit |
| Kandi et al. [41] | Ascon-Hash descritto con b = 6 round, contro i 12 della specifica |
| Urbano-Molano & Velasco-Medina [9] | Riduzioni di area incoerenti tra testo e Tab. III |
| Sideris et al. [21] (*Information* 2023) | TP nell'abstract doppio rispetto al testo |
| Abdulsamad & Répás (*Computers* 2026) | Numero di BRAM incoerente tra le tabelle |
| Kaps et al. [18] | r · f / c con il *c* di Tab. 1 (3696 / 2328 cicli) non riproduce il TP dichiarato |
| Gholipour & Mirzakuchaki [12] | r · f / c con r = 1024 dà 4767 Mbps, non i 4670 dichiarati |
| Baldwin et al. [16] | TP per variante in Tab. IV non corrispondenti a r · f / c (8518 Mbps attribuito a Keccak-512) |
| Sundal & Chaves [15] | TP di Tab. I (7.80 Gbps) non corrispondente a r · f / c |
| Sideris et al. [22] (ICM 2020) | Contatore fino a 48 cicli (pipeline a 2 stadi), ma TP dichiarato con 24 |
| Sideris et al. [23] (*JENRS* 2022) | 18 e 14 cicli per 24 round non spiegati; r · f / c = 26112 Mbps contro 25507 dichiarati; il testo indica un aumento dei cicli del 12.85 %, ma passano da 18 a 14 |
| Sideris et al. [22][23][24] | Area in "slice" su FPGA Intel, unità non definita per quel vendor |
| Khan, Lee, Hwang (TRONSHOW 2022) | LUT dichiarati oltre la capacità dei device |

## Appendice B — Punti del fronte non usati per il confronto diretto

Punti non dominati nel proprio gruppo che non soddisfano almeno un criterio di §4. Ultima colonna: criteri non soddisfatti.

**B.1 — AMD, area in LUT**

| ID | Alg. | Lavoro | Famiglia | Architettura | LUT | Fmax (MHz) | c | TP (Mbps) | TP/LUT | Crit. |
|---|---|---|---|---|---|---|---|---|---|---|
| A02 | Ascon-Hash | GMU finalisti, GMU2-x1 [2] | Artix-7 | 1 r/c, include AEAD | 1315 | 253 | n.d. | 1247 | 948.3 | (2) |
| A04 | Ascon-Hash | GMU finalisti, GMU2-x2 [2] | Artix-7 | 2 r/c, include AEAD | 2082 | 220 | n.d. | 2009 | 964.9 | (1)(2) |
| A05 | Ascon-Hash | GMU Round 2, GMU2-v2h [1] | Artix-7 | 2 r/c, include AEAD | 2126 | 234 | 7 | 2139.4* | 1006.3 | (1) |
| A07 | Ascon-Hash | Khan et al. [3] | Virtex-7 | 2 r/c, solo hash | 1290 | 224.56 | 6* | 2395.3* † | 1856.8 | (1) |
| A08 | Ascon-Hash | Khan et al. [3] | Virtex-7 | 4 r/c, solo hash | 2550 | 162.68 | 3* | 3470.5* † | 1361.0 | (1) |

- GMU finalisti: *c* non dichiarato; da TP e Fmax si ricavano 13 (x1) e 7 (x2). Il benchmark usa un device diverso dal Round 2: xc7a100t-2, contro xc7a12t-3.
- † Khan et al.: TP pubblicato su un singolo messaggio di 256 bit (1108.2 / 1488.4 Mbps); vedi Tabella 1.

**B.2 — AMD, area in slice (SHA3-256)**

| ID | Lavoro | Famiglia | Architettura | slice | Fmax (MHz) | c | TP (Mbps) | TP/slice | Crit. |
|---|---|---|---|---|---|---|---|---|---|
| S71 | Kaps et al. [18] | Spartan-3 | compatto, BRAM | 627 | 112.4* | n.d. | 32.5 † | 51.8 | (3) |
| S70 | Kaps et al. [18] | Spartan-3 | compatto, solo logica | 766 | 101.7* | n.d. | 46.2 † | 60.3 | (3) |
| S10 | Honda et al. [11] | Spartan-3 | 1 r/c + interfaccia 64 bit | 3632 | 95.45 | 41 | 2532.9* | 697.4 | (3) |
| S30 | Ioannou et al. [13] | Virtex-4 | 1 r/c, senza pipeline | 2365 | 273 | 24 | 12376.0* | 5233.0 | (3) |
| S75 | Kaps et al. [18] | Virtex-5 | compatto, BRAM | 159 | 247.5* | n.d. | 71.6 † | 450.3 | (3) |
| S74 | Kaps et al. [18] | Virtex-5 | compatto, solo logica | 275 | 259.7* | n.d. | 118.1 † | 429.5 | (3) |
| S42 | Sundal & Chaves [15] | Virtex-5 | ripiegato FF = 4 | 476 | 200 | 96 | 2266.7* | 4761.9 | (3) |
| S90 | Sideris et al. [20] | Virtex-5 | 1 r/c | 868 | 347.49 | 24 | 15752.9* | 18148.5 | (3) |
| S91 | Sideris et al. [20] | Virtex-5 | 2 r/c | 1112 | 203.28 | 12 | 18430.7* | 16574.4 | (1)(3) |
| SA0 | Sideris et al. [21] | Virtex-5 | unrolling + pipelining | 1186 | 272.41 | n.d. | 24699 † | 20825.5 | (1)(2)(3) |
| S72 | Kaps et al. [18] | Spartan-6 | compatto, solo logica | 113 | 202.0* | n.d. | 91.8 † | 812.4 | (3) |
| SD2 | Rao et al. [29] | Spartan-6 | 1 r/c (LUT6 con INIT condiviso) | 1162 | 111.73 | 24 | 5065.1* | 4358.9 | (3) |
| S76 | Kaps et al. [18] | Virtex-6 | compatto, solo logica | 106 | 299.4* | n.d. | 136 † | 1283.0 | (3) |
| S92 | Sideris et al. [20] | Virtex-6 | 1 r/c | 946 | 438.49 | 24 | 19878.2* | 21012.9 | (3) |
| S93 | Sideris et al. [20] | Virtex-6 | 2 r/c | 1287 | 347.84 | 12 | 31537.5* | 24504.7 | (1)(3) |
| S97 | Sideris et al. [20] | Artix-7 | 2 r/c | 1184 | 254.46 | 12 | 23071.0* | 19485.7 | (1) |
| S95 | Sideris et al. [20] | Virtex-7 | 2 r/c | 1375 | 378.73 | 12 | 34338.2* | 24973.2 | (1) |
| SA2 | Sideris et al. [21] | Virtex-7 | unrolling + pipelining | 1452 | 396.28 | n.d. | 35929 † | 24744.5 | (1)(2) |

- † Kaps et al.:
  - Fmax è ricavata dal ritardo massimo dichiarato.
  - r · f / c con il *c* di Tab. 1 non riproduce il TP dichiarato, che è quello usato.
  - Gli autori riportano solo Keccak-256.
  - Memoria esclusa dall'area: 1 BRAM nelle varianti "BRAM".
- Honda et al.: *c* include i cicli di ingresso del messaggio.
- † Sideris et al. [21]: cicli per blocco non dichiarati (circa 12 ricavati dal TP); usato il TP dichiarato.

**B.3 — Intel**

| ID | Alg. | Lavoro | Famiglia | Architettura | Area | Unità | Fmax (MHz) | c | TP (Mbps) | TP/area | Crit. |
|---|---|---|---|---|---|---|---|---|---|---|---|
| G10 | Ascon-Hash | GMU Round 2, GMU2-v1h [1] | Cyclone 10 LP | 1 r/c, include AEAD | 2415 | LE | 175.6 | 13 | 864.5* | 358.0 | (3) |
| G11 | Ascon-Hash | GMU Round 2, GMU2-v2h [1] | Cyclone 10 LP | 2 r/c, include AEAD | 3215 | LE | 134.8 | 7 | 1232.5* | 383.3 | (1)(3) |
| I20 | SHA3-256 | Kaps et al. [18] | Cyclone II | compatto, BRAM | 996 | LE | 182.5* | n.d. | 52.7 † | 52.9 | (3) |
| I21 | SHA3-256 | Kaps et al. [18] | Cyclone II | compatto, solo logica | 6247 | LE | 117.8* | n.d. | 53.5 | 8.6 | (3) |
| I01 | SHA3-256 | Honda et al. [11] | Cyclone III | 1 r/c + interfaccia 64 bit | 5636 | LE | 53.23 | 41 | 1412.5* | 250.6 | (3) |
| I12 | SHA3-256 | Gholipour & Mirzakuchaki [12] | Cyclone III | 1 r/c | 5761 | LE | 137.51 | 24 | 6233.8* † | 1082.1 | (3) |
| I02 | SHA3-256 | Honda et al. [11] | Cyclone IV | 1 r/c + interfaccia 64 bit | 5636 | LE | 51.76 | 41 | 1373.5* | 243.7 | (3) |
| G19 | SHA3-256 | GMU Round 2, SHA3-v1 [1] | Cyclone 10 LP | ripiegato ×8, senza padding | 5417 | LE | 84.5 | 233 | 394.6* | 72.8 | (3) |
| I00 | SHA3-256 | Honda et al. [11] | Arria II | 1 r/c + interfaccia 64 bit | 2980 | ALM | 211.24 | 41 | 5605.6* | 1881.1 | (3) |
| I40 | SHA3-256 | Sideris et al. [22] | Arria 10 | pipeline a 2 stadi | 1422 | slice (n.d.) | 498.62 | 48 | 11302.1* † | 7948.0 | (1) |
| I41 | SHA3-256 | Sideris et al. [23] | Arria 10 | pipeline a 2 stadi | 2682 | slice (n.d.) | 432 | 18 | 26112.0* † | 9736.0 | (1) |
| I42 | SHA3-256 | Sideris et al. [23] | Arria 10 | pipeline a 2 stadi, con FPH-2 | 2764 | slice (n.d.) | 472 | 14 | 36681.1* † | 13271.0 | (1) |
| I04 | SHA3-256 | Honda et al. [11] | Stratix II | 1 r/c + interfaccia 64 bit | 3732 | ALM | 86.96 | 41 | 2307.6* | 618.3 | (3) |
| I05 | SHA3-256 | Honda et al. [11] | Stratix III | 1 r/c + interfaccia 64 bit | 3738 | ALM | 261.85 | 41 | 6948.6* | 1858.9 | (3) |
| I11 | SHA3-256 | Gholipour & Mirzakuchaki [12] | Stratix III | 1 r/c | 4304 | ALUT | 230.57 | 24 | 10452.5* † | 2428.6 | (3) |
| I10 | SHA3-256 | Gholipour & Mirzakuchaki [12] | Stratix III | 3 round in 2 cicli, srotolato | 14402 | ALUT | 212.49 | 16 | 14449.3* † | 1003.3 | (1)(3) |
| I06 | SHA3-256 | Honda et al. [11] | Stratix IV | 1 r/c + interfaccia 64 bit | 3675 | ALM | 278.47 | 41 | 7389.6* | 2010.8 | (3) |

- La Cyclone 10 LP non soddisfa (3): stessa architettura LE della Cyclone IV.
- † Kaps et al. (I20): la Cyclone II non ha RAM distribuita. Memoria esclusa dall'area: 8192 bit.
- † Gholipour & Mirzakuchaki: Keccak pre-FIPS 202 con r = 1024, TP ricalcolato con r = 1088. La versione srotolata non entra nel device Cyclone III.
- † Sideris et al. su Arria 10:
  - Area in "slice", unità non definita su Intel; sintesi Quartus 18.x.
  - [22]: per messaggio singolo *c* = 48, mentre il TP dichiarato usa 24.
  - [23]: 18 e 14 cicli per 24 round non spiegati (Appendice A).

**B.4 — HLS**

| ID | Alg. | Lavoro | Famiglia | Architettura | Area | Unità | FF | Fmax (MHz) | c | TP (Mbps) | TP/area | Crit. |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| H04 | SHA3-256 | Bensalem et al. [31] | Arria 10 | OpenCL, dati in memoria locale (kernel VLU) | 55536* | ALM | n.d. | 312 | n.d. | 22360 † | 402.6 | (1) non verificabile |
| H02 | SHA3-256 | AMD Vitis Security Library [10] | n.d. † | C++, libreria vendor | 44975 | LUT | 36787 | 306.65 † | 1104 | 302.2* | 6.7 | (3) non verificabile |

- † Bensalem et al.:
  - Area dichiarata solo in percentuale: ALM = 13 % × 427 200 ALM dell'Arria 10 GX 1150.
  - TP misurato come dati / tempo del kernel su 10–100 KB, variante SHA-3 non specificata.
  - Memoria esclusa dall'area: 787 M20K.
- † AMD: device non dichiarato (progetto di test configurato per vck190, Versal). Non è dichiarato se il periodo sia stima di sintesi o post-route.

**B.5 — Lattice ECP5**

| ID | Alg. | Lavoro | Architettura | LUT4 | Fmax (MHz) | c | TP (Mbps) | TP/LUT4 | Crit. |
|---|---|---|---|---|---|---|---|---|---|
| L11 | Ascon-Hash | GMU Round 2, GMU2-v2h [1] | 2 r/c, include AEAD | 3764 | 89.2 | 7 | 815.5* | 216.7 | (1) |

## Appendice C — Lavori esclusi

**C.1 — Punti dominati.** 50 punti (17 Ascon-Hash, 33 SHA3-256) sono dominati nel proprio gruppo. Sono elencati nel foglio *Esclusi* di `stato_arte_fpga.xlsx`, con l'ID dei punti che li dominano. Tra questi:
- RECO-HCON [5] su Artix-7, dominato da A02 e A03;
- il core unificato AES/SHA-3 di Kundi et al. [28] su Virtex-7, dominato da S94.
- tutte le varianti con pipeline a 2 stadi nel round di Athanasiou et al. [17] (Virtex-5, -6, -7).

**C.2 — Esclusi per altri motivi**

| Alg. | Lavoro | Famiglia | Motivo |
|---|---|---|---|
| SHA-3 | Kermiche et al. [14] | Virtex-5 / Virtex-6 | TP solo multi-messaggio (5 messaggi nella pipeline a 5 stadi); *c* per messaggio singolo non dichiarato |
| SHA-3 | Assad et al. [19], pipeline a 2, 3, 4, 6 stadi | Virtex-5 / Virtex-6 | TP multi-messaggio; per messaggio singolo *c* resta 24 con area maggiore della versione a 1 stadio (S80, S81), a sua volta dominata da S90 e S92 |
| SHA-3 | Honda et al. [11], 2/3/4/6 round per ciclo | 14 famiglie | Valori numerici non tabulati, solo grafici |
| SHA-3 | Sideris et al. 2022 (Springer, *Image hashing*) | Arria 10 | Solo da fonte secondaria ([23], Tab. 8); inoltre dominato da I42 |
| SHA-3 | Keccak team v1.2 (tabella eHash) | Cyclone III / Stratix III | Keccak v1.x a 18 round: permutazione diversa da FIPS 202; fonte secondaria |
| SHA-3 | Strömbergson (tabella eHash) | Cyclone III / Stratix III | Keccak a 18 round; fonte primaria non trovata |
| SHA-3 | JRC 2014, *FPGA performances in cryptography* | Stratix V | Kernel OpenCL per attacchi a dizionario; risorse solo stimate dal compilatore |
| SHA-3 | Abdulsamad & Répás (*Computers* 2026) | n.d. | Area solo in percentuale; BRAM incoerenti tra le tabelle |
| SHA-3 | Crypto-RV [42] | UltraScale+ (ZCU102) | Frequenza di funzionamento, non Fmax; TP su messaggi lunghi non dichiarato |
| SHA-3 | *Unified SHA-3/SHAKE* [44] | Virtex-7 | Solo area; TP non dichiarato |
| SHA-3 | Gavrilan et al. [43] | Artix-7 | TP su messaggi lunghi non dichiarato; originale non ancora letto ⏳ |
| SHA-3 | Sravani & Durai [25], Dual-f e Cascade-P | Virtex-7 | Solo SHA3-512, senza Fmax e *c*: TP SHA3-256 non ricalcolabile |
| SHA-3 | AMD Vitis Security Library [10], SHA3-512 | n.d. | Solo SHA3-512; la versione SHA3-256 è in Appendice B.4 (H02) |
| Ascon-Hash | Kandi et al. [41] | Kintex-7 | TP non dichiarato; Ascon-Hash descritto con 6 round |
| Ascon-Hash | Khan, Lee, Hwang (TRONSHOW 2022) | Virtex-7 / Virtex-4 / Spartan-6 | Solo AEAD; LUT dichiarati oltre la capacità dei device |
| Ascon-Hash | Alharbi et al. (*IEEE Access* 2024) | 7-series | Solo AEAD |
| Ascon-Hash | Raphael et al. (*Sensors* 2025) | – | Ascon in software su Arduino |

## Appendice D — Fonti in attesa ⏳

| Fonte | Dati mancanti | Perché serve |
|---|---|---|
| Gaj et al., ePrint 2012/368 | Keccak-256 su Altera (ALUT; Stratix III/IV, Cyclone) e Xilinx (slice) | Benchmark canonico SHA-3; PDF non leggibile per intero dal web |
| Wong et al. [26] | Le altre 4 varianti (871 / 1393 / 2145 / 1416 slice, secondo Sideris et al. 2024) | La variante a 871 slice potrebbe stare sul fronte Virtex-6 a bassa area |
| El Moumni et al. [27] | Varianti Virtex-5 e a 12 cicli | Possibili punti sul fronte Virtex-5 |
| Homsirikamol & Gaj [45] | Area, Fmax e cicli delle versioni HLS e RTL di Keccak | Riga HLS su Virtex-6 |
| Jacinto et al. (MWSCAS 2017) | Area (LUT / slice) e Fmax su Zynq-7000 | Riga HLS su Zynq-7000 |
| Core compatti citati da Sundal & Chaves (Tab. I): Jungk, San & At, Winderickx, Pereira, Akin | Valori primari (oggi solo da fonte secondaria) | Potrebbero spostare il fronte Virtex-5 a bassa area |
| Gavrilan et al. [43] | Lettura dell'originale | Verifica dell'esclusione |
| GMU ATHENa, sorgenti RTL SHA-3 (Round 3) | Sorgenti momentaneamente non disponibili | Risintesi sulla xck26 |

## Appendice E — Fonti

| # | Riferimento | Stato |
|---|---|---|
| [1] | K. Mohajerani et al., *FPGA Benchmarking of Round 2 Candidates in the NIST Lightweight Cryptography Standardization Process*, ePrint 2020/1207 | ✔ |
| [2] | K. Mohajerani et al., *SCA Evaluation and Benchmarking of Finalists in the NIST LWC Standardization Process*, ePrint 2023/484 | ✔ |
| [3] | S. Khan et al., *Area–Time Efficient Implementation of NIST Lightweight Hash Functions Targeting IoT Applications*, IEEE IoT-J 10(9), 2023 | ✔ |
| [4] | M. El-Hadedy et al., *RECO-ASCON: Reconfigurable ASCON hash functions for IoT applications*, Integration 93, 2023 | ✔ |
| [5] | X. Wei et al., *RECO-HCON: A High-Throughput Reconfigurable Compact ASCON Processor for Trusted IoT*, IEEE SOCC 2022 | ✔ |
| [6] | B. Rezvani, F. Coleman, S. Sachin, W. Diehl, *Hardware Implementations of NIST Lightweight Cryptographic Candidates: A First Look*, ePrint 2019/824 (rev. 2020) | ✔ |
| [7] | S. Kerckhof et al., *Compact FPGA Implementations of the Five SHA-3 Finalists*, CARDIS 2011 | ✔ |
| [8] | R. Choudhary et al., *ASCON-KECCAK AEAD: A Novel Hardware Architecture for Cryptographic Processing*, TechRxiv, 2026 (preprint) | ✔ |
| [9] | F. A. Urbano-Molano, J. Velasco-Medina, *SHA-3 Implementation for Post-Quantum Cryptography using High-Level Synthesis*, SPL 2023 | ✔ |
| [10] | AMD, *Vitis Security Library — SHA-3*, docs v2020.1 / v2026.1 | ✔ |
| [11] | T. Honda, H. Guntur, A. Satoh, *FPGA Implementation of New Standard Hash Function Keccak*, IEEE GCCE 2014, pp. 275–279 | ✔ |
| [12] | A. Gholipour, S. Mirzakuchaki, *Throughput Optimum Architecture of KECCAK Hash Function*, IJCEE 4(6), 2012 | ✔ |
| [13] | L. Ioannou, H. E. Michail, A. G. Voyiatzis, *High Performance Pipelined FPGA Implementation of the SHA-3 Hash Algorithm*, MECO 2015, pp. 68–71 | ✔ |
| [14] | A. Kermiche, M. Saoudi, A. Drouiche, *High throughput pipelined implementation of SHA3 hash algorithm on FPGA*, J. Cryptogr. Eng. 15:15, 2025 | ✔ |
| [15] | M. Sundal, R. Chaves, *Efficient FPGA Implementation of the SHA-3 Hash Function*, IEEE ISVLSI 2017 | ✔ |
| [16] | B. Baldwin et al., *FPGA Implementations of the Round Two SHA-3 Candidates*, FPL 2010 | ✔ |
| [17] | G. S. Athanasiou, G.-P. Makkas, G. Theodoridis, *High Throughput Pipelined FPGA Implementation of the New SHA-3 Cryptographic Hash Algorithm*, ISCCSP 2014, pp. 538–541 | ✔ |
| [18] | J.-P. Kaps et al., *Lightweight Implementations of SHA-3 Finalists on FPGAs* (GMU; sede e anno non riportati nel PDF) | ✔ |
| [19] | F. Assad, M. Fettach, F. El Otmani, A. Tragha, *High-performance FPGA implementation of the secure hash algorithm 3 for single and multi-message processing*, IJECE 12(2):1324–1333, 2022 | ✔ |
| [20] | A. Sideris, T. Sanida, M. Dasygenis, *Hardware acceleration design of the SHA-3 for high throughput and low area on FPGA*, J. Cryptogr. Eng. 14:193–205, 2024 | ✔ |
| [21] | A. Sideris, T. Sanida, M. Dasygenis, *A Novel Hardware Architecture for Enhancing the Keccak Hash Function in FPGA Devices*, Information 14(9):475, 2023 | ✔ |
| [22] | A. Sideris, T. Sanida, M. Dasygenis, *High Throughput Pipelined Implementation of the SHA-3 Cryptoprocessor*, ICM 2020 | ✔ |
| [23] | A. Sideris et al., *Acceleration of Image Processing with SHA-3 (Keccak) Algorithm using FPGA*, JENRS 1(7):20–28, 2022 | ✔ |
| [24] | A. Sideris, T. Sanida, M. Dasygenis, *High Throughput Implementation of the Keccak Hash Function Using the Nios-II Processor*, Technologies 8(1):15, 2020 | ✔ |
| [25] | M. M. Sravani, S. A. Durai, *On Efficiency Enhancement of SHA-3 for FPGA-Based Multimodal Biometric Authentication*, IEEE TVLSI, 2022 | ✔ |
| [26] | M. M. Wong, J. Haj-Yahya, S. Sau, A. Chattopadhyay, *A New High Throughput and Area Efficient SHA-3 Implementation*, ISCAS 2018 | ✔ parziale |
| [27] | S. El Moumni, M. Fettach, A. Tragha, *High throughput implementation of SHA3 hash algorithm on FPGA*, Microelectronics J. 93:104615, 2019 | ✔ parziale |
| [28] | D. Kundi et al., *Resource-Shared Crypto-Coprocessor of AES Enc/Dec With SHA-3*, IEEE TCAS-I 67(12), 2020 | ✔ |
| [29] | M. Rao, T. Newe, I. Grout, *Secure Hash Algorithm-3 (SHA-3) implementation on Xilinx FPGAs, Suitable for IoT Applications*, IJSSIS, 2019 | ✔ |
| [30] | A. Maache, A. Kalache, *Design and Implementation of a flexible Multi-purpose Cryptographic System on low cost FPGA*, IJECES 14(1):45–58, 2023 | ✔ |
| [31] | H. Bensalem, Y. Blaquière, Y. Savaria, *An Efficient OpenCL-Based Implementation of a SHA-3 Co-Processor on an FPGA-Centric Platform*, IEEE TCAS-II 70(3):1144–1148, 2023 | ✔ |
| [32] | R. Primas, *ascon-verilog* (SP 800-232), github.com/rprimas/ascon-verilog | ✔ |
| [33] | K. Mohajerani, *BlueLight* (Bluespec LWC implementations), github.com/kammoh/bluelight | ✔ |
| [34] | Keccak team, *Keccak hardware implementations* v3.1 (high-speed core) | ✔ |
| [35] | Questo lavoro: ascon-sha3-hybrid-hls (LOG-004, report `hls_impl_pnr` / `hls_run_impl.log`) | ✔ |
| [36] | CAST Inc., *SHA-3 Secure Hash Crypto Engine*, product brief AMD / Intel | ✔ |
| [37] | CAST Inc., *ASCON-F*, implementation results | ✔ |
| [38] | Xiphera, *XIP3030C Compact SHA-3*, pagine prodotto Lattice e Microchip | ✔ |
| [39] | Xiphera, *XIP2201B Balanced Ascon*, pagina prodotto Lattice | ✔ |
| [40] | C. Rubattu, A. Ledda, F. Ratto, C. Jugade, D. Goswami, F. Palumbo, *Integrating FPGA-Based Acceleration in Industrial Motion Control System*, IEEE OJIES 6, 2025 | ✔ |
| [41] | A. Kandi et al., *Side-Channel and Fault Resistant ASCON Implementation*, IEEE ISVLSI 2024 (ePrint 2024/984) | ✔ |
| [42] | A. K. Pham et al., *Crypto-RV: High-Efficiency FPGA-Based RISC-V Cryptographic Co-Processor for IoT Security*, arXiv 2602.04415, 2026 | ✔ |
| [43] | I. Gavrilan et al., *Impeccable Keccak: Towards Fault Resilient SPHINCS+ Implementations*, TCHES 2024(2) | ⏳ |
| [44] | *Lightweight Unified Sha-3/Shake Architecture with a Fault-Resilient State*, arXiv 2512.03616, 2025 | ✔ |
| [45] | E. Homsirikamol, K. Gaj, *Hardware Benchmarking of Cryptographic Algorithms Using High-Level Synthesis Tools: The SHA-3 Contest Case Study*, ARC 2015 | ✔ parziale |
| [46] | M. Elomda et al., *Crypto-Agile FPGA Architecture with Single-Cycle Switching for OFDM-Based Vehicular Networks*, Signals 7(2), 2026 | ✔ |
| [47] | S. Müller, *Ascon-Keccak AEAD Algorithm*, ePrint 2024/858 | ✔ |
| [48] | C. Dobraunig et al., *Ascon v1.2: Lightweight Authenticated Encryption and Hashing*, J. Cryptology 34, 2021 | ✔ |
| – | GMU ATHENa, sorgenti RTL SHA-3 (Round 3) | ⏳ |
