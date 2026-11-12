# Ottimizzazioni *logiche* — standalone Ascon-Hash

Convenzione: identificatori e commenti nel codice restano in inglese.

---

## Ottimizzazioni applicate

- **LOG-001** — Ristrutturazione per HLS: permutazione scomposta in funzioni foglia (chi, diffusion), label su loop e call-site, squeeze in 4 fasi, top sintetizzabile `hash_top`.
- **LOG-002** — Variante standalone: solo il ramo Ascon del codice ibrido, API senza `mode`.
- **LOG-003** — Pass pragma-only: `INLINE off` su `keccakf_asconp12` (una sola istanza), `INLINE` su fasi della spugna e funzioni foglia, `ARRAY_PARTITION complete` su `bc[5]` e costanti di round, `PIPELINE II=1` sui loop di I/O; `PIPELINE` su `rounds_loop` mantenuta (round su 5 word, NE0), `LOOP_TRIPCOUNT`.
- **LOG-006** — Esplorazioni scartate, annotate nel codice (`[EXPLORED]`, stime HLS): copia locale partizionata dello stato (28.1k LUT), `PIPELINE` su `absorbing_loop` (dipendenza su `st`), rimozione di `INLINE off` (una permutazione per call-site, 2× LUT), `UNROLL` sugli squeeze (seconda permutazione).
- **LOG-010** — `enable_auto_rewind=1`, come nelle altre varianti (era 0).
- **LOG-013 (B-01)** — Union rimossa: stato di sole 5 word `uint64_t st[5]` (niente storage morto), accesso a byte come selezione di lane su `st[0]` (indice di word costante, rate = 8 byte), `init_loop` da 25 a 5 iterazioni; partizionamento automatico di HLS disattivato. Risultato (post-route + cosim): 61 → 45 cicli/blocco, f post-route +42%, throughput +93%, LUT −14%.
- **LOG-014 (B-03)** — Interfaccia AXI4-Stream 64 bit (in/out) + AXI4-Lite (`inlen`, `ap_return`): un beat = un blocco (XOR in `st[0]` + permutazione), squeeze in 4 beat, controllo TLAST/`inlen`, `mdlen` rimosso (digest fisso a 32 byte), `hash()` e API a puntatori rimossi, sorgenti in C++. Risultato (post-route + cosim): 45 → 24 cicli/blocco, throughput +61%.

---

## 1. Stato in registri: partizionamento completo di `st[5]` — impatto: ALTO

Con la union rimossa lo stato è partizionabile, ma il partizionamento automatico è disattivato (LOG-013): oggi `st[5]` è una RAM a 2 porte, mappata da Vivado su 4 BRAM (LOG-014). Con

```c
#pragma HLS ARRAY_PARTITION variable=H.st type=complete dim=1
```

le 5 word diventano 5 registri: niente BRAM, ogni accesso è libero e `chi`, `diffusion` e gli step lineari diventano puramente combinatori. 5 word sono minuscole, quindi il costo d'area atteso è basso.

---

## 2. Permutazione: srotolamento parziale dei round — impatto: VARIABILE (solo se cambia la metrica)

I 12 round hanno una dipendenza completa su `st` (round r+1 dipende da round r), quindi pipeline a II basso non è possibile a logica invariata. Con lo stato in registri (punto 1) un round intero diventa combinatorio: si potrebbe valutare lo **srotolamento di K round** (es. 2 o 3) per ridurre l'overhead di controllo, accettando un aumento d'area. **Contro l'obiettivo area-first**, quindi lo lascio come leva di throughput, non come raccomandazione.

---

## Sintesi

| # | Modifica | Tocca | Impatto | Costo area |
|---|----------|-------|---------|------------|
| 1 | Stato in registri (`ARRAY_PARTITION complete` su `st[5]`) | hash.cpp | Alto | Basso |
| 2 | Srotolare K round | hash.cpp | Variabile | Alto (contro area-first) |

Ordine consigliato entro un profilo area-first: **1**, poi **2** solo se la metrica passa da area a throughput.
