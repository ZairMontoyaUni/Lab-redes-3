/*
 * quic_comun.h - Definiciones comunes de la version QUIC (BONO)
 * Lab 3 - Grupo 9, Seccion 3
 *
 * Esta version usa la libreria externa picoquic (+ picotls para TLS 1.3).
 * Cada funcion de picoquic usada esta documentada punto a punto en
 * src/quic/README_QUIC.md (requisito de la guia, seccion 6.2).
 *
 * Decisiones de diseno:
 *  - Cada cliente (publicador o suscriptor) abre UNA conexion QUIC al broker
 *    y usa UN stream bidireccional (el primero iniciado por el cliente: id 0).
 *  - Sobre ese stream viaja el MISMO protocolo de texto que en TCP y UDP
 *    (SUB| / PUB| / MSG|, una linea por mensaje terminada en '\n').
 *    Un stream QUIC es un flujo de bytes como TCP, por eso tambien se acumula
 *    en un buffer y se procesa por lineas.
 */
#ifndef QUIC_COMUN_H
#define QUIC_COMUN_H

#include <stdint.h>
#include <picoquic.h>
#include <picoquic_utils.h>
#include <picoquic_packet_loop.h>
#include "protocolo.h"

#define ALPN_PUBSUB        "pubsub-lab3"   /* identificador de protocolo de aplicacion (TLS ALPN) */
#define SNI_DEFECTO        "localhost"
#define IDLE_TIMEOUT_MS    10000           /* un par inactivo se declara caido tras 10 s */

#endif /* QUIC_COMUN_H */
