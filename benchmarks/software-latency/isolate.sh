#!/usr/bin/env bash
# ============================================================================
# isolate.sh  -  Isolamento del dispositivo per il benchmark (livello 1)
# ----------------------------------------------------------------------------
# Obiettivo: sul core che misura deve girare SOLO il benchmark, e il resto del
# sistema deve essere a riposo. Tutto a runtime, senza riavvio, reversibile.
#
# Cosa fa "on":
#   1. SMT spento (se supportato): ogni core fisico serve una sola CPU logica
#   2. governor "performance" su tutte le CPU (frequenza stabile)
#   3. sceglie il core di misura (CPU online con indice piu' alto)
#   4. sposta TUTTI gli altri processi sugli altri core
#      (cpuset di systemd: AllowedCPUs su user.slice, system.slice, init.scope)
#   5. sposta gli interrupt hardware sugli altri core (dove il kernel lo consente)
#   6. ferma i servizi che si attivano da soli (aggiornamenti, snap, ...)
# "off" ripristina tutto com'era (stato salvato in $STATE).
#
# Uso (sempre con sudo):
#   sudo ./isolate.sh run      on -> esegue ./run_all.sh sul core riservato -> off
#                              (consigliato: il ripristino avviene anche con Ctrl+C)
#   sudo ./isolate.sh on       solo isolamento
#   sudo ./isolate.sh off      solo ripristino
#   sudo ./isolate.sh status   mostra lo stato attuale
#   Argomenti dopo "run" vengono passati a run_all.sh (es. sudo ./isolate.sh run quick)
#
# Da fare A MANO prima (consigliato): chiudere l'interfaccia grafica e lanciare
# il benchmark da SSH o da console testuale:
#   sudo systemctl isolate multi-user.target        (ripristino: graphical.target)
#
# Non applicabile in WSL: lo scheduler e' quello di Windows.
# ============================================================================

set -uo pipefail

STATE="/run/bench-isolate.state"
HERE="$(cd "$(dirname "$0")" && pwd)"

# Servizi che possono attivarsi durante la misura (fermati solo se attivi)
SERVICES=(unattended-upgrades.service apt-daily.timer apt-daily-upgrade.timer
          packagekit.service snapd.service snapd.socket fwupd.service
          man-db.timer motd-news.timer)

die()  { echo "isolate.sh: $*" >&2; exit 1; }
info() { echo "  - $*"; }

[[ $EUID -eq 0 ]] || die "va eseguito con sudo"
grep -qi microsoft /proc/version 2>/dev/null && die "in WSL l'isolamento non e' possibile (scheduler di Windows)"
command -v systemctl >/dev/null || die "systemd non trovato"

# --- elenco CPU online espanso, es. "0-3,6" -> "0 1 2 3 6" -------------------
online_cpus() {
    local list part a b i out=()
    list="$(cat /sys/devices/system/cpu/online)"
    IFS=',' read -ra parts <<< "$list"
    for part in "${parts[@]}"; do
        a="${part%-*}"; b="${part#*-}"
        for ((i = a; i <= b; i++)); do out+=("$i"); done
    done
    echo "${out[@]}"
}
join_by_comma() { local IFS=','; echo "$*"; }

