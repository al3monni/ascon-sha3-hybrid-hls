# Analisi delle direttive HLS (`#pragma`) nelle implementazioni di SHA-3 e Ascon

*Report tecnico per la discussione del progetto — analisi del codice `hash.c` / `hash.h`*

---

## 1. Contesto e oggetto dell'analisi

Il progetto implementa in C, destinato alla sintesi ad alto livello (HLS, Vitis/Vivado HLS), due funzioni hash crittografiche:

- **SHA-3 / Keccak** (permutazione Keccak-f[1600] su 24 round);
- **Ascon-Hash** (permutazione Ascon-p su 12 round), il vincitore della competizione NIST per la *lightweight cryptography*.

Esistono **tre varianti** del file `hash.c`, che sono l'oggetto vero di questo confronto:

| Variante | Parametro `mode` | `nrounds` / `chirounds` | Carattere |
|---|---|---|---|
| **A — SHA-3 standalone** | assente | `24` / `25` (**costanti**) | solo Keccak: `theta → rho_pi → chi → iota` |
| **B — Ascon standalone** | assente | `12` / `5` (**costanti**) | solo Ascon: mixing lineare `→ chi → tweak → diffusion` |
| **C — unificata (agile)** | presente (`uint8_t mode`) | calcolati da `mode` (**runtime**) | un unico datapath che, in base a `mode`, esegue Keccak *oppure* Ascon |

La variante C è quella presente nella cartella di progetto ed è l'implementazione "finale": realizza in hardware l'idea di **crypto agility** descritta nel NIST CSWP 39 (un unico blocco riconfigurabile che supporta più algoritmi, invece di un IP separato per ciascuno). Questo punto è importante da sottolineare in sede di discussione: **la variante C non è "più lenta per caso"** — è il prezzo, ben motivato, dell'agilità.

L'osservazione centrale di tutto il report è la seguente:

> **Quasi tutte le differenze di pragma tra le tre varianti si spiegano con due cause: (1) un vincolo strutturale in `hash.h` che rende lo stato `st` non partizionabile, e (2) il fatto che nella variante C i conteggi dei round diventano variabili a runtime, impedendo l'unroll dei loop.**

I capitoli successivi sviluppano questa tesi.

---

## 2. Il vincolo architetturale che spiega tutto: la `union` su `st`

In `hash.h`:

```c
typedef struct {
    union {                 // state:
        uint8_t  b[200];    // 8-bit bytes
        uint64_t q[25];     // 64-bit words
    } st;
    int pt, rsiz, mdlen, mode;
} hash_ctx_t;
```

La `union` fa sì che `st.b[200]` e `st.q[25]` siano **due viste della stessa area di memoria**. Serve perché lo schema sponge richiede di indirizzare lo stato in due modi diversi:

- **a byte** (`st.b`) durante l'assorbimento del messaggio e durante lo *squeeze* dell'output;
- **a word da 64 bit** (`st.q`) durante la permutazione (θ, ρ, π, χ, ι o Ascon-p operano su word).

**Conseguenza per la sintesi:** un array coinvolto in una `union` (e con aliasing tra due tipi di dimensione diversa) **non è partizionabile** dallo strumento HLS. È esattamente quanto annotato nel commento del progetto:

```c
// st is not partitionable because of the union ...
// it is also not possible to partition one field of the union due to this HLS constraint
```

Quindi `st` viene mappato su una **RAM a 2 porte**. Tutte le operazioni di θ/ρ/π/χ/ι e di Ascon-p leggono e scrivono `st` intensamente: con sole 2 porte, questi accessi sono **serializzati** e l'II (Initiation Interval) effettivamente raggiungibile da qualunque loop che tocca `st` è **limitato dalla contesa sulle porte di memoria**, non dalle pragma.

Questo è il vero **collo di bottiglia** del design e va detto chiaramente: è la ragione per cui (a) l'`#pragma HLS ARRAY_PARTITION variable=H.st.q ...` in `hash()` è giustamente lasciato **commentato** (non funzionerebbe), e (b) la strategia complessiva tende al **risparmio d'area** anziché al massimo parallelismo, perché il parallelismo sarebbe comunque strozzato da `st`.

