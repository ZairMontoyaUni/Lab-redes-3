# Lab 3 - Sockets y Capa de Transporte (Grupo 9, Sección 3)
ISIS2311L - Redes y Comunicaciones - Universidad de los Andes

Sistema de noticias deportivas en tiempo real con el modelo **publicación–suscripción**
(publicadores = periodistas, broker = canal central, suscriptores = hinchas), implementado
en **C** con sockets **TCP** y **UDP** (y **QUIC** como bono).

---

## 1. Configurar la máquina virtual (Windows → Ubuntu)

Todo el laboratorio corre **dentro de una sola VM Linux**: broker, publicadores y suscriptores
en terminales distintas usando `127.0.0.1`. No necesitas red de celular ni una VM por persona.

### Paso 1. Preparar Windows
1. Entra a la BIOS/UEFI y verifica que la virtualización (Intel VT-x / AMD-V / SVM) esté **activada**.
   Para comprobarlo: Administrador de tareas → Rendimiento → CPU → "Virtualización: Habilitada".
2. Necesitas libres al menos **4 GB de RAM para la VM y 30 GB de disco**.

### Paso 2. Instalar VirtualBox y descargar Ubuntu
1. Instala **VirtualBox** desde https://www.virtualbox.org/wiki/Downloads (Windows hosts).
2. Descarga la ISO de **Ubuntu Desktop 24.04 LTS** desde https://ubuntu.com/download/desktop.

### Paso 3. Crear la VM
1. VirtualBox → **Nueva**. Nombre: `Lab3-Redes`. Selecciona la ISO. Tipo: Linux / Ubuntu (64-bit).
2. Memoria: **4096 MB** (mínimo 2048). Procesadores: **2**. Disco: **30 GB** (dinámico).
3. Configuración → Red: deja **NAT** (el tráfico del lab es interno, no necesita más).
4. Configuración → General → Avanzado: **Portapapeles compartido: Bidireccional**.
5. Inicia la VM e instala Ubuntu (instalación normal o mínima, borrando el disco *virtual*).
6. Reinicia, inicia sesión y abre una terminal (`Ctrl+Alt+T`).

> Si VirtualBox va muy lento o no arranca, puede ser por Hyper-V/WSL2 activos en Windows.
> Alternativa: usar **WSL2** (`wsl --install -d Ubuntu`), pero entonces captura con `tcpdump`
> dentro de WSL y abre el `.pcap` en Wireshark de Windows.

### Paso 4. Instalar herramientas dentro de Ubuntu
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential git tcpdump wireshark iproute2 openssl \
                    linux-modules-extra-$(uname -r)
```
Cuando pregunte *"¿Debería permitirse a usuarios no root capturar paquetes?"* responde **Sí**.
Si no te lo preguntó:
```bash
sudo dpkg-reconfigure wireshark-common     # elegir <Sí>
```
Luego agrégate al grupo y **cierra sesión y vuelve a entrar** (o reinicia la VM):
```bash
sudo usermod -aG wireshark $USER
```
Verifica con `groups` (debe aparecer `wireshark`) y `gcc --version`.

### Paso 5. Configurar Git y acceso a GitHub
```bash
git config --global user.name  "Tu Nombre"
git config --global user.email "tu_correo@uniandes.edu.co"

