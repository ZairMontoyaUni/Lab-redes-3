#!/usr/bin/env bash
# netem.sh - Simula una red mala en la interfaz loopback (lo) para que UDP pierda/desordene
# paquetes y TCP tenga que retransmitir. REQUIERE sudo.
#
# Uso:
#   sudo ./scripts/netem.sh on                 # 10% perdida, 50ms +-20ms retraso, 25% reorden
#   sudo ./scripts/netem.sh on 20 100 30 25    # perdida%, retraso_ms, jitter_ms, reorden%
#   sudo ./scripts/netem.sh status
#   sudo ./scripts/netem.sh off                # IMPORTANTE: quitarlo al terminar
#
# Si da error de modulo: sudo apt install linux-modules-extra-$(uname -r)

ACCION=${1:-status}
PERDIDA=${2:-10}
RETRASO=${3:-50}
JITTER=${4:-20}
REORDEN=${5:-25}

case "$ACCION" in
  on)
    tc qdisc del dev lo root 2>/dev/null
    tc qdisc add dev lo root netem loss ${PERDIDA}% delay ${RETRASO}ms ${JITTER}ms reorder ${REORDEN}% 50%
    echo "netem ACTIVO en lo: perdida ${PERDIDA}%, retraso ${RETRASO}ms (+-${JITTER}ms), reorden ${REORDEN}%"
    ;;
  off)
    tc qdisc del dev lo root 2>/dev/null && echo "netem DESACTIVADO" || echo "No habia netem activo"
    ;;
  status)
    tc qdisc show dev lo
    ;;
  *) echo "Uso: sudo $0 on|off|status [perdida%] [retraso_ms] [jitter_ms] [reorden%]"; exit 1 ;;
esac