---

## 3. Glossario tecnico delle pragma usate (e come interagiscono)

Per leggere l'analisi serve avere chiaro cosa fa ciascuna direttiva e — soprattutto — come si influenzano a vicenda.

- **`#pragma HLS INLINE`** — fonde la funzione nel chiamante, eliminando la gerarchia. Permette allo scheduler di ottimizzare attraverso il confine di funzione. Adatto a funzioni piccole.
- **`#pragma HLS INLINE off`** — vieta l'inlining: la funzione resta un **blocco RTL separato**, quindi **riutilizzabile**. Costa overhead di chiamata e impedisce ottimizzazioni cross-funzione, ma consente di **istanziare l'hardware una sola volta** e richiamarlo più volte → risparmio d'area.
- **`#pragma HLS PIPELINE II=1`** — pipelina un loop (o una funzione) in modo che una nuova iterazione inizi ogni `II` cicli (`II=1` = una al ciclo). **Effetto collaterale chiave: pipelinare un loop forza l'unroll completo dei loop annidati al suo interno.**
- **`#pragma HLS UNROLL`** — srotola il loop creando copie hardware **parallele** del corpo. Più area, più parallelismo.
- **`#pragma HLS ARRAY_PARTITION variable=x complete dim=1`** — spezza l'array in **registri** singoli, così tutti gli elementi sono accessibili nello stesso ciclo. È spesso il **prerequisito** perché unroll/pipeline diano vantaggio: senza partizionamento l'array resta una memoria a porte limitate e gli accessi paralleli vengono serializzati.

**Tre regole d'interazione decisive per questo progetto:**

1. **Pipeline ⇒ unroll dei loop interni.** Mettere `PIPELINE` su un loop esterno srotola gli interni.
2. **Unroll/parallelismo ⇒ servono porte di memoria.** Senza `ARRAY_PARTITION`, le copie parallele competono sulle 2 porte della RAM e l'II reale degrada.
3. **Bound a runtime ⇒ niente unroll.** Un loop il cui limite è una variabile di runtime **non può essere srotolato** (lo strumento non sa quante copie creare). **Può** però essere pipelinato.

