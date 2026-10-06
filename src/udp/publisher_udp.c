/*
 * publisher_udp.c - Publicador (periodista) sobre UDP
 * Lab 3 - Grupo 9, Seccion 3
 *
 * COMANDOS:
 *   Compilar : make udp
 *              (o: gcc -Wall -Wextra -O2 -Isrc/common -o bin/publisher_udp src/udp/publisher_udp.c)
 *   Uso      : ./bin/publisher_udp <ip_broker> <puerto> <tema> [num_mensajes=10] [intervalo_ms=500]
 *   Ejemplos : ./bin/publisher_udp 127.0.0.1 9301 PartidoA
 *              ./bin/publisher_udp 127.0.0.1 9301 PartidoB 20 200
 *              ./bin/publisher_udp 127.0.0.1 9301 PartidoA 1000 0    # rafaga sin pausa
 *
 * FUNCIONAMIENTO
 *   socket(SOCK_DGRAM) -> sendto() de un datagrama "PUB|tema|id|seq|texto\n"
 *   por cada evento. No hay connect ni handshake: el mensaje sale y se olvida.
 *   Aunque el broker este apagado, sendto() no avisa de ningun error.
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
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes] [intervalo_ms]\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];
    int n = (argc > 4) ? atoi(argv[4]) : 10;
    int intervalo = (argc > 5) ? atoi(argv[5]) : 500;
    if (strlen(tema) >= MAX_TEMA) { fprintf(stderr, "Tema demasiado largo\n"); return 1; }

    instalar_senales();
    char id[MAX_ID];
    snprintf(id, sizeof id, "P%d", (int)getpid());

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }
    printf("[pub %s] enviando a %s:%d, tema '%s', %d mensajes\n", id, ip, puerto, tema, n);

    for (long seq = 1; seq <= n && !g_salir; seq++) {
        char texto[MAX_TEXTO], linea[TAM_LINEA];
        construir_evento(texto, sizeof texto, seq);
        int len = snprintf(linea, sizeof linea, "PUB|%s|%s|%ld|%s\n", tema, id, seq, texto);
        if (sendto(fd, linea, len, 0, (struct sockaddr *)&dir, sizeof dir) < 0) perror("sendto");
        printf("[pub %s] enviado #%ld: %s\n", id, seq, texto);
        if (intervalo > 0) dormir_ms(intervalo);
    }

    close(fd);
    printf("[pub %s] terminado\n", id);
    return 0;
}