isolate_on() {
    [[ -f "$STATE" ]] && die "isolamento gia' attivo (sudo ./isolate.sh off per ripristinare)"
    : > "$STATE"
    echo "Isolamento livello 1:"

    # 1. SMT
    local smt; smt="$(cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo notsupported)"
    echo "SMT_PREV=$smt" >> "$STATE"
    if [[ "$smt" == "on" ]]; then
        echo off > /sys/devices/system/cpu/smt/control && info "SMT spento"
    else
        info "SMT: $smt (nessuna modifica)"
    fi

    # 2. governor
    local g f
    for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
        [[ -w "$f" ]] || continue
        g="$(cat "$f")"
        echo "GOV $f $g" >> "$STATE"
        echo performance > "$f" 2>/dev/null
    done
    info "governor: performance"

    # 3. core di misura e core "di servizio"
    local cpus core others
    read -ra cpus <<< "$(online_cpus)"
    core="${cpus[-1]}"
    others=("${cpus[@]:0:${#cpus[@]}-1}")
    (( ${#others[@]} > 0 )) || die "serve almeno un secondo core online"
    local others_list; others_list="$(join_by_comma "${others[@]}")"
    echo "CORE=$core" >> "$STATE"
    echo "OTHERS=$others_list" >> "$STATE"
    info "core di misura: $core   (altri processi su: $others_list)"

    # 4. tutti gli altri processi fuori dal core di misura
    local unit
    for unit in user.slice system.slice init.scope; do
        systemctl set-property --runtime "$unit" AllowedCPUs="$others_list" \
            && info "$unit -> CPU $others_list"
    done

    # 5. interrupt sugli altri core (alcuni IRQ per-CPU non sono spostabili)
    local irq cur moved=0 fixed=0
    for irq in /proc/irq/[0-9]*; do
        [[ -w "$irq/smp_affinity_list" ]] || continue
        cur="$(cat "$irq/smp_affinity_list")"
        if echo "$others_list" > "$irq/smp_affinity_list" 2>/dev/null; then
            echo "IRQ $irq $cur" >> "$STATE"; moved=$((moved + 1))
        else
            fixed=$((fixed + 1))
        fi
    done
    if [[ -w /proc/irq/default_smp_affinity ]]; then
        echo "IRQDEF $(cat /proc/irq/default_smp_affinity)" >> "$STATE"
        local mask=0 c; for c in "${others[@]}"; do mask=$((mask | (1 << c))); done
        printf '%x\n' "$mask" > /proc/irq/default_smp_affinity 2>/dev/null
    fi
    info "interrupt spostati: $moved (non spostabili: $fixed)"

    # 6. servizi
    local s stopped=()
    for s in "${SERVICES[@]}"; do
        if systemctl is-active --quiet "$s" 2>/dev/null; then
            systemctl stop "$s" 2>/dev/null && { echo "SVC $s" >> "$STATE"; stopped+=("$s"); }
        fi
    done
    info "servizi fermati: ${stopped[*]:-nessuno}"

    echo "DESC=livello 1: core $core riservato (altri processi e IRQ su $others_list), SMT $(cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo n/d), governor performance, servizi fermati: ${stopped[*]:-nessuno}" >> "$STATE"
}

isolate_off() {
    [[ -f "$STATE" ]] || { echo "isolamento non attivo"; return 0; }
    echo "Ripristino:"
    local key a b unit
    # processi: tolgo il vincolo (stringa vuota = nessuna restrizione)
    for unit in user.slice system.slice init.scope; do
        systemctl set-property --runtime "$unit" AllowedCPUs= 2>/dev/null
    done
    info "processi: nessun vincolo di CPU"
    # SMT prima del resto (riaccende le CPU logiche)
    local smt_prev; smt_prev="$(awk -F= '/^SMT_PREV=/ {print $2}' "$STATE")"
    [[ "$smt_prev" == "on" ]] && echo on > /sys/devices/system/cpu/smt/control && info "SMT riacceso"
    while read -r key a b; do
        case "$key" in
            GOV)    [[ -w "$a" ]] && echo "$b" > "$a" 2>/dev/null ;;
            IRQ)    echo "$b" > "$a/smp_affinity_list" 2>/dev/null ;;
            IRQDEF) echo "$a" > /proc/irq/default_smp_affinity 2>/dev/null ;;
            SVC)    systemctl start "$a" 2>/dev/null ;;
        esac
    done < "$STATE"
    info "governor, interrupt e servizi ripristinati"
    rm -f "$STATE"
}

isolate_status() {
    if [[ -f "$STATE" ]]; then
        echo "Isolamento ATTIVO:"; grep '^DESC=' "$STATE" | cut -d= -f2-
    else
        echo "Isolamento non attivo."
    fi
}

isolate_run() {
    local user="${SUDO_USER:-}"
    [[ -n "$user" && "$user" != "root" ]] || die "lancia con sudo da un utente normale (i risultati devono appartenere a lui)"
    isolate_on
    trap 'echo; isolate_off' EXIT INT TERM
    local core desc
    core="$(awk -F= '/^CORE=/ {print $2}' "$STATE")"
    desc="$(grep '^DESC=' "$STATE" | cut -d= -f2-)"
    echo
    echo "Avvio del benchmark come '$user' sul core $core (slice dedicata)..."
    # Slice dedicata FUORI da user/system.slice: e' l'unica a poter usare il core riservato.
    env BENCH_CORE="$core" BENCH_ISOLATION="$desc" \
        systemd-run --scope --quiet --slice=bench.slice -p AllowedCPUs="$core" \
                    --uid="$(id -u "$user")" --gid="$(id -g "$user")" \
                    -- "$HERE/run_all.sh" "$@"
    # (con --scope il comando eredita l'ambiente: BENCH_CORE e BENCH_ISOLATION
    #  arrivano a run_all.sh tramite "env" qui sopra)
}

case "${1:-}" in
    on)     isolate_on ;;
    off)    isolate_off ;;
    status) isolate_status ;;
    run)    shift; isolate_run "$@" ;;
    *)      sed -n '2,32p' "$0"; exit 1 ;;
esac
