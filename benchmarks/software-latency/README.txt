===============================================================================
  STATISTICS-TEST  -  Benchmark di latenza software (versione semplificata)
===============================================================================

Confronto della latenza fra l'implementazione ibrida e le due singole:
  - hybrid-test/   SHA3-256 + SHA3-512 + Ascon  (una sola codebase)
  - sha3-test/     SHA-3 (256 e 512)
  - ascon-test/    Ascon-Hash


STRUTTURA
-------------------------------------------------------------------------------
  statistics-test/
  +-- bench_common.h          infrastruttura di misura condivisa (commentata)
  +-- run_all.sh              compila, esegue e genera result.txt
  +-- README.txt              questo file
  +-- hybrid-test/  hash.h  hash.c  main.c
  +-- sha3-test/    hash.h  hash.c  main.c
  +-- ascon-test/   hash.h  hash.c  main.c
  +-- results/                creata dallo script; conterra' solo result.txt

I file hash.h / hash.c sono gia' privi delle pragma HLS.
I file main.c e run_all.sh sono commentati passo passo.


COME SI USA (in WSL)
-------------------------------------------------------------------------------
  cd <repo>/benchmarks/software-latency
  chmod +x run_all.sh
  ./run_all.sh

Al termine trovi UN SOLO file:  results/result.txt
E' un file di testo pensato per essere mostrato cosi' com'e' ai professori:
contiene una legenda delle sigle e, per ogni algoritmo, le tabelle di
confronto (tempo in ns, cicli di CPU, throughput) con la riga "hybrid" e la
riga della singola affiancate, piu' una tabella delle differenze percentuali.

Verifica rapida (poche iterazioni, ~10 secondi) per controllare che funzioni:
  ./run_all.sh quick


COSA VIENE MISURATO
-------------------------------------------------------------------------------
  - 100000 iterazioni misurate + 1000 di warmup (scartate) per ogni caso
  - compilazione con -O2 -march=native
  - per ogni esecuzione: tempo (clock_gettime, ns) e cicli di CPU (rdtsc)
  - outlier rimossi con la regola IQR (solo recinto superiore Q3+1.5*IQR):
    interrupt e context switch aggiungono solo tempo, quindi si scarta la
    coda alta; il minimo resta quello grezzo
  - statistiche (sui campioni tenuti): media, mediana, deviazione standard,
    min, max, piu' la percentuale di campioni scartati (out%)
  - throughput in byte/s e hash/s (calcolati sulla media ripulita)
  - input sintetici deterministici a 16, 64, 256, 1024, 4096 byte
  - correttezza verificata sui vettori KAT prima di iniziare a misurare

Nota: con la rimozione degli outlier la deviazione standard e il massimo
diventano molto piu' bassi e rappresentativi, perche' non includono piu' i
picchi dovuti a interrupt e context switch del sistema operativo. La colonna
out% ti dice quanti campioni sono stati scartati in ciascun caso.
