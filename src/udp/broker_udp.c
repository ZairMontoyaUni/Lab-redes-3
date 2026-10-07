/*
 * broker_udp.c - Broker publicador-suscriptor sobre UDP
 * Lab 3 - Grupo 9, Seccion 3
 *
 * COMANDOS (desde la raiz del repo; Linux o macOS):
 *   Compilar : make udp
 *              (o: gcc -Wall -Wextra -O2 -Isrc/common -o bin/broker_udp src/udp/broker_udp.c)
 *   Ejecutar : ./bin/broker_udp            # puerto 9301 por defecto
 *              ./bin/broker_udp 9301
 *   Capturar : sudo tcpdump -i lo -w captures/udp_pubsub.pcap "udp port 9301"
 *              (en macOS la interfaz es "lo0"; o Wireshark: interfaz loopback, filtro: udp port 9301)
 *   CPU/RAM  : ps -o pid,%cpu,rss,cmd -p $(pgrep -n broker_udp)
 *   Detener  : Ctrl+C (imprime estadisticas)
 *
 * FUNCIONAMIENTO
 *   - socket(SOCK_DGRAM) + bind. NO hay listen/accept: UDP no tiene conexion.
 *   - Un solo socket recibe TODO con recvfrom(), que entrega tambien la
 *     direccion (IP:puerto) de quien envio el datagrama.
 *   - SUB|tema -> el broker recuerda la direccion del suscriptor y su tema
 *     (los suscriptores repiten el SUB cada 5 s: asi se recuperan si se pierde).
 *   - PUB|tema|id|seq|txt -> sendto() de MSG|... a cada direccion suscrita.
 *   - Cada mensaje es un datagrama independiente: sin ACK, sin reintentos.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocolo.h"

#define MAX_SUBS 256

typedef struct {
    struct sockaddr_in addr;
    char temas[MAX_TEMAS_SUB][MAX_TEMA];
    int  ntemas;
    int  usado;
} Suscriptor;

static Suscriptor subs[MAX_SUBS];

static Suscriptor *buscar_o_crear(const struct sockaddr_in *a) {
    Suscriptor *libre = NULL;
    for (int i = 0; i < MAX_SUBS; i++) {
        if (subs[i].usado) {
            if (subs[i].addr.sin_addr.s_addr == a->sin_addr.s_addr && subs[i].addr.sin_port == a->sin_port)
                return &subs[i];
        } else if (!libre) libre = &subs[i];
    }
    if (libre) { memset(libre, 0, sizeof *libre); libre->addr = *a; libre->usado = 1; }
    return libre;
}

static int esta_suscrito(const Suscriptor *s, const char *tema) {
    for (int i = 0; i < s->ntemas; i++)
        if (strcmp(s->temas[i], tema) == 0) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    int puerto = (argc > 1) ? atoi(argv[1]) : PUERTO_UDP;
    long total_pub = 0, total_envios = 0;
    instalar_senales();

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_ANY);
    dir.sin_port = htons(puerto);
    if (bind(fd, (struct sockaddr *)&dir, sizeof dir) < 0) { perror("bind"); return 1; }
    printf("[broker] UDP escuchando en el puerto %d (Ctrl+C para salir)\n", puerto);

    char buf[TAM_LINEA];
    while (!g_salir) {
        struct sockaddr_in origen;
        socklen_t ol = sizeof origen;
        ssize_t r = recvfrom(fd, buf, sizeof buf - 1, 0, (struct sockaddr *)&origen, &ol);
        if (r < 0) { if (errno == EINTR) continue; perror("recvfrom"); break; }
        buf[r] = '\0';

        Mensaje m;
        if (!parsear_mensaje(buf, &m)) continue;

        if (strcmp(m.tipo, "SUB") == 0) {
            Suscriptor *s = buscar_o_crear(&origen);
            if (s && !esta_suscrito(s, m.tema) && s->ntemas < MAX_TEMAS_SUB) {
                snprintf(s->temas[s->ntemas++], MAX_TEMA, "%s", m.tema);
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &origen.sin_addr, ip, sizeof ip);
                printf("[broker] SUB  %s:%d -> tema '%s'\n", ip, ntohs(origen.sin_port), m.tema);
            }
        } else if (strcmp(m.tipo, "PUB") == 0) {
            char salida[TAM_LINEA + 8];
            int len = snprintf(salida, sizeof salida, "MSG|%s|%s|%ld|%s\n", m.tema, m.id, m.seq, m.texto);
            int enviados = 0;
            total_pub++;
            for (int i = 0; i < MAX_SUBS; i++) {
                if (!subs[i].usado || !esta_suscrito(&subs[i], m.tema)) continue;
                if (sendto(fd, salida, len, 0, (struct sockaddr *)&subs[i].addr, sizeof subs[i].addr) >= 0) {
                    enviados++;
                    total_envios++;
                }
            }
            printf("[broker] PUB  %s #%ld (%s) -> %d suscriptor(es)\n", m.tema, m.seq, m.id, enviados);
        }
    }

    printf("\n[broker] Publicaciones recibidas: %ld | Datagramas reenviados: %ld\n", total_pub, total_envios);
    close(fd);
    return 0;
}
