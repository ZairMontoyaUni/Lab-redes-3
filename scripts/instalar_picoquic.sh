#!/usr/bin/env bash
# instalar_picoquic.sh - Descarga y compila picotls + picoquic en third_party/ (para el BONO QUIC).
#
# Uso (desde la raiz del repo, en la VM Ubuntu):
#   ./scripts/instalar_picoquic.sh
# Demora unos minutos. Despues: make quic
#
# Por que una libreria externa: QUIC (RFC 9000) incluye TLS 1.3, cifrado de paquetes, control
# de congestion y streams sobre UDP; no se puede implementar solo con la biblioteca estandar de C.
# Ver src/quic/README_QUIC.md para la documentacion de cada funcion usada.
set -e
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
TP="$RAIZ/third_party"
mkdir -p "$TP"

echo ">> Dependencias del sistema (pide sudo)"
sudo apt-get update || echo "(aviso: apt update tuvo errores en algun repositorio; se continua)"
sudo apt-get install -y build-essential cmake git pkg-config libssl-dev

echo ">> picotls (TLS 1.3 que usa picoquic)"
[ -d "$TP/picotls" ] || git clone --depth 1 https://github.com/h2o/picotls.git "$TP/picotls"
(cd "$TP/picotls" && git submodule update --init --depth 1)
mkdir -p "$TP/picotls/build" && cd "$TP/picotls/build"
cmake .. > /dev/null
make -j"$(nproc)" picotls-core picotls-openssl picotls-minicrypto
make -j"$(nproc)" picotls-fusion || echo "(picotls-fusion no disponible en esta arquitectura: se omite)"

echo ">> picoquic"
[ -d "$TP/picoquic" ] || git clone --depth 1 https://github.com/private-octopus/picoquic.git "$TP/picoquic"
mkdir -p "$TP/picoquic/build" && cd "$TP/picoquic/build"
FUSION=""
[ -f "$TP/picotls/build/libpicotls-fusion.a" ] && FUSION="-DPTLS_FUSION_LIBRARY=$TP/picotls/build/libpicotls-fusion.a"
cmake -DPTLS_INCLUDE_DIR="$TP/picotls/include" \
      -DPTLS_CORE_LIBRARY="$TP/picotls/build/libpicotls-core.a" \
      -DPTLS_OPENSSL_LIBRARY="$TP/picotls/build/libpicotls-openssl.a" \
      -DPTLS_MINICRYPTO_LIBRARY="$TP/picotls/build/libpicotls-minicrypto.a" \
      $FUSION .. > /dev/null
make -j"$(nproc)" picoquic-core

echo ">> Listo. Ahora: ./scripts/generar_certificado.sh && make quic"
