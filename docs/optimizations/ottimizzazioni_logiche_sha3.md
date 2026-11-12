//=======================================================================================================================



#### Ottimizzazioni applicate



* **LOG-001** — Ristrutturazione per HLS: permutazione scomposta in funzioni foglia (theta, rho\_pi, chi, iota), label su loop e call-site, top sintetizzabile hash\_top.
* **LOG-002** — Variante standalone: solo il ramo SHA-3 del codice ibrido, API senza mode.
* **LOG-003** — Pass pragma-only: INLINE off su keccakf\_asconp12 (una sola istanza), INLINE su fasi della spugna e funzioni foglia, ARRAY\_PARTITION complete su bc\[5] e tabelle di costanti, PIPELINE II=1 dove l'accesso allo stato lo consente, LOOP\_TRIPCOUNT.
* **LOG-006** — Esplorazioni annotate nel codice (\[EXPLORED]): PIPELINE solo sui loop interni invece che su top\_chi (4.2k vs 5.0k LUT, area-first); PIPELINE II=1 su rounds\_loop scartata (stime HLS: 53 cicli/permutazione ma LUT 3.1k → 8.9k; da rivalutare: B-08).
* **LOG-013 (B-01)** — Union rimossa: stato uint64\_t st\[25], vista a byte via shift/mask (st\_xor\_byte); partizionamento automatico di HLS disattivato. Risultato (post-route + cosim): BRAM 10 → 0, LUT −5%, f post-route +16%, throughput +24% (SHA3-256).
* **LOG-014 (B-03)** — Interfaccia AXI4-Stream 64 bit (in/out) + AXI4-Lite (inlen, mdlen, ap\_return): absorb e squeeze a word, controllo TLAST/inlen, hash() e API a puntatori rimossi, sorgenti in C++. Risultato (post-route + cosim): throughput +16% / +10% (SHA3-256 / SHA3-512), LUT +20%.





//=======================================================================================================================



#### 1\. Partizionamento esplicito dello stato



Con la union rimossa (B-01) lo stato uint64\_t st\[25] è partizionabile; il partizionamento automatico è disattivato, quindi va fatto con pragma esplicite.

SHA-3 usa tutte le 25 word di stato a ogni round (theta e chi leggono e scrivono tutte e 25, rho\_pi 24).

Questo è il caso **più penalizzato** dal limite dalle porte della ram.



* \#pragma HLS ARRAY\\\_PARTITION variable=H.st type=cyclic factor=5 dim=1 --> mappando le colonne su array separati
* \#pragma HLS ARRAY\\\_PARTITION variable=H.st type=complete dim=1 --> mappando tutte le word su registri separati (parallelismo totale) *\[+ area]*





//=======================================================================================================================



#### 2\. Rompere la dipendenza seriale in rho\_pi()



La funzione rho\_pi() contiene un loop con una dipendenza sulla variabile t, il valore prodotto nell'iterazione presedente è usato nella successiva.



Si può ristrutturare questa fase copiando lo stato in un buffer locale eliminando la catena su t.



In questo modo il loop diventa pipelinabile. *\[+ area + troughput]*



Su SHA-3 questo loop pesa più che altrove (24 iterazioni per round × 24 round).