> **Precisazione da tenere pronta per i professori.** Nel codice compare più volte questo commento:
> ```c
> /* When pragmas like ARRAY_PARTITION or ARRAY_RESHAPE are used,
>    the HLS tool automatically unrolls any loops consuming this data by default. */
> ```
> È una semplificazione un po' imprecisa: **`ARRAY_PARTITION` di per sé non forza l'unroll** dei loop che usano l'array. L'unroll è innescato da `UNROLL` o, indirettamente, da `PIPELINE` sul loop genitore. `ARRAY_PARTITION` *abilita* il parallelismo (rendendo possibile l'accesso simultaneo), ma non lo *causa*. Vale la pena correggere questa frase, o riformularla come «partizionare `bc` è ciò che rende *utile* l'unroll dei loop che lo consumano».

---

## 4. Analisi funzione per funzione (con confronto tra le tre varianti)

### 4.1 Le funzioni-foglia di trasformazione: `theta`, `rho_pi`, `chi`, `iota`, `diffusion`

Tutte e cinque hanno **`#pragma HLS INLINE`** in testa. **Scelta corretta:** sono funzioni piccole, ciascuna implementa un singolo passo della permutazione; inlinandole, lo scheduler ottimizza l'intero round come un unico datapath, senza overhead di chiamata. (`iota` ha il commento "applied by default" perché funzioni così piccole verrebbero comunque inlinate dallo strumento.)

Il punto interessante è l'array temporaneo locale **`bc[5]`** e la pragma `ARRAY_PARTITION variable=bc complete dim=1`:

| Funzione | Variante A (SHA) | Variante B (Ascon) | Variante C (unificata) |
|---|---|---|---|
| `theta` (`bc`) | partition **attiva** | n/a (Ascon non usa θ) | partition **commentata** |
| `rho_pi` (`bc`) | partition **attiva** | n/a | partition **commentata** |
| `chi` (`bc`) | partition **attiva** | partition **attiva** | partition **commentata** |
| `diffusion` (`bc`) | n/a (SHA non usa diffusion) | partition **commentata** | partition **commentata** |

I loop interni (`theta_C`, `theta_D`, `theta_D_loop`, `rho_pi_loop`, `bc_cp_chi`, `chi_nlin`, `bc_cp_diff`) hanno in **tutte** le varianti `#pragma HLS PIPELINE II=1` con il commento *"to save area"* e l'`unroll` lasciato commentato (*"applied by default"*).

**Lettura.** Questi loop hanno bound **costante** (`< 5`), quindi sarebbero candidati ideali all'unroll. La scelta di **pipelinarli a II=1 invece di srotolarli** è una scelta consapevole di *risparmio d'area*: si ottiene un datapath piccolo riusato sequenzialmente sulle 5 iterazioni, anziché 5 copie parallele. È **coerente** con la filosofia "area-first" dell'intero progetto.

Sul partizionamento di `bc` nella variante C, vedi §5 e §7: lasciarlo commentato è difendibile, ma è anche il primo candidato a essere **ri-attivato** in fase di tuning, perché `bc` è minuscolo (5×64 bit) e portarlo in registri costa pochissimo, evitando che diventi un secondo collo di bottiglia (oltre a `st`) sui loop interni a II=1.

Nota su `theta` (variante C):
```c
theta_D: for (i = 0; i < 5; i++) {
    #pragma HLS PIPELINE II=1
    ...
    theta_D_loop: for (j = 0; j < 25; j += 5) {
        #pragma HLS PIPELINE II=1   // <-- ridondante
        st[j + i] ^= t;
    }
}
```
Quando si pipelina `theta_D`, il loop interno `theta_D_loop` viene **automaticamente srotolato** (regola 1 del §3): la pragma `PIPELINE` su `theta_D_loop` diventa quindi **ridondante / ignorata**. Non è un errore, ma è un punto di pulizia da poter citare. Inoltre, l'unroll di `theta_D_loop` produce 5 scritture su `st[j+i]` che, con `st` a 2 porte, **non potranno avvenire tutte nello stesso ciclo**: l'II=1 nominale su `theta_D` quasi certamente **non sarà rispettato** e lo strumento rilasserà l'II. Da verificare nel report di scheduling (vedi §9).

### 4.2 Il cuore: `keccakf_asconp12`

```c
#pragma HLS inline off // because it is reused multiple times and we want to save area
```

**Scelta corretta e centrale.** Il core della permutazione è la parte più costosa in area. Lasciandolo **non inlinato**, viene istanziato **una sola volta** in RTL e richiamato da `hash_init` (init permutation), `hash_update` (absorb) e `hash_final` (final/squeeze). La variante B ha persino il commento esplicito: *"bad performance (area) if removed"*, cioè se lo si inlinasse ovunque, il blocco più grande verrebbe **replicato** in ogni punto di chiamata → esplosione d'area. **Da enfatizzare:** è qui che il design "compra" la maggior parte del suo risparmio d'area.

Il loop dei round, `rounds_loop`, è il punto dove A/B e C divergono di più:

| | `nrounds`/`chirounds` | `rounds_loop` |
|---|---|---|
| **A (SHA)** | `24` / `25` costanti | `#pragma HLS pipeline II=1` **attiva** |
| **B (Ascon)** | `12` / `5` costanti | `#pragma HLS pipeline II=1` **attiva** |
| **C (unificata)** | `12*(!mode)+24*(!!mode)` / `5*(!mode)+25*(!!mode)` → **runtime** | `//#pragma HLS pipeline II=1` **commentata** |

**Perché in C è commentata.** Due ragioni concorrono:
1. Il bound `nrounds` ora dipende da `mode` (runtime): il loop **non è srotolabile** (regola 3). Il commento *"(disabled when loop is fully unrolled)"* lo segnala.
2. Anche volendo pipelinarlo, ogni round ha una **dipendenza dati con il precedente** attraverso `st` (loop-carried dependency): un round non può iniziare finché il precedente non ha aggiornato `st`. Pipelinare il loop dei round non darebbe quindi throughput, ma forzerebbe l'unroll di θ/ρ/π/χ/ι **e** della catena di chiamate → esplosione di risorse. **Commentarla è la scelta giusta.**

### 4.3 `chi`: il caso più istruttivo sui bound a runtime

```c
top_chi: for (j = 0; j < chirounds ; j += 5) {
    #pragma HLS pipeline II=1   // pipeline because chirounds is a runtime variable
                                // (disabled when the loop is fully unrolled)
    ...
}
```

| | partition `bc` | `top_chi` pipeline |
|---|---|---|
| **A (SHA)**, **B (Ascon)** | **attiva** | **attiva** (con nota `//maxiteration pragma da vedere`) |
| **C (unificata)** | **commentata** | **commentata** |

In A/B `chirounds` è costante (`25` o `5`): `top_chi` ha bound noto, quindi può essere srotolato/pipelinato e partizionare `bc` ha senso. In C `chirounds` è **runtime** → `top_chi` **non srotolabile**; resta pipelinabile, ma il team ha scelto di pipelinare i due loop interni (`bc_cp_chi`, `chi_nlin`, bound costante 5) e lasciare commentato `top_chi`. La nota **`//maxiteration pragma da vedere`** è preziosa: indica che ci si è accorti che manca un'indicazione del numero massimo di iterazioni → è esattamente il caso d'uso di `LOOP_TRIPCOUNT` (vedi §7).

### 4.4 Le funzioni dello schema sponge: `hash_init`, `hash_update`, `hash_final`

**`hash_init` — `init_loop`** (azzeramento dei 25 word):
```c
#pragma HLS PIPELINE II=1   // applied by default
// #pragma HLS UNROLL       // more resouces but a bit faster
```
Corretto: 25 scritture sequenziali, pipeline a II=1. L'`UNROLL` alternativo è documentato con il suo trade-off (più veloce, più area) ma **giustamente** lasciato commentato — peraltro l'unroll sarebbe comunque strozzato dalle 2 porte di `st`.

**`hash_update` — `absorbing_loop`:** **nessuna pragma**, con il commento *"Pipelining this loop results in a resource explosion"*. **Scelta corretta e non banale:** il loop contiene una chiamata condizionale a `keccakf_asconp12` (quando il blocco è pieno). Pipelinare il loop costringerebbe a srotolare/replicare quella chiamata costosa per iterazione → esplosione. Lasciarlo sequenziale è la decisione giusta. (Bound `len` è runtime: anche qui un `LOOP_TRIPCOUNT` aiuterebbe il report di latenza.)

**`hash_final`:**

- *Output SHA-3* — `output_loop` su `mdlen` (runtime):
  ```c
  // It should not be possible to unroll the loop because mdlen is a runtime variable
  //#pragma HLS pipeline II=1 // applied by default
  ```
  Nella variante C è commentata ("applied by default"); nella variante A è attiva. Entrambe corrette: bound runtime → niente unroll, pipeline (esplicita o di default).
- *Output Ascon* — quattro `squeezing_1..4` da 8 byte con permutazione tra uno squeeze e l'altro. In A/B i `#pragma HLS pipeline II=1` sono **attivi**; in C sono **commentati** (insieme all'`unroll`). Bound costante (8): pipelinarli è innocuo e corretto; commentarli in C lascia che lo strumento applichi i default. Da uniformare per coerenza (vedi §7).

