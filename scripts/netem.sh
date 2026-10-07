#!/usr/bin/env bash
# netem.sh - Simula una red mala en la interfaz loopback para que UDP pierda/desordene
# paquetes y TCP tenga que retransmitir. REQUIERE sudo.
#   Linux : tc + netem sobre "lo"
#   macOS : dummynet (dnctl + pfctl) sobre "lo0", solo para los puertos del lab (9300-9302)
#
# Uso:
#   sudo ./scripts/netem.sh on                 # 10% perdida, 50ms de retraso, 25% reorden
#   sudo ./scripts/netem.sh on 20 100 30 25    # perdida%, retraso_ms, jitter_ms, reorden%
#   sudo ./scripts/netem.sh status
#   sudo ./scripts/netem.sh off                # IMPORTANTE: quitarlo al terminar
#
# En macOS no existe "jitter": el reorden se logra mandando el <reorden%> de los paquetes
# por una segunda tuberia con <jitter_ms> de retraso EXTRA (por defecto 250 ms, mas que el
# intervalo entre mensajes, para que un mensaje alcance a adelantarse al anterior).
#
# Linux, si da error de modulo: sudo apt install linux-modules-extra-$(uname -r)

ACCION=${1:-status}
PERDIDA=${2:-10}
RETRASO=${3:-50}
REORDEN=${5:-25}

if [ "$(uname)" = "Darwin" ]; then
  JITTER=${4:-250}
  ANCLA="com.apple/lab3"      # /etc/pf.conf ya trae: dummynet-anchor "com.apple/*"
  LENTA=9302; NORMAL=9301     # numeros de tuberia (pipes) de dummynet
  PUERTOS="{ 9300, 9301, 9302 }"

  # $1 = texto extra de la regla lenta ("probability N%"); imprime las reglas del ancla
  reglas() {
    for dir in "from any to any port $PUERTOS" "from any port $PUERTOS to any"; do
      echo "dummynet out quick on lo0 proto { tcp, udp } $dir $1 pipe $LENTA"
    done
    for dir in "from any to any port $PUERTOS" "from any port $PUERTOS to any"; do
      echo "dummynet out quick on lo0 proto { tcp, udp } $dir pipe $NORMAL"
    done
  }

  case "$ACCION" in
    on)
      PLR=$(awk "BEGIN { print $PERDIDA / 100 }")
      dnctl pipe $NORMAL config delay "$RETRASO" plr "$PLR" || exit 1
      dnctl pipe $LENTA  config delay $((RETRASO + JITTER)) plr "$PLR" || exit 1
      pfctl -q -f /etc/pf.conf 2>/dev/null          # asegura que el ancla com.apple/* este cargada
      if reglas "probability ${REORDEN}%" | pfctl -q -a "$ANCLA" -f - 2>/dev/null; then
        echo "dummynet ACTIVO en lo0: perdida ${PERDIDA}%, retraso ${RETRASO}ms, reorden ${REORDEN}% (+${JITTER}ms)"
      elif reglas "" | tail -n 2 | pfctl -q -a "$ANCLA" -f -; then
        echo "dummynet ACTIVO en lo0: perdida ${PERDIDA}%, retraso ${RETRASO}ms (SIN reorden: pf rechazo 'probability')"
      else
        echo "No se pudieron cargar las reglas de pf" >&2; exit 1
      fi
      pfctl -q -E 2>/dev/null                        # enciende pf si estaba apagado
      ;;
    off)
      pfctl -q -a "$ANCLA" -F all 2>/dev/null
      dnctl -q pipe delete $NORMAL 2>/dev/null
      dnctl -q pipe delete $LENTA 2>/dev/null
      echo "dummynet DESACTIVADO"
      ;;
    status)
      pfctl -a "$ANCLA" -s dummynet 2>/dev/null
      dnctl list
      ;;
    *) echo "Uso: sudo $0 on|off|status [perdida%] [retraso_ms] [jitter_ms] [reorden%]"; exit 1 ;;
  esac
  exit 0
fi

JITTER=${4:-20}
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
