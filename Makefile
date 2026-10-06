# Makefile - Lab 3 (Grupo 9, Seccion 3)
# Comandos:
#   make          -> compila TCP y UDP en bin/
#   make tcp      -> solo TCP
#   make udp      -> solo UDP
#   make clean    -> borra bin/
# (la version QUIC del bono se agregara cuando se defina la libreria)

CC     = gcc
CFLAGS = -Wall -Wextra -O2 -g -Isrc/common
BIN    = bin

TCP = $(BIN)/broker_tcp $(BIN)/publisher_tcp $(BIN)/subscriber_tcp
UDP = $(BIN)/broker_udp $(BIN)/publisher_udp $(BIN)/subscriber_udp

all: tcp udp
tcp: $(TCP)
udp: $(UDP)

$(BIN)/%_tcp: src/tcp/%_tcp.c src/common/protocolo.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/%_udp: src/udp/%_udp.c src/common/protocolo.h | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN):
	mkdir -p $(BIN)

clean:
	rm -rf $(BIN)

.PHONY: all tcp udp clean