### 4.5 `hash` / `hash_top`

```c
// it is not possible to partition the array st, see hash.h
//#pragma HLS ARRAY_PARTITION variable=H.st.q dim=1 type=cyclic factor=5
//#pragma HLS ARRAY_PARTITION variable=H.st.b dim=1 type=cyclic factor=5
```
Le due pragma di partizionamento sono **giustamente commentate** e rimandano al vincolo della `union` (§2). `hash_top` è il wrapper aggiunto per il *co-simulation* / per fungere da funzione *top* dell'IP. **Manca però qualunque `#pragma HLS INTERFACE`** su questa funzione top: vedi §7, punto 2 — è la lacuna più rilevante per un IP destinato alla cosim.

---

## 5. Perché le varianti divergono — la sintesi della tesi

Mettendo insieme §2–§4, le differenze di pragma seguono **due assi**:

1. **`st` non partizionabile (union)** → vale per tutte e tre le varianti → impone una filosofia *area-first* e rende inutile partizionare lo stato a qualunque livello.
2. **Bound costanti (A, B) vs bound runtime (C)**:
   - In **A/B** `nrounds`/`chirounds` sono costanti → i loop dei round e `top_chi` sono **srotolabili/pipelinabili** e partizionare `bc` *serve* → pragma attive.
   - In **C** quegli stessi bound diventano **runtime** (perché dipendono da `mode`) → quei loop **non sono più srotolabili** → le pragma di partizione/pipeline ai livelli alti vengono **commentate** e il design ripiega su pipeline a II=1 sui soli loop interni a bound costante.

