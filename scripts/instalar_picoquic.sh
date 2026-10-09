#!/usr/bin/env bash
set -e
RAIZ=$(cd "$(dirname "$0")/.." && pwd)
TP="$RAIZ/third_party"
mkdir -p "$TP"

PICOTLS_COMMIT=f07f1c8c68b237f1468bc1f1fe1b68aba3ff23b4
PICOQUIC_COMMIT=0a847bd58f5dd4f19238876a52f0b8600f066b18


clonar() {
  if [ ! -d "$2/.git" ]; then
    git init -q "$2" && git -C "$2" remote add origin "$1"
  fi
  git -C "$2" fetch -q --depth 1 origin "$3"
  git -C "$2" checkout -q FETCH_HEAD
}

if [ "$(uname)" = "Darwin" ]; then
  echo ">> Dependencias del sistema (Homebrew)"
  command -v brew >/dev/null || { echo "Falta Homebrew: instalalo desde https://brew.sh"; exit 1; }
  brew install cmake pkg-config openssl@3
  OPENSSL_DIR=$(brew --prefix openssl@3)
  CMAKE_SSL="-DOPENSSL_ROOT_DIR=$OPENSSL_DIR"
  NPROC=$(sysctl -n hw.ncpu)
else
  echo ">> Dependencias del sistema (pide sudo)"
  sudo apt-get update || echo "(aviso: apt update tuvo errores en algun repositorio; se continua)"
  sudo apt-get install -y build-essential cmake git pkg-config libssl-dev
  CMAKE_SSL=""
  NPROC=$(nproc)
fi

echo ">> picotls (TLS 1.3 que usa picoquic)"
clonar https://github.com/h2o/picotls.git "$TP/picotls" "$PICOTLS_COMMIT"
(cd "$TP/picotls" && git submodule update --init --depth 1)
rm -rf "$TP/picotls/build" && mkdir -p "$TP/picotls/build" && cd "$TP/picotls/build"
cmake $CMAKE_SSL .. > /dev/null
make -j"$NPROC" picotls-core picotls-openssl picotls-minicrypto

make -j"$NPROC" picotls-fusion 2>/dev/null || echo "(picotls-fusion no disponible en esta arquitectura: se omite)"

echo ">> picoquic"
clonar https://github.com/private-octopus/picoquic.git "$TP/picoquic" "$PICOQUIC_COMMIT"
rm -rf "$TP/picoquic/build" && mkdir -p "$TP/picoquic/build" && cd "$TP/picoquic/build"
FUSION=""
[ -f "$TP/picotls/build/libpicotls-fusion.a" ] && FUSION="-DPTLS_FUSION_LIBRARY=$TP/picotls/build/libpicotls-fusion.a"
cmake $CMAKE_SSL \
      -DPTLS_INCLUDE_DIR="$TP/picotls/include" \
      -DPTLS_CORE_LIBRARY="$TP/picotls/build/libpicotls-core.a" \
      -DPTLS_OPENSSL_LIBRARY="$TP/picotls/build/libpicotls-openssl.a" \
      -DPTLS_MINICRYPTO_LIBRARY="$TP/picotls/build/libpicotls-minicrypto.a" \
      $FUSION .. > /dev/null
make -j"$NPROC" picoquic-core

echo ">> Listo. Ahora: ./scripts/generar_certificado.sh && make quic"
