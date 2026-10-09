#!/usr/bin/env bash
PROTO=${1:-tcp}
N=${2:-10}
INTERVALO=${3:-300}

case "$PROTO" in
  tcp) PUERTO=9300 ;;
  udp) PUERTO=9301 ;;
  quic) PUERTO=9302 ;;
  *) echo "Uso: $0 tcp|udp|quic [num_mensajes] [intervalo_ms]"; exit 1 ;;
esac

trap 'kill $(jobs -p) 2>/dev/null' EXIT

BIN=bin
LOGS=logs
mkdir -p "$LOGS"
for p in broker subscriber publisher; do
  [ -x "$BIN/${p}_$PROTO" ] || { echo "Falta $BIN/${p}_$PROTO. Ejecuta 'make' primero."; exit 1; }
done

echo ">> Iniciando broker $PROTO en el puerto $PUERTO"
if [ "$PROTO" = "quic" ]; then
  [ -f certs/cert.pem ] || { echo "Falta certs/cert.pem: ejecuta ./scripts/generar_certificado.sh"; exit 1; }
  $BIN/broker_quic $PUERTO certs/cert.pem certs/key.pem > $LOGS/broker_$PROTO.log 2>&1 &
else
  $BIN/broker_$PROTO $PUERTO > $LOGS/broker_$PROTO.log 2>&1 &
fi
BROKER=$!
sleep 1

echo ">> Suscriptor 1 -> PartidoA | Suscriptor 2 -> PartidoA y PartidoB"
$BIN/subscriber_$PROTO 127.0.0.1 $PUERTO PartidoA > $LOGS/sub1_$PROTO.log 2>&1 &
SUB1=$!
$BIN/subscriber_$PROTO 127.0.0.1 $PUERTO PartidoA PartidoB > $LOGS/sub2_$PROTO.log 2>&1 &
SUB2=$!
sleep 1

echo ">> Publicadores: PartidoA y PartidoB ($N mensajes c/u, cada $INTERVALO ms)"
$BIN/publisher_$PROTO 127.0.0.1 $PUERTO PartidoA $N $INTERVALO > $LOGS/pub1_$PROTO.log 2>&1 &
PUB1=$!
$BIN/publisher_$PROTO 127.0.0.1 $PUERTO PartidoB $N $INTERVALO > $LOGS/pub2_$PROTO.log 2>&1 &
PUB2=$!

wait $PUB1 $PUB2
sleep 2
echo ">> Deteniendo suscriptores y broker (Ctrl+C simulado)..."
kill -TERM $SUB1 $SUB2 $BROKER 2>/dev/null
sleep 1

for f in sub1 sub2; do
  echo; echo "=================== $f ($PROTO) ==================="
  cat $LOGS/${f}_$PROTO.log
done
echo; echo "Logs completos en $LOGS/"