# Llave SSH (Enter a todo)
ssh-keygen -t ed25519 -C "tu_correo@uniandes.edu.co"
cat ~/.ssh/id_ed25519.pub
```
Copia la salida y pégala en GitHub → *Settings → SSH and GPG keys → New SSH key*. Prueba: `ssh -T git@github.com`.

### Paso 6. Clonar el repo y compilar
```bash
git clone git@github.com:USUARIO/REPO.git lab3-g9-s3
cd lab3-g9-s3
make                 # compila TCP y UDP en bin/
ls bin               # deben aparecer 6 ejecutables
```

### Paso 7. Verificar que todo funciona (prueba de humo)
```bash
./scripts/demo.sh tcp          # debe terminar con "Mensajes recibidos: 20"
./scripts/demo.sh udp
sudo tc qdisc add dev lo root netem delay 1ms && sudo tc qdisc del dev lo root   # prueba de netem (sin error = ok)
```
Abre **Wireshark** (`wireshark` en la terminal), haz doble clic en **Loopback: lo** y comprueba que ves tráfico mientras corres un demo.

### Paso 8. Sacar archivos de la VM (capturas, pantallazos)
- Pantallazos: tecla `Impr Pant` (se guardan en *Imágenes*). Súbelos desde el navegador de la VM a Drive.
- `.pcap`: sube desde el navegador de la VM a Google Drive/GitHub y pon el enlace en el informe, o usa
  VirtualBox → Dispositivos → Carpetas compartidas.

---

## 1b. Alternativa: correr todo en macOS (sin VM)
Los programas compilan y corren igual en macOS. Cambian solo tres cosas:

| | Linux | macOS |
|---|---|---|
| Interfaz loopback | `lo` | **`lo0`** (en todos los comandos `tcpdump -i`) |
| Red mala | `tc` + netem | dummynet (`dnctl` + `pfctl`); `scripts/netem.sh` lo detecta solo |
| Herramientas | `apt install ...` | `xcode-select --install` y `brew install --cask wireshark` |

En macOS `netem.sh` solo afecta los puertos 9300–9302 de `lo0` y deja `pf` encendido con las reglas
por defecto del sistema al hacer `off`. En las capturas de `lo0` la cabecera de enlace es *Null/Loopback*
(4 B) en vez de Ethernet (14 B); las cabeceras IP, TCP y UDP son las mismas.

---

## 2. Estructura del repo
```
src/common/protocolo.h     formato de mensajes, parser, detector de huecos/desorden (compartido)
src/tcp/                   broker_tcp.c  publisher_tcp.c  subscriber_tcp.c
src/udp/                   broker_udp.c  publisher_udp.c  subscriber_udp.c
src/quic/                  (bono) broker/publisher/subscriber_quic.c + README_QUIC.md (librería picoquic documentada)
scripts/                   demo.sh  netem.sh  medir_broker.sh  instalar_picoquic.sh  generar_certificado.sh
captures/                  .pcap (subir a Drive; los .pcap están en .gitignore)
report/                    informe PDF
Makefile
```

## 3. Protocolo de mensajes (igual en TCP, UDP y QUIC)
Texto plano, un mensaje por línea terminada en `\n`:

| Dirección | Formato |
|---|---|
| Suscriptor → Broker | `SUB\|tema` |
| Publicador → Broker | `PUB\|tema\|id_publicador\|seq\|texto` |
| Broker → Suscriptor | `MSG\|tema\|id_publicador\|seq\|texto` |

`seq` empieza en 1 por cada (tema, publicador). Sirve para detectar **pérdidas, desorden y duplicados**.
Puertos: **TCP 9300 · UDP 9301 · QUIC 9302**.

## 4. Ejecución manual (mínimo pedido: 1 broker, 2 suscriptores, 2 publicadores)
Una terminal por proceso. **Inicia primero broker, luego suscriptores, luego publicadores.**
```bash
# --- TCP ---
./bin/broker_tcp                                            # terminal 1
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA                # terminal 2
./bin/subscriber_tcp 127.0.0.1 9300 PartidoA PartidoB       # terminal 3
./bin/publisher_tcp  127.0.0.1 9300 PartidoA 10 500         # terminal 4  (tema, nº mensajes, ms entre mensajes)
./bin/publisher_tcp  127.0.0.1 9300 PartidoB 10 500         # terminal 5

