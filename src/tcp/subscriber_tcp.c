/*
 * subscriber_tcp.c - Suscriptor (hincha) sobre TCP
 * Lab 3 - Grupo 9, Seccion 3
 *
 * COMANDOS:
 *   Compilar : make tcp
 *              (o: gcc -Wall -Wextra -O2 -Isrc/common -o bin/subscriber_tcp src/tcp/subscriber_tcp.c)
 *   Uso      : ./bin/subscriber_tcp <ip_broker> <puerto> <tema1> [tema2 ...]
 *   Ejemplos : ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA
 *              ./bin/subscriber_tcp 127.0.0.1 9300 PartidoA PartidoB
 *   Variables de entorno opcionales (para provocar problemas y analizarlos):
 *              RETRASO_MS=200 ./bin/subscriber_tcp ...   # suscriptor LENTO (prueba de control de flujo)
 *              RCVBUF=2048    ./bin/subscriber_tcp ...   # buffer de recepcion pequeno
 *   Detener  : Ctrl+C (imprime el resumen: recibidos, perdidos, desordenados)
 *
 * IMPORTANTE: iniciar los suscriptores ANTES que los publicadores.
 *
 * FUNCIONAMIENTO
 *   connect() -> send("SUB|tema\n") por cada tema -> recv() en bucle.
 *   Acumula bytes y procesa por lineas (TCP no respeta fronteras de mensaje).
 *   Verifica el numero de secuencia de cada mensaje para comprobar el orden.
 *   Si recv() devuelve 0, el broker cerro la conexion (se detecta la caida).
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocolo.h"

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

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    if (rcvbuf > 0) setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }
    if (connect(fd, (struct sockaddr *)&dir, sizeof dir) < 0) { perror("connect"); return 1; }

    for (int i = 3; i < argc; i++) {
        char linea[TAM_LINEA];
        int len = snprintf(linea, sizeof linea, "SUB|%s\n", argv[i]);
        if (send(fd, linea, len, MSG_NOSIGNAL) < 0) { perror("send"); return 1; }
        printf("[sub] suscrito al tema '%s'\n", argv[i]);
    }

    char buf[TAM_LINEA * 4];
    int len = 0;
    while (!g_salir) {
        ssize_t k = recv(fd, buf + len, sizeof buf - len - 1, 0);
        if (k < 0) { if (errno == EINTR) continue; perror("recv"); break; }
        if (k == 0) { printf("[sub] el broker cerro la conexion (recv devolvio 0)\n"); break; }
        len += (int)k;

        char *ini = buf, *fin = buf + len, *nl;
        while ((nl = memchr(ini, '\n', fin - ini)) != NULL) {
            *nl = '\0';
            Mensaje m;
            long faltan = 0;
            if (parsear_mensaje(ini, &m) && strcmp(m.tipo, "MSG") == 0) {
                int est = rastrear(&r, &m, &faltan);
                reportar_mensaje(&m, est, faltan);
                if (retraso_ms > 0) dormir_ms(retraso_ms);
            }
            ini = nl + 1;
        }
        int resto = (int)(fin - ini);
        memmove(buf, ini, resto);
        len = resto;
        if (len >= (int)sizeof buf - 1) len = 0;
    }

    imprimir_resumen(&r, "TCP");
    close(fd);
    return 0;
}
