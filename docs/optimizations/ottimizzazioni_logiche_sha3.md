//=======================================================================================================================



#### 1\. Eliminare la union su st



Lo stato st è una union tra una vista a byte (b\[200]) e una vista a word (q\[25]).



Le due viste condividono la stessa memoria, e per HLS questo significa una sola RAM a 2 porte.



Questo condiziona pesantemente l'II raggiungibile dai cicli interni quando ci sono più di due letture/scritture per ciclo.



SHA-3 usa tutte le 25 word di stato a ogni round (theta e chi leggono e scrivono tutte e 25, rho\_pi 24).

Questo è il caso **più penalizzato** dal limite dalle porte della ram.



Indirizzando esclusivamente lo stato per word (uint64\_t q\[25]) permetterebbe di poterlo partizionare come:



* \#pragma HLS ARRAY\\\_PARTITION variable=q type=cyclic factor=5 dim=1 --> mappando le colonne su array separati
* \#pragma HLS ARRAY\\\_PARTITION variable=q type=ccomplete dim=1 --> mappando tutti i bit su registri separati (parallelismo totale) *\[+ area]*





Attraverso maschere/shift sarebbe comunque possibile accedervi a byte.



Questo impatterebbe l'absorbing loop in hash\_update() e il padding e squeezing in hash\_final().



* c->st.b\\\[j] ^= byte --> diventerebbe --> c->st.q\\\[j>>3] ^= ((uint64\\\_t)byte) << (8 \\\* (j \\\& 7))





//=======================================================================================================================



#### 2\. Rompere la dipendenza seriale in rho\_pi()



La funzione rho\_pi() contiene un loop con una dipendenza sulla variabile t, il valore prodotto nell'iterazione presedente è usato nella successiva.



Si può ristrutturare questa fase copiando lo stato in un buffer locale eliminando la catena su t.



In questo modo il loop diventa pipelinabile. *\[+ area + troughput]*



Su SHA-3 questo loop pesa più che altrove (24 iterazioni per round × 24 round).





//=======================================================================================================================



#### 3\. AXI4-Stream via hls::stream



L'interfaccia di *uint8\_t in\[256]* e *uint8\_t md\[64]* attraverso hls::stream< ap\_axiu<8,0,0,0> > con TLAST



L'interfaccia di *int mdlen*, *uint8\_t mode*, attraverso AXI4-Lite eliminando *int inlen*



Necessario cambiare logica su hash\_update e hash\_final.