# --- UDP --- (igual, cambiando _tcp por _udp y el puerto por 9301)
```
O todo automático: `./scripts/demo.sh tcp` / `./scripts/demo.sh udp 20 100` (logs en `logs/`).

## 5. Capturar con Wireshark / tcpdump
Inicia la captura **antes** de lanzar los programas (para ver el handshake). En macOS usa `-i lo0`.
```bash
sudo tcpdump -i lo -w captures/tcp_pubsub.pcap "tcp port 9300"     # Ctrl+C al terminar
sudo tcpdump -i lo -w captures/udp_pubsub.pcap "udp port 9301"
```
Filtros de visualización útiles en Wireshark:
`tcp.port == 9300` · `udp.port == 9301` · `tcp.flags.syn == 1` · `tcp.analysis.retransmission` ·
`tcp.analysis.out_of_order` · `quic`. Menú *Statistics → Conversations / I/O Graphs / Flow Graph*.
Para el overhead: abre un paquete y compara la cabecera TCP (≥20 B) con la UDP (8 B).

## 6. Provocar pérdida y desorden (para que el análisis tenga evidencia)
En loopback casi nunca se pierde nada, por eso se simula una red mala:
```bash
sudo ./scripts/netem.sh on            # 10% pérdida, 50 ms de retraso, 25% reorden (Linux y macOS)
./scripts/demo.sh udp 50 100          # UDP: aparecen HUECO / DESORDENADO
./scripts/demo.sh tcp 50 100          # TCP: llega todo; en Wireshark verás retransmisiones
sudo ./scripts/netem.sh off           # ¡SIEMPRE quitarlo al terminar!
```
Otra prueba (control de flujo / buffer lleno), con un suscriptor lento y publicador rápido:
```bash
RETRASO_MS=30 RCVBUF=2304 ./bin/subscriber_udp 127.0.0.1 9301 PartidoA     # UDP pierde mensajes
./bin/publisher_udp 127.0.0.1 9301 PartidoA 200 5
# (mismo experimento con _tcp: no se pierde nada, TCP frena al emisor)
```
Nota: si los mensajes perdidos son los **últimos** de la serie, el suscriptor no ve "hueco"; compara
el "último seq visto" del resumen con los mensajes que envió el publicador.

## 7. Medir CPU y memoria del broker
```bash
./scripts/medir_broker.sh broker_tcp 30 > logs/cpu_tcp.csv      # con el broker ya corriendo
for i in $(seq 20); do ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA > /dev/null & done   # 20 suscriptores
pkill subscriber_tcp                                            # detenerlos
```
Prueba también **matar el broker** (Ctrl+C) con suscriptores conectados: en TCP el suscriptor lo detecta
(`recv` devuelve 0); en UDP no se entera.

## 8. Problemas frecuentes
| Problema | Solución |
|---|---|
| `bind: Address already in use` | Quedó un broker abierto: `pkill broker_tcp` / `pkill broker_udp` |
| Wireshark no deja capturar | Falta el grupo: `sudo usermod -aG wireshark $USER` y reiniciar sesión |
| No aparece la interfaz `lo` | Usa `sudo tcpdump -i lo ...` (macOS: `-i lo0`) y abre el `.pcap` en Wireshark |
| `make quic`: "Falta picoquic" | Ejecuta `./scripts/instalar_picoquic.sh` primero |
| `make: *** missing separator` | El Makefile perdió sus tabs; vuelve a bajarlo del repo (`git checkout Makefile`) |
| `tc: ... netem` da error | `sudo apt install linux-modules-extra-$(uname -r)` y reinicia |
| Suscriptor no recibe nada | Se inició después del publicador, o el tema está escrito distinto (distingue mayúsculas) |
| Scripts: `Permission denied` | `chmod +x scripts/*.sh` |

## 9. Bono QUIC (librería picoquic)
Necesita una librería externa (QUIC incluye TLS 1.3); la justificación y cada función usada están en
`src/quic/README_QUIC.md`. Pasos (Ubuntu o macOS; en macOS el script usa Homebrew y en Apple Silicon omite `picotls-fusion`, que es solo x86):
```bash
./scripts/instalar_picoquic.sh        # una sola vez (unos minutos): compila picotls + picoquic en third_party/
./scripts/generar_certificado.sh      # certs/cert.pem y certs/key.pem
make quic
./scripts/demo.sh quic                # debe terminar con "Mensajes recibidos: 20" en el suscriptor 2
```
Para mostrarlo en Wireshark (puerto UDP 9302, filtro `quic`), con llaves para descifrar:
```bash
sudo tcpdump -i lo -w captures/quic_pubsub.pcap "udp port 9302"          # terminal 1 (macOS: -i lo0)
SSLKEYLOGFILE=$PWD/captures/quic_keys.log ./scripts/demo.sh quic         # terminal 2
```
Wireshark → Edit → Preferences → Protocols → TLS → *(Pre)-Master-Secret log filename* = `captures/quic_keys.log`.

## 10. Flujo de trabajo en equipo (GitHub)
Rama por tarea (`feat/udp`, `feat/quic`), Pull Request hacia `main` con revisión de otro integrante,
un Issue por tarea en el tablero del repo. Los `.pcap` van a Drive (enlace en el informe), no al repo.
