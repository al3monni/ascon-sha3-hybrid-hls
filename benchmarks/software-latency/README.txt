===============================================================================
  SOFTWARE-LATENCY  -  Benchmark di latenza software (hybrid vs standalone)
===============================================================================

Confronto della latenza fra l'implementazione ibrida e le due singole:
  - hybrid  SHA3-256 + SHA3-512 + Ascon  (una sola codebase)  <- ../../Hybrid-Test/hash.c
  - sha3    SHA-3 (256 e 512)                                 <- ../../Sha-Test/hash.c
  - ascon   Ascon-Hash                                        <- ../../Ascon-Test/hash.c

FONTE UNICA: il benchmark compila DIRETTAMENTE i sorgenti dei componenti Vitis
(gcc ignora le pragma HLS). Non esistono copie del codice: quello che si misura
e' per costruzione il codice che va in sintesi.


STRUTTURA
-------------------------------------------------------------------------------
  software-latency/
  +-- bench_common.h          infrastruttura di misura condivisa (commentata)
  +-- run_all.sh              compila, esegue e genera i risultati
  +-- isolate.sh              isolamento del dispositivo (livello 1, Linux nativo)
  +-- README.txt              questo file
  +-- hybrid-test/main.c      main di misura (KAT + casi sintetici)
  +-- sha3-test/main.c
  +-- ascon-test/main.c
  +-- build/                  creata dallo script, ignorata da git
  +-- results/<piattaforma>/  result.txt (report) + data.tsv (dati grezzi)
                              <piattaforma> = <arch>-<native|wsl>, es.
                              x86_64-wsl, x86_64-native, aarch64-native (Kria)


COSA VIENE MISURATO
-------------------------------------------------------------------------------
Regione cronometrata: UNA chiamata a

    hash(msg, len, md, mdlen [, mode]);   // = hash_init + hash_update + hash_final

con input gia' in memoria e caldo in cache. Esclusi: generazione dell'input,
verifica KAT, statistiche, I/O.

Per ogni caso (algoritmo x dimensione 16/64/256/1024/4096 B) due fasi:

  A) per chiamata : 10000 chiamate, ognuna fra due letture dei tick (registro
                    rdtsc / cntvct_el0, nessuna syscall); ns = tick / f_tick
                    -> distribuzione: media, mediana, sd, min, max, outlier
  B) batch        : 10 sotto-batch da 1000 chiamate consecutive; contatori letti
                    SOLO prima e dopo ogni sotto-batch -> MEDIANA di: ns/hash,
                    cicli core reali, istruzioni, IPC, cicli/byte (cpb);
                    dispersione fra sotto-batch (disp%) come indicatore di qualita'

Le metriche aggiuntive non disturbano l'algoritmo: dentro il loop misurato ci
sono solo letture di registri (tick) o nulla (fase B); i contatori hardware
(PMU) contano in hardware e si leggono fuori dal loop.

Footprint statico (a compile time, nessun impatto sulle misure):
  ctx (sizeof(hash_ctx_t)), stima dello stack di hash(), .text e .rodata.

Tick:  x86_64  -> rdtsc (TSC, frequenza costante: NON i cicli reali del core)
       aarch64 -> cntvct_el0 (generic timer; frequenza letta da cntfrq_el0)
I cicli REALI del core vengono solo dalla PMU (fase B).


COME SI USA
-------------------------------------------------------------------------------
Requisiti: gcc (pacchetto build-essential), git.

  # (una volta) permessi per i contatori hardware, validi fino al riavvio
  sudo sysctl -w kernel.perf_event_paranoid=2

  # (consigliato) frequenza fissa per misure stabili, se disponibile
  sudo cpupower frequency-set -g performance      # oppure governor "performance"

  cd <repo>/benchmarks/software-latency

  # Linux nativo (portatile, Kria): misura su dispositivo isolato (consigliato)
  sudo systemctl isolate multi-user.target   # opzionale: chiude la grafica (usa SSH/console)
  sudo ./isolate.sh run                      # isola -> esegue run_all.sh -> ripristina
  sudo ./isolate.sh run quick                # verifica rapida

  # WSL (isolamento non possibile: chiudere le app)
  ./run_all.sh            # completo: 10000 iterazioni per fase
  ./run_all.sh quick      # verifica rapida (1000 iterazioni)

  PLATFORM=nome ./run_all.sh   # forza il nome della cartella dei risultati

ISOLAMENTO (isolate.sh, livello 1, senza riavvio e reversibile)
  - SMT spento e governor "performance" (ripristinati alla fine)
  - core di misura = CPU online con indice piu' alto, riservato al benchmark:
    tutti gli altri processi (user.slice, system.slice, init.scope) e gli
    interrupt hardware vengono spostati sugli altri core
  - fermati i servizi automatici (unattended-upgrades, apt timers, snapd,
    packagekit, fwupd, ...) se attivi; riavviati alla fine
  - il benchmark gira in una slice dedicata (bench.slice) come utente normale
  - se interrotto con Ctrl+C il ripristino avviene comunque;
    in caso di problemi: sudo ./isolate.sh off
  - restano attivi solo i thread del kernel legati al core (non spostabili
    senza riavvio: per quello servirebbe isolcpus, "livello 2")

Senza permessi perf (o in WSL, dove la PMU di solito non e' esposta) il
benchmark gira comunque: le colonne cicli/istruzioni/IPC/cpb valgono "n/a".


KRIA KV260 (Ubuntu)
-------------------------------------------------------------------------------
  sudo apt update && sudo apt install -y build-essential git
  git clone https://github.com/al3monni/ascon-sha3-hybrid-hls.git
  cd ascon-sha3-hybrid-hls/benchmarks/software-latency
  sudo sysctl -w kernel.perf_event_paranoid=2
  ./run_all.sh
  # risultati in results/aarch64-native/ : commit + push dalla Kria, oppure
  # scp results/aarch64-native/* <utente>@<pc>:<repo>/benchmarks/software-latency/results/aarch64-native/


NOTE PER UNA MISURA PULITA
-------------------------------------------------------------------------------
  - Il report elenca in testa gli "AVVISI SULLA QUALITA' DELLA MISURA".
  - PC portatile: alimentatore collegato, appoggiato su superficie rigida.
  - SMT, governor e isolamento sono gestiti da isolate.sh.
  - Il processo viene fissato sul core di misura (taskset).
  - L'intestazione di result.txt registra CPU, kernel, governor, SMT,
    clocksource, compilatore, flag, overhead e disponibilita' della PMU.