In altre parole: **passando da due IP specializzati a un unico IP agile, si perde la possibilità di sfruttare i bound costanti, e le pragma cambiano di conseguenza.** Questo è il messaggio da consegnare ai professori, ed è una conseguenza *attesa e corretta* dell'agilità, non un difetto.

---

## 6. Punti di forza del codice (da valorizzare in sede di discussione)

1. **Decomposizione pulita in sotto-funzioni** (`theta`, `rho_pi`, `chi`, `iota`, `diffusion`): rende il datapath leggibile e consente di applicare/leggere le direttive per blocco. Architettura modulare, idiomatica per HLS.
2. **Uso sistematico delle label sui loop** (`theta_C`, `theta_D`, `rho_pi_loop`, `top_chi`, `bc_cp_chi`, `chi_nlin`, `init_loop`, `absorbing_loop`, `squeezing_*`, `init_perm`, `absorb_perm`, `final_perm_*`). **Ottima pratica HLS:** le label rendono leggibili i report di scheduling/risorse e permettono di spostare le direttive in un file `.tcl`/directory di direttive, applicandole *per nome* invece che inline. Facilita anche il *design space exploration*.
3. **Strategia `INLINE` coerente e corretta:** foglie inlinate per ottimizzazione locale, core `inline off` per riuso e risparmio d'area. È la scelta giusta data l'architettura.
4. **Riuso del core della permutazione** (un'istanza richiamata da init/absorb/final): è il principale risparmio d'area del design, ed è una scelta deliberata e ben motivata nei commenti.
5. **La `union` come idioma sponge:** nonostante costi la partizionabilità, è il modo naturale ed elegante di esprimere il doppio indirizzamento byte/word richiesto dallo schema sponge.
6. **La fusione Keccak-χ ↔ Ascon S-box:** la variante C sfrutta il fatto che la χ di Keccak, con alcuni *tweak* lineari post-χ, è equivalente all'S-box di Ascon. Ciò consente di condividere `chi` tra i due algoritmi. È la realizzazione hardware concreta del concetto di *crypto agility* del NIST CSWP 39 — un punto concettualmente forte.
7. **Pragma documentate con la motivazione** (`to save area`, `applied by default`, `resource explosion`, `disabled when loop is fully unrolled`): è esattamente ciò che serve per *difendere* le scelte di design. Rende il codice auto-esplicativo.

---

## 7. Pragma da aggiungere / modificare, file per file (con motivazione)

Ordinate per impatto.

**1. `#pragma HLS LOOP_TRIPCOUNT` su tutti i loop a bound runtime — *priorità massima per una presentazione*.**
Riguarda: `top_chi` (`chirounds ∈ {5, 25}`), `output_loop` (`mdlen ≤ 64`), `absorbing_loop` (`len ≤ 256`) e, nella variante C, `rounds_loop` (`nrounds ∈ {12, 24}`).
```c
top_chi: for (j = 0; j < chirounds; j += 5) {
    #pragma HLS LOOP_TRIPCOUNT min=1 max=5    // Ascon: 1 iter; SHA: 5 iter
    ...
}
```
Non cambia l'hardware, ma **dà a HLS i numeri per stimare latenza e throughput**: senza, il report mostra latenze *indeterminate* ("?") sui loop a bound variabile, rendendo i risultati poco presentabili. È letteralmente ciò a cui allude il commento `//maxiteration pragma da vedere` (`max_iteration`/`LOOP_TRIPCOUNT` sono lo stesso concetto).

**2. `#pragma HLS INTERFACE` sulla funzione top `hash_top` — *lacuna più rilevante per la cosim*.**
La funzione top è priva di direttive di interfaccia: lo strumento applica protocolli di default che possono non coincidere con l'integrazione desiderata (es. AXI). Per un IP destinato alla co-simulazione vanno specificate esplicitamente le porte di `in[256]`, `md[64]`, gli scalari `inlen`/`mdlen`/`mode` e il controllo a blocco. Esempio (da adattare al target):
```c
void hash_top(uint8_t in[256], int inlen, uint8_t md[64], int mdlen, uint8_t mode) {
    #pragma HLS INTERFACE mode=s_axilite port=inlen
    #pragma HLS INTERFACE mode=s_axilite port=mdlen
    #pragma HLS INTERFACE mode=s_axilite port=mode
    #pragma HLS INTERFACE mode=s_axilite port=return
    #pragma HLS INTERFACE mode=bram port=in
    #pragma HLS INTERFACE mode=bram port=md
    ...
}
```

**3. `#pragma HLS ALLOCATION` per *forzare* una sola istanza del core.**
Il risparmio d'area dato da `inline off` è efficace solo se lo strumento istanzia `keccakf_asconp12` **una volta sola**. Per renderlo esplicito (e garantito, non solo sperato):
```c
#pragma HLS ALLOCATION function instances=keccakf_asconp12 limit=1
```
da inserire nei chiamanti (o nelle direttive globali). Trasforma l'*intento* "save area" in un *vincolo* verificato.

**4. Ri-attivare `#pragma HLS ARRAY_PARTITION variable=bc complete dim=1` anche nella variante C (da testare).**
`bc` è 5×64 bit: portarlo in registri costa pochissimo e impedisce che diventi un secondo collo di bottiglia sui loop interni pipelinati a II=1. Poiché tali loop hanno bound costante (5), il partizionamento è utile e *non* implica srotolare i loop a bound runtime. Da verificare in sintesi, ma è il candidato n.1 per migliorare l'II reale a costo d'area trascurabile. (Nota: questa modifica chiarisce anche, nei fatti, la frase imprecisa del §3 sul "partition auto-unroll".)

**5. Rimuovere le pragma `PIPELINE` ridondanti** dove un loop interno è già srotolato dalla pipeline del genitore (es. `theta_D_loop` in §4.1). Pulizia, nessun effetto funzionale.

**6. Uniformare le pragma degli `squeezing_*` e `output_loop`** tra le varianti (in C sono commentate, in A/B attive): scegliere una linea (pipeline esplicita) e renderla coerente, così il comportamento è documentato e non lasciato ai default impliciti.

**7. (Opzionale, alto impatto sul throughput) Stato locale partizionato.**
È il vero "passo successivo" se l'obiettivo diventasse la *velocità* anziché l'area. Poiché la permutazione usa **solo** l'indirizzamento a word (l'indirizzamento a byte serve unicamente in absorb/squeeze), si può, all'ingresso del core, copiare `st.q` in un array locale `uint64_t local_st[25]` con `#pragma HLS ARRAY_PARTITION local_st complete`, eseguire i round sulla copia partizionata (ora θ/ρ/π/χ/ι **possono** essere srotolati a II=1 senza la strozzatura delle 2 porte), e infine riscrivere su `st.q`. Si paga una copia in/out, ma si rimuove il collo di bottiglia descritto nel §2. Va presentato come *direzione futura*, non come correzione: cambia il punto di lavoro area↔velocità.

