# Lab 3 - Sockets y Capa de Transporte (Grupo 9, Sección 3)
ISIS2311L - Redes y Comunicaciones - Universidad de los Andes

Sistema publicación–suscripción en C con sockets TCP y UDP (y QUIC como bono). Funciona en Linux y macOS.

## 1. Instalar herramientas

**Linux (Ubuntu/Debian)**
```bash
sudo apt update
sudo apt install -y build-essential git tcpdump iproute2 openssl linux-modules-extra-$(uname -r)
```

**macOS**
```bash
xcode-select --install
```

## 2. Obtener y compilar el proyecto
Clonar el repositorio:
```bash
git clone https://github.com/ZairMontoyaUni/Lab-redes-3.git
cd Lab-redes-3
```
O descomprimir el `.zip` y abrir una terminal dentro de la carpeta descomprimida.

Luego, en cualquiera de los dos casos:
```bash
chmod +x scripts/*.sh
make
```

## 3. Ejecutar

### Demo automática (1 broker, 2 suscriptores, 2 publicadores)
```bash
./scripts/demo.sh tcp
./scripts/demo.sh udp
```
Al terminar se muestra la salida de los dos suscriptores: el suscriptor 1 recibe 10 mensajes y el suscriptor 2 recibe 20.

### Ejecución manual (una terminal por proceso, en este orden)
```bash
./bin/broker_tcp
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA PartidoB
./bin/publisher_tcp 127.0.0.1 9300 PartidoA 10 500
./bin/publisher_tcp 127.0.0.1 9300 PartidoB 10 500
```
Para UDP se usan los mismos comandos cambiando `_tcp` por `_udp` y el puerto `9300` por `9301`.
Ctrl+C en un suscriptor muestra el resumen de mensajes recibidos, perdidos y desordenados.

## 4. Capturar el tráfico
Iniciar la captura en otra terminal antes de correr la demo y detenerla con Ctrl+C al terminar.
En macOS se usa `-i lo0` en lugar de `-i lo`.
```bash
sudo tcpdump -i lo -w captures/tcp_pubsub.pcap "tcp port 9300"
sudo tcpdump -i lo -w captures/udp_pubsub.pcap "udp port 9301"
```

## 5. Simular pérdida y desorden
```bash
sudo ./scripts/netem.sh on
./scripts/demo.sh udp 50 100
./scripts/demo.sh tcp 50 100
sudo ./scripts/netem.sh off
```
Con la red simulada, UDP muestra mensajes perdidos (`HUECO`) y TCP los recibe todos.

## 6. Bono QUIC
En macOS se necesita Homebrew (https://brew.sh).
```bash
./scripts/instalar_picoquic.sh
./scripts/generar_certificado.sh
make quic
./scripts/demo.sh quic
```
