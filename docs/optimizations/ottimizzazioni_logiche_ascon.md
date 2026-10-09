# Ottimizzazioni *logiche* possibili — standalone Ascon-Hash

Come per l'ibrida, in questa iterazione **non ho applicato modifiche di logica**: i file `hash.c`/`hash.h`/`main.c` consegnati contengono solo l'ottimizzazione via pragma, a logica invariata (self-test superati con gcc). Qui elenco le modifiche che cambierebbero il codice, ordinate per impatto. La differenza importante rispetto all'ibrida è che in Ascon **tutti i bound sono costanti**, quindi il vero limite residuo è solo la `union` (RAM a 2 porte): rimuoverla rende Ascon banale da parallelizzare.

Convenzione: identificatori e commenti nel codice restano in inglese.

---

## 1. Stato Ascon dedicato: 5 word in registri — impatto: ALTISSIMO (è IL punto su Ascon)

Ascon-Hash lavora **solo su 5 word di stato** (320 bit: `st[0..4]`). Oggi lo stato è dichiarato come 25 word dentro una `union` (eredità del codice condiviso con SHA-3), quindi:
- 20 word su 25 sono **storage morto** (mai usate ma allocate);
- la `union` impone una **singola RAM a 2 porte**, che è l'unico vincolo che ancora frena il parallelismo (i bound sono già costanti).

**Proposta.** Header dedicato senza union:
```c
typedef struct {
    uint64_t s[5];          // stato Ascon, 320 bit
    int pt, rsiz, mdlen;
} hash_ctx_t;
```
e indirizzamento a byte via shift/mask nei punti che oggi usano `st.b[]` (absorbing, padding, squeezing), p.es. `s[k>>3] ^= ((uint64_t)byte) << (8*(k&7))`.

**Effetto.** Con
```c
#pragma HLS ARRAY_PARTITION variable=s type=complete dim=1
```
i 5 word diventano 5 registri: ogni accesso è libero, le porte non sono più un collo di bottiglia. A quel punto:
- `chi`, `diffusion` e gli step lineari di round diventano **puramente combinatori** (II=1 reale ovunque);
- si possono persino srotolare i loop a 5 senza problemi di porte, valutando il trade-off con l'area (5 word sono minuscoli).

È, fra tutte e tre le varianti, la modifica con il miglior rapporto guadagno/sforzo, proprio perché lo stato Ascon è piccolissimo.

---

## 2. Azzerare solo 5 word in `hash_init` — impatto: BASSO (ma gratis)

`init_loop` azzera 25 word; ad Ascon ne servono 5 (`s[0..4]`, con `s[0]=IV`). Ridurre il loop a 5 iterazioni (o a 5 assegnazioni esplicite) elimina cicli inutili. Subordinato/coerente col punto 1.

---

## 3. Assorbimento a granularità di word — impatto: BASSO/MEDIO

Il rate di Ascon-Hash è 8 byte = **1 word**. Oggi `absorbing_loop` assorbe 1 byte per iterazione e permuta ogni 8 byte. Con lo stato in registri (punto 1) e I/O a stream, si potrebbe assorbire **1 word per iterazione** (gestendo l'ultimo blocco parziale), riducendo di ~8× le iterazioni del loop di assorbimento.

---

## 4. I/O AXI4-Stream "vero" con `hls::stream`/`ap_axiu` — impatto: MEDIO (robustezza)

Identico al discorso fatto per l'ibrida: con i port-array, `mode=axis` dà uno stream a 8 bit senza `TLAST`; la quantità di beat è governata da `inlen`/`mdlen` via AXI-Lite. Passare a `hls::stream< ap_axiu<8,...> >` con `TLAST` rende l'IP componibile e robusto. Per Ascon l'output è sempre 32 byte, quindi lato `md` la delimitazione è banale.

---

## 5. Permutazione: srotolamento parziale dei round — impatto: VARIABILE (solo se cambia la metrica)

I 12 round hanno una dipendenza completa su `st` (round r+1 dipende da round r), quindi pipeline a II basso non è possibile a logica invariata. Con lo stato in registri (punto 1) un round intero diventa combinatorio: si potrebbe valutare lo **srotolamento di K round** (es. 2 o 3) per ridurre l'overhead di controllo, accettando un aumento d'area. **Contro l'obiettivo area-first**, quindi lo lascio come leva di throughput, non come raccomandazione.

---

## Sintesi

| # | Modifica | Tocca | Impatto | Costo area |
|---|----------|-------|---------|------------|
| 1 | Stato dedicato 5 word in registri (no union) | hash.h, hash.c | Altissimo | Molto basso |
| 2 | Azzerare solo 5 word in init | hash.c | Basso | Nullo |
| 3 | Assorbimento a word (rate = 8 byte) | hash.c | Basso/Medio | Basso |
| 4 | I/O `hls::stream`/`ap_axiu` + TLAST | hash.c, main.c | Medio | Basso |
| 5 | Srotolare K round | hash.c | Variabile | Alto (contro area-first) |

Ordine consigliato entro un profilo area-first: **1 → 2 → 3**, e **4** in fase di integrazione. Il **5** solo se la metrica passa da area a throughput. Nota: il punto 1 su Ascon è molto più conveniente che sulle altre varianti, perché lo stato utile è di soli 5 word.
