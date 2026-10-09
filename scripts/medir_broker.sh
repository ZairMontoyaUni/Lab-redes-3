#!/usr/bin/env bash
NOMBRE=${1:?Uso: $0 <nombre_proceso> [segundos]}
SEG=${2:-30}
PID=$(pgrep -n -x "$NOMBRE") || { echo "No encuentro el proceso $NOMBRE" >&2; exit 1; }

echo "segundo,cpu_percent,rss_kb"
for s in $(seq 1 "$SEG"); do
  read -r cpu rss < <(ps -o %cpu=,rss= -p "$PID") || break
  echo "$s,$cpu,$rss"
  sleep 1
done
