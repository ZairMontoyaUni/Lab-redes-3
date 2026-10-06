#!/usr/bin/env bash
# medir_broker.sh - Muestrea CPU y memoria (RSS) del broker cada segundo en formato CSV.
#
# Uso (con el broker ya corriendo, en otra terminal):
#   ./scripts/medir_broker.sh broker_tcp 30 > logs/cpu_tcp.csv     # 30 segundos
#   ./scripts/medir_broker.sh broker_udp 30 > logs/cpu_udp.csv
# Para tener muchos suscriptores (ej. 20 en TCP):
#   for i in $(seq 20); do ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA > /dev/null & done
#   (detenerlos despues con: pkill subscriber_tcp)

NOMBRE=${1:?Uso: $0 <nombre_proceso> [segundos]}
SEG=${2:-30}
PID=$(pgrep -n -x "$NOMBRE") || { echo "No encuentro el proceso $NOMBRE" >&2; exit 1; }

echo "segundo,cpu_percent,rss_kb"
for s in $(seq 1 "$SEG"); do
  read -r cpu rss < <(ps -o %cpu=,rss= -p "$PID") || break
  echo "$s,$cpu,$rss"
  sleep 1
done
