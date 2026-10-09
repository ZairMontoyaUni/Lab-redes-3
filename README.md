# Lab 3 - Sockets y Capa de Transporte (Grupo 9, Sección 3)
ISIS2311L - Redes y Comunicaciones - Universidad de los Andes

Sistema de noticias deportivas en tiempo real con el modelo **publicación–suscripción**
(publicadores = periodistas, broker = canal central, suscriptores = hinchas), implementado en **C**
con sockets **TCP** y **UDP**, y **QUIC** como bono.

Esta guía explica paso a paso cómo compilar, ejecutar y verificar el proyecto en **Linux** o **macOS**.
El procedimiento es el mismo en los dos sistemas. Solo cambian la instalación de herramientas (sección 2)
y el nombre de la interfaz loopback (`lo` en Linux, `lo0` en macOS).

---

## Índice
1. [Requisitos](#1-requisitos)
2. [Instalación de herramientas](#2-instalación-de-herramientas)
3. [Compilar](#3-compilar)
4. [Verificación paso a paso](#4-verificación-paso-a-paso)
5. [Bono QUIC](#5-bono-quic)
6. [Problemas frecuentes](#6-problemas-frecuentes)
7. [Referencia: estructura, protocolo y scripts](#7-referencia-estructura-protocolo-y-scripts)

---

## 1. Requisitos

| | Linux | macOS |
|---|---|---|
| Sistema probado | Ubuntu 22.04 / 24.04 (VM, nativo o Docker) | macOS 12 o superior (Intel o Apple Silicon) |
| Compilador | `gcc` + `make` (`build-essential`) | `clang` (se invoca como `gcc`) + `make` de las Xcode Command Line Tools |
| Interfaz loopback | `lo` | `lo0` |
| Simular red mala | `tc` + netem | dummynet (`dnctl` + `pfctl`) |
| Captura de tráfico | `tcpdump` / Wireshark | `tcpdump` (ya viene) / Wireshark |

> **Windows:** no hay soporte nativo (el código usa sockets POSIX). Usar una VM con Ubuntu (VirtualBox)
> o WSL2 con Ubuntu. En WSL2 todo funciona menos, posiblemente, `netem` (depende del kernel de WSL).

Todo corre en **una sola máquina** usando `127.0.0.1`: cada proceso (broker, suscriptores, publicadores)
va en su propia terminal, o se lanzan todos juntos con `scripts/demo.sh`.

---

## 2. Instalación de herramientas

### Linux (Ubuntu/Debian)
```bash
sudo apt update
sudo apt install -y build-essential git tcpdump wireshark iproute2 openssl procps \
                    linux-modules-extra-$(uname -r)
```
- `linux-modules-extra` trae el módulo `sch_netem`, que se usa para simular pérdida. Si `apt` no lo
  encuentra (p. ej. dentro de Docker), se puede omitir y saltar la prueba 4.5.
- Para usar Wireshark sin `sudo`: `sudo usermod -aG wireshark $USER` y cerrar sesión y volver a entrar.

### macOS
```bash
xcode-select --install                 # compilador, make y git (si ya están instalados, avisa y no hace nada)
brew install --cask wireshark          # opcional, para abrir las capturas (requiere Homebrew: https://brew.sh)
```
`tcpdump`, `openssl`, `dnctl` y `pfctl` ya vienen con macOS.

> La primera vez que corra un broker, macOS puede preguntar si se permiten conexiones entrantes.
> Cualquier respuesta sirve: el tráfico por `127.0.0.1` no pasa por el firewall.

### Obtener el código
```bash
git clone https://github.com/ZairMontoyaUni/Lab-redes-3.git      # o descomprimir el .zip entregado
cd Lab-redes-3
chmod +x scripts/*.sh                            # por si los permisos se perdieron al descomprimir
```
**Todos los comandos de esta guía se ejecutan desde la raíz del proyecto.**

---

## 3. Compilar
```bash
make clean
make            # compila TCP y UDP
ls bin
```
**Resultado esperado:** la compilación termina sin errores y `bin/` contiene 6 ejecutables:
```
broker_tcp  publisher_tcp  subscriber_tcp  broker_udp  publisher_udp  subscriber_udp
```
TCP y UDP **no usan ninguna librería externa**, solo la biblioteca estándar de C y sockets POSIX.
(`make tcp` o `make udp` compilan solo una versión.)

---

## 4. Verificación paso a paso

Cada prueba indica qué ejecutar y qué se debe observar. Las pruebas 4.1 a 4.4 cubren lo que pide el
enunciado; las demás muestran las diferencias entre TCP y UDP.

### 4.1 Demo automática TCP (1 broker + 2 suscriptores + 2 publicadores)
```bash
./scripts/demo.sh tcp
```
El script lanza:
- un broker en el puerto 9300,
- **Suscriptor 1** suscrito a `PartidoA`,
- **Suscriptor 2** suscrito a `PartidoA` y `PartidoB`,
- dos publicadores (`PartidoA` y `PartidoB`), cada uno con 10 mensajes, uno cada 300 ms.

Al final imprime la salida de los dos suscriptores (tarda unos 8 segundos).

**Resultado esperado:**
| | Mensajes recibidos | Perdidos | Desordenados | Duplicados |
|---|---|---|---|---|
| sub1 (PartidoA) | **10** | 0 | 0 | 0 |
| sub2 (PartidoA + PartidoB) | **20** | 0 | 0 | 0 |

Cada línea recibida tiene esta forma (`<OK>` = llegó en orden):
```
[14:03:21.512] PartidoA  P4821    #3    Cambio: jugador 10 entra por jugador 20 (min 10)  <OK>
```
Los logs completos (también los del broker y los publicadores) quedan en `logs/*_tcp.log`.
Opcional: `./scripts/demo.sh tcp 20 100` (20 mensajes por publicador, cada 100 ms).

### 4.2 Demo automática UDP
```bash
./scripts/demo.sh udp
```
**Resultado esperado:** igual que en TCP (10 y 20 mensajes). En loopback, sin red simulada, UDP casi nunca
pierde nada; las diferencias aparecen en las pruebas 4.5 y 4.6.

### 4.3 Ejecución manual (una terminal por proceso)
Sirve para ver cada componente en vivo. **Orden obligatorio: broker → suscriptores → publicadores**
(si un suscriptor se conecta tarde, se pierde los primeros mensajes y los marca como hueco).
```bash
./bin/broker_tcp                                            # terminal 1
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA                # terminal 2
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA PartidoB       # terminal 3
./bin/publisher_tcp  127.0.0.1 9300 PartidoA 10 500         # terminal 4  (tema, nº mensajes, ms entre mensajes)
./bin/publisher_tcp  127.0.0.1 9300 PartidoB 10 500         # terminal 5
```
Para UDP: los mismos comandos, cambiando `_tcp` por `_udp` y el puerto `9300` por `9301`.

**Resultado esperado:**
- El broker muestra cada `SUB` y cada `PUB ... -> N suscriptor(es)`.
- La terminal 2 recibe solo los mensajes de `PartidoA`; la terminal 3 recibe los de los dos partidos.
- Al hacer **Ctrl+C** en un suscriptor se imprime un **resumen** (recibidos, perdidos, desordenados,
  duplicados). Ctrl+C en el broker muestra el total de publicaciones y reenvíos.

### 4.4 Capturar el tráfico (Wireshark / tcpdump)
Abrir una terminal aparte e iniciar la captura **antes** de la demo, para que quede el handshake TCP.
Usar `-i lo` en Linux y `-i lo0` en macOS:
```bash
# Terminal A (Linux: lo | macOS: lo0)
sudo tcpdump -i lo -w captures/tcp_pubsub.pcap "tcp port 9300"
# Terminal B
./scripts/demo.sh tcp
# Terminal A: Ctrl+C cuando termine la demo
```
Lo mismo para UDP, con `-w captures/udp_pubsub.pcap "udp port 9301"` y `./scripts/demo.sh udp`.

Abrir el `.pcap` en Wireshark. **Qué verificar:**
- **TCP:** handshake de 3 vías (`tcp.flags.syn == 1`), un segmento con datos por mensaje (`PUB|...` / `MSG|...`
  en texto plano), sus ACK y el cierre con FIN.
- **UDP:** un datagrama por mensaje, sin handshake ni ACK.
- Overhead de cabeceras: TCP ≥ 20 B, UDP 8 B. En macOS la capa de enlace de `lo0` es *Null/Loopback* (4 B)
  en lugar de Ethernet (14 B); las cabeceras IP/TCP/UDP son las mismas.

Filtros útiles: `tcp.port == 9300`, `udp.port == 9301`, `tcp.analysis.retransmission`,
`tcp.analysis.out_of_order`. Menú *Statistics → Conversations / Flow Graph*.

### 4.5 Red con pérdida y desorden (TCP recupera, UDP no)
En loopback casi no se pierde nada, así que el script `netem.sh` simula una red mala. Requiere `sudo`
y detecta solo el sistema operativo: en Linux usa `tc netem` sobre `lo` y en macOS dummynet sobre `lo0`
(solo para los puertos 9300–9302).
```bash
sudo ./scripts/netem.sh on            # 10% pérdida, 50 ms de retraso, 25% reorden
sudo ./scripts/netem.sh status        # confirmar que quedó activo
./scripts/demo.sh udp 50 100
./scripts/demo.sh tcp 50 100
sudo ./scripts/netem.sh off           # ¡SIEMPRE desactivarlo al terminar!
```
**Resultado esperado:**
| | UDP | TCP |
|---|---|---|
| Mensajes recibidos (sub2) | **menos de 100** | **100** |
| Marcas en la salida | `<HUECO (faltaron N)>`, a veces `<DESORDENADO>` | todo `<OK>` |
| Resumen | Perdidos > 0 | Perdidos = 0 (en una captura se ven las retransmisiones) |

Parámetros propios: `sudo ./scripts/netem.sh on <perdida%> <retraso_ms> <jitter_ms> <reorden%>`.
En macOS, si `pf` no acepta reglas con probabilidad, el script avisa ("SIN reorden") y solo aplica
pérdida y retraso.

> Si los mensajes perdidos son los **últimos** de la serie, el suscriptor no detecta un hueco. En ese caso
> conviene comparar el "último seq visto" del resumen con la cantidad de mensajes enviados.

### 4.6 Suscriptor lento (control de flujo)
Un suscriptor lento con buffer de recepción pequeño, frente a un publicador rápido.
Dos terminales con el broker correspondiente ya corriendo (`./bin/broker_udp` / `./bin/broker_tcp`):
```bash
# UDP
RETRASO_MS=30 RCVBUF=2304 ./bin/subscriber_udp 127.0.0.1 9301 PartidoA     # terminal 1 (Ctrl+C al final)
./bin/publisher_udp 127.0.0.1 9301 PartidoA 200 5                          # terminal 2

# TCP
RETRASO_MS=30 RCVBUF=2304 ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA
./bin/publisher_tcp 127.0.0.1 9300 PartidoA 200 5
```
**Resultado esperado:** con **UDP** el buffer se llena y se pierden mensajes (aparecen huecos y el resumen
da Perdidos > 0). Con **TCP** llegan los 200, aunque más lento, porque TCP frena al emisor.

### 4.7 Caída del broker
Correr la ejecución manual (4.3) sin publicadores y matar el broker con **Ctrl+C**.

**Resultado esperado:**
- **TCP:** el suscriptor lo detecta de inmediato: `el broker cerro la conexion (recv devolvio 0)` y
  muestra su resumen.
- **UDP:** el suscriptor no se entera y sigue esperando (UDP no tiene conexión). Hay que cerrarlo con Ctrl+C.

### 4.8 CPU y memoria del broker (opcional)
```bash
./bin/broker_tcp &                                                             # broker en segundo plano
for i in $(seq 20); do ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA > /dev/null & done   # 20 suscriptores
./scripts/medir_broker.sh broker_tcp 15 > logs/cpu_tcp.csv &                   # mide 15 s
./bin/publisher_tcp 127.0.0.1 9300 PartidoA 1000 5
cat logs/cpu_tcp.csv                                                           # columnas: segundo,cpu_percent,rss_kb
pkill subscriber_tcp; pkill broker_tcp                                         # limpiar
```
Para UDP: igual, con `_udp` y el puerto `9301`.

---

## 5. Bono QUIC
QUIC incluye TLS 1.3, así que se usa la librería externa **picoquic** (con picotls + OpenSSL). La
justificación y la documentación de cada función usada están en [src/quic/README_QUIC.md](src/quic/README_QUIC.md).

```bash
./scripts/instalar_picoquic.sh        # una sola vez, tarda unos minutos (ver nota)
./scripts/generar_certificado.sh      # crea certs/cert.pem y certs/key.pem (autofirmado)
make quic                             # crea bin/broker_quic, publisher_quic, subscriber_quic
./scripts/demo.sh quic
```
`instalar_picoquic.sh` instala las dependencias del sistema y compila picotls y picoquic (versiones
fijas) dentro de `third_party/`:
- **Linux:** usa `apt` y pide `sudo` (instala `cmake`, `libssl-dev`, etc.).
- **macOS:** usa **Homebrew** (instala `cmake`, `pkg-config`, `openssl@3`). En Apple Silicon omite
  `picotls-fusion`, que es solo para x86; es normal ver ese aviso.

**Resultado esperado:** igual que en TCP: sub1 recibe **10** y sub2 recibe **20** mensajes, sin pérdidas.

Captura con llaves TLS, para que Wireshark pueda descifrar QUIC:
```bash
sudo tcpdump -i lo -w captures/quic_pubsub.pcap "udp port 9302"        # terminal A (macOS: -i lo0)
SSLKEYLOGFILE=$PWD/captures/quic_keys.log ./scripts/demo.sh quic       # terminal B
```
En Wireshark: *Preferences → Protocols → TLS → (Pre)-Master-Secret log filename* =
`captures/quic_keys.log`, y filtro `quic`.

---

## 6. Problemas frecuentes
| Problema | Solución |
|---|---|
| `bind: Address already in use` | Quedó un broker abierto: `pkill broker_tcp` / `pkill broker_udp` / `pkill broker_quic` |
| `Permission denied` al correr un script | `chmod +x scripts/*.sh` |
| `Falta bin/...` al correr `demo.sh` | No se compiló: `make` (o `make quic` para QUIC) |
| El suscriptor no recibe nada | Se inició después del publicador, o el tema está escrito distinto (distingue mayúsculas) |
| La salida muestra `HUECO` sin haber activado netem | Quedó netem activo de antes: `sudo ./scripts/netem.sh off` |
| `make: *** missing separator` | El Makefile perdió los tabs (p. ej. al copiarlo desde Windows): volver a bajarlo del repo |
| `/usr/bin/env: 'bash\r'` | Los scripts quedaron con fin de línea de Windows: `sed -i 's/\r$//' scripts/*.sh` (en macOS: `sed -i '' ...`) |
| Linux: `tc` da error con `netem` | `sudo apt install linux-modules-extra-$(uname -r)` y reiniciar |
| No aparece la interfaz para capturar | Linux: `-i lo`; macOS: `-i lo0`. Capturar con `tcpdump` y abrir el `.pcap` en Wireshark |
| `make quic`: "Falta picoquic" | Ejecutar antes `./scripts/instalar_picoquic.sh` |
| `broker_quic`: "No se pudo crear el contexto QUIC" | Falta el certificado: `./scripts/generar_certificado.sh` |
| macOS: `instalar_picoquic.sh` dice "Falta Homebrew" | Instalarlo desde https://brew.sh y repetir |

---

## 7. Referencia: estructura, protocolo y scripts

### Estructura
```
src/common/protocolo.h   formato de mensajes, parser y detector de huecos/desorden/duplicados (compartido)
src/tcp/                 broker_tcp.c  publisher_tcp.c  subscriber_tcp.c
src/udp/                 broker_udp.c  publisher_udp.c  subscriber_udp.c
src/quic/                (bono) broker/publisher/subscriber_quic.c + README_QUIC.md
scripts/                 demo.sh  netem.sh  medir_broker.sh  instalar_picoquic.sh  generar_certificado.sh
captures/                capturas .pcap (no se suben al repo)
logs/                    salida de demo.sh y medir_broker.sh
report/                  informe PDF
Makefile
```

### Protocolo (igual en TCP, UDP y QUIC)
Texto plano, un mensaje por línea terminada en `\n`:

| Dirección | Formato |
|---|---|
| Suscriptor → Broker | `SUB\|tema` |
| Publicador → Broker | `PUB\|tema\|id_publicador\|seq\|texto` |
| Broker → Suscriptor | `MSG\|tema\|id_publicador\|seq\|texto` |

- `seq` empieza en 1 para cada pareja (tema, publicador) y permite detectar **pérdidas, desorden y duplicados**.
- `id_publicador` es `P<pid>`.
- El broker reenvía el mensaje sin modificar su contenido.
- En UDP el suscriptor repite su `SUB` cada 5 s, por si el primero se perdió.

**Puertos:** TCP **9300**, UDP **9301**, QUIC **9302**.

### Programas y scripts
| Comando | Uso |
|---|---|
| `./bin/broker_<proto> [puerto]` | Broker (puerto por defecto según el protocolo) |
| `./bin/subscriber_<proto> <ip> <puerto> <tema1> [tema2 ...]` | Suscriptor; variables opcionales `RETRASO_MS`, `RCVBUF` |
| `./bin/publisher_<proto> <ip> <puerto> <tema> [n=10] [intervalo_ms=500]` | Publicador (`intervalo_ms=0` = ráfaga) |
| `./scripts/demo.sh <tcp\|udp\|quic> [n=10] [intervalo_ms=300]` | 1 broker + 2 suscriptores + 2 publicadores, con resumen |
| `sudo ./scripts/netem.sh on\|off\|status [...]` | Simula pérdida, retraso y reorden en loopback |
| `./scripts/medir_broker.sh <proceso> [segundos]` | CSV de CPU y memoria del broker, una muestra por segundo |
