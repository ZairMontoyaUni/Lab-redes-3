/*
 * subscriber_udp.c - Suscriptor (hincha) sobre UDP
 * Lab 3 - Grupo 9, Seccion 3
 *
 * COMANDOS:
 *   Compilar : make udp
 *              (o: gcc -Wall -Wextra -O2 -Isrc/common -o bin/subscriber_udp src/udp/subscriber_udp.c)
 *   Uso      : ./bin/subscriber_udp <ip_broker> <puerto> <tema1> [tema2 ...]
 *   Ejemplos : ./bin/subscriber_udp 127.0.0.1 9301 PartidoA
 *              ./bin/subscriber_udp 127.0.0.1 9301 PartidoA PartidoB
 *   Variables de entorno opcionales (para provocar perdidas y analizarlas):
 *              RETRASO_MS=200 ./bin/subscriber_udp ...   # suscriptor LENTO: se llena el buffer y UDP pierde
 *              RCVBUF=2048    ./bin/subscriber_udp ...   # buffer de recepcion pequeno
 *   Detener  : Ctrl+C (imprime el resumen: recibidos, perdidos, desordenados)
 *
 * IMPORTANTE: iniciar los suscriptores ANTES que los publicadores.
 *
 * FUNCIONAMIENTO
 *   socket(SOCK_DGRAM) -> sendto("SUB|tema\n") al broker (el SO le asigna un
 *   puerto local) -> recvfrom() en bucle por ese mismo socket.
 *   Como un datagrama SUB tambien puede perderse, se repite cada 5 s
 *   (timeout de recepcion SO_RCVTIMEO); el broker ignora los repetidos.
 *   Con el numero de secuencia se marcan HUECO (perdido), DESORDENADO y DUPLICADO.
 *   Si el broker se cae, este programa NO se entera: simplemente deja de recibir.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocolo.h"

static void registrar(int fd, const struct sockaddr_in *broker, int argc, char *argv[]) {
    for (int i = 3; i < argc; i++) {
        char linea[TAM_LINEA];
        int len = snprintf(linea, sizeof linea, "SUB|%s\n", argv[i]);
        sendto(fd, linea, len, 0, (const struct sockaddr *)broker, sizeof *broker);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int puerto = atoi(argv[2]);
    int retraso_ms = env_int("RETRASO_MS", 0);
    int rcvbuf = env_int("RCVBUF", 0);

    instalar_senales();
    Rastreador r;
    memset(&r, 0, sizeof r);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    if (rcvbuf > 0) setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    struct timeval tv = { 5, 0 };                      /* cada 5 s sin trafico -> reenviar SUB */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    struct sockaddr_in broker;
    memset(&broker, 0, sizeof broker);
    broker.sin_family = AF_INET;
    broker.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &broker.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }

    registrar(fd, &broker, argc, argv);
    printf("[sub] SUB enviado al broker %s:%d para %d tema(s)\n", ip, puerto, argc - 3);

    char buf[TAM_LINEA];
    while (!g_salir) {
        ssize_t k = recvfrom(fd, buf, sizeof buf - 1, 0, NULL, NULL);
        if (k < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) { registrar(fd, &broker, argc, argv); continue; }
            perror("recvfrom");
            break;
        }
        buf[k] = '\0';

        Mensaje m;
        long faltan = 0;
        if (parsear_mensaje(buf, &m) && strcmp(m.tipo, "MSG") == 0) {
            int est = rastrear(&r, &m, &faltan);
            reportar_mensaje(&m, est, faltan);
            if (retraso_ms > 0) dormir_ms(retraso_ms);
        }
    }

    imprimir_resumen(&r, "UDP");
    close(fd);
    return 0;
}
