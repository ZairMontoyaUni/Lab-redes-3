#!/usr/bin/env bash
set -e
mkdir -p certs
openssl req -x509 -newkey rsa:2048 -nodes -keyout certs/key.pem -out certs/cert.pem \
        -days 30 -subj "/CN=localhost"
echo "Certificado creado en certs/"
