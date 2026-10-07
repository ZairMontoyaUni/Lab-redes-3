#!/usr/bin/env bash
# generar_certificado.sh - Crea un certificado TLS autofirmado para el broker QUIC (30 dias).
# QUIC exige TLS 1.3, por eso el servidor necesita certificado y llave.
#
# Uso (desde la raiz del repo):  ./scripts/generar_certificado.sh
# Crea: certs/cert.pem y certs/key.pem  (estan en .gitignore: NO se suben a GitHub)
set -e
mkdir -p certs
openssl req -x509 -newkey rsa:2048 -nodes -keyout certs/key.pem -out certs/cert.pem \
        -days 30 -subj "/CN=localhost"
echo "Certificado creado en certs/"
