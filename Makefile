# Makefile - Lab 3 (Grupo 9, Seccion 3)
# Comandos:
#   make          -> compila TCP y UDP en bin/
#   make tcp      -> solo TCP
#   make udp      -> solo UDP
#   make quic     -> BONO QUIC (requiere ./scripts/instalar_picoquic.sh antes)
#   make clean    -> borra bin/

CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -Isrc/common
BIN    = bin

TCP = $(BIN)/broker_tcp $(BIN)/publisher_tcp $(BIN)/subscriber_tcp
UDP = $(BIN)/broker_udp $(BIN)/publisher_udp $(BIN)/subscriber_udp
QUIC = $(BIN)/broker_quic $(BIN)/publisher_quic $(BIN)/subscriber_quic

# Rutas de la libreria externa picoquic/picotls (las deja instalar_picoquic.sh)
TP         = third_party
QUIC_INC   = -Isrc/quic -I$(TP)/picoquic/picoquic -I$(TP)/picotls/include
QUIC_LIBS  = $(TP)/picoquic/build/libpicoquic-core.a \
             $(TP)/picotls/build/libpicotls-openssl.a \
             $(TP)/picotls/build/libpicotls-core.a \
             $(TP)/picotls/build/libpicotls-fusion.a \
             $(TP)/picotls/build/libpicotls-minicrypto.a \
             -lssl -lcrypto -lpthread -lm

all: tcp udp
tcp: $(TCP)
udp: $(UDP)
quic: $(QUIC)

$(BIN)/%_tcp: src/tcp/%_tcp.c src/common/protocolo.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/%_udp: src/udp/%_udp.c src/common/protocolo.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/%_quic: src/quic/%_quic.c src/quic/quic_comun.h src/common/protocolo.h | $(BIN)
	@test -f $(TP)/picoquic/build/libpicoquic-core.a || { echo "Falta picoquic: ejecuta ./scripts/instalar_picoquic.sh"; exit 1; }
	$(CC) $(CFLAGS) $(QUIC_INC) -o $@ $< $(QUIC_LIBS)

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)

.PHONY: all tcp udp quic clean