---

## 8. Tabella di sintesi — pragma per funzione e per variante

| Funzione / Loop | A — SHA standalone | B — Ascon standalone | C — unificata | Verdetto |
|---|---|---|---|---|
| foglie (`theta`/`rho_pi`/`chi`/`iota`/`diffusion`) — `INLINE` | sì | sì | sì | corretto |
| `bc` — `ARRAY_PARTITION complete` | attiva | attiva | **commentata** | difendibile in C; candidata a riattivazione (§7.4) |
| loop interni (5 iter) — `PIPELINE II=1` | sì | sì | sì | corretto (scelta area) |
| `keccakf_asconp12` — `INLINE off` | sì | sì | sì | **corretto e centrale** (riuso/area) |
| `rounds_loop` — `PIPELINE II=1` | attiva (bound cost.) | attiva (bound cost.) | **commentata** (bound runtime + dipendenza) | corretto in tutte |
| `top_chi` — `PIPELINE II=1` | attiva | attiva | **commentata** | corretto; manca `LOOP_TRIPCOUNT` (§7.1) |
| `init_loop` — `PIPELINE II=1` | sì | sì | sì | corretto |
| `absorbing_loop` — nessuna pragma | sì | sì | sì | **corretto** (evita esplosione) |
| `output_loop` (SHA) — `PIPELINE` | attiva | n/a | commentata (default) | corretto (bound runtime) |
| `squeezing_*` (Ascon) — `PIPELINE` | n/a | attiva | commentate (default) | corretto; da uniformare |
| `hash_top` — `INTERFACE` | assente | assente | **assente** | **da aggiungere** (§7.2) |

