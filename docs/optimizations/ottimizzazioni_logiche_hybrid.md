//=======================================================================================================================



#### Ottimizzazioni applicate



* **LOG-001** — Ristrutturazione per HLS: permutazione scomposta in funzioni foglia (theta, rho\_pi, chi, iota, diffusion), label su loop e call-site, squeeze Ascon in 4 fasi, top sintetizzabile hash\_top.
* **LOG-003** — Pass pragma-only: INLINE off su keccakf\_asconp12 (una sola istanza condivisa dai due algoritmi), INLINE su fasi della spugna e funzioni foglia, ARRAY\_PARTITION complete su bc\[5] e tabelle di costanti, PIPELINE II=1 dove l'accesso allo stato lo consente, LOOP\_TRIPCOUNT.
* **LOG-006** — Lezioni delle esplorazioni sulle standalone riportate come commenti \[EXPLORED] (stime HLS): INLINE off mantenuta, PIPELINE II=1 su rounds\_loop rimandata (B-08).
* **LOG-013 (B-01)** — Union rimossa: stato uint64\_t st\[25], vista a byte via shift/mask (st\_xor\_byte); partizionamento automatico di HLS disattivato (AUTO\_PARTITION=off, il cyclic factor=3 dedotto dal tool peggiorava tutto). Risultato (post-route + cosim): BRAM 10 → 0, LUT −3%, throughput invariato (cicli −4/−7%, f −7%).
* **LOG-014 (B-03)** — Interfaccia AXI4-Stream 64 bit (in/out) + AXI4-Lite (inlen, mdlen, mode, ap\_return): absorb e squeeze a word (un beat = una word di stato), controllo TLAST/inlen, hash() e API a puntatori rimossi, sorgenti in C++. Risultato (post-route + cosim): throughput +39% / +32% / +31% (SHA3-256 / SHA3-512 / Ascon), LUT +15%.





//=======================================================================================================================



#### 1\. Partizionamento esplicito dello stato



Con la union rimossa (B-01) lo stato uint64\_t st\[25] è partizionabile; il partizionamento automatico è disattivato, quindi va fatto con pragma esplicite:



* \#pragma HLS ARRAY\\\_PARTITION variable=H.st type=cyclic factor=5 dim=1 --> mappando le colonne su array separati
* \#pragma HLS ARRAY\\\_PARTITION variable=H.st type=complete dim=1 --> mappando tutte le word su registri separati (parallelismo totale) *\[+ area]*





//=======================================================================================================================



#### 2\. Rompere la dipendenza seriale in rho\_pi()



La funzione rho\_pi() contiene un loop con una dipendenza sulla variabile t, il valore prodotto nell'iterazione presedente è usato nella successiva.



Si può ristrutturare questa fase copiando lo stato in un buffer locale eliminando la catena su t.



In questo modo il loop diventa pipelinabile. *\[+ area + troughput]*



//=======================================================================================================================



#### 3\. Ramo Ascon del datapath condiviso



Nell'ibrida Ascon costa 449 cicli/blocco contro 24 della standalone (LOG-014): usa lo stato a 25 word in memoria e loop rolled dimensionati per Keccak.

È il principale collo di bottiglia dell'ibrida, da affrontare insieme al punto 1.