---

## 9. Punti aperti da verificare in sintesi (e da menzionare proattivamente)

Per blindare la presentazione, conviene aprire i report di Vitis HLS e controllare:

1. **II realmente raggiunto vs II target.** A causa di `st` a 2 porte, è probabile che i loop con scritture multiple su `st` (es. `theta_D` con `theta_D_loop` srotolato) **non rispettino II=1** e che lo strumento lo rilassi. Conviene saperlo e spiegarlo *prima* che lo chiedano i professori: l'II è limitato dalla memoria, non dalle pragma.
2. **Numero di istanze di `keccakf_asconp12`.** Verificare nel report risorse che ne esista **una sola** (altrimenti aggiungere `ALLOCATION`, §7.3).
3. **Latenze indeterminate** sui loop a bound runtime, da risolvere con `LOOP_TRIPCOUNT` (§7.1).
4. **Coerenza funzionale**: il `main.c` esegue self-test con vettori noti (SHA3-256, SHA3-512, Ascon-256). Il C-simulation/cosim deve restituire "All Self-Tests OK!": è la prova che le pragma non hanno alterato la funzionalità (le direttive HLS *non devono* cambiare il risultato, solo l'hardware).

---

### In una frase, da dire ai professori

> «Lo stato `st` è una `union` byte/word, quindi non partizionabile: è il collo di bottiglia che impone una strategia *area-first* (core `inline off` riusato, loop interni pipelinati a II=1 invece di srotolati). Le due versioni specializzate possono permettersi `ARRAY_PARTITION` e pipeline sui loop dei round perché i conteggi sono *costanti*; nella versione agile quegli stessi conteggi dipendono da `mode` a *runtime*, quindi i loop non sono più srotolabili e le pragma vengono coerentemente disattivate. Le pragma presenti sono corrette; le aggiunte più utili sono `LOOP_TRIPCOUNT` (per avere numeri di latenza sensati), `INTERFACE` sul top per la cosim e `ALLOCATION` per garantire l'istanza unica del core.»
