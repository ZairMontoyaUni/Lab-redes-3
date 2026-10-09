#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "protocolo.h"

static int enviar_todo(int fd, const char *datos, int len) {
    int enviado = 0;
    while (enviado < len) {
        ssize_t r = send(fd, datos + enviado, len - enviado, MSG_NOSIGNAL);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        enviado += (int)r;
    }
    return 0;
}

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

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }
    if (connect(fd, (struct sockaddr *)&dir, sizeof dir) < 0) { perror("connect"); return 1; }
    printf("[pub %s] conectado a %s:%d, tema '%s', %d mensajes\n", id, ip, puerto, tema, n);

    for (long seq = 1; seq <= n && !g_salir; seq++) {
        char texto[MAX_TEXTO], linea[TAM_LINEA];
        construir_evento(texto, sizeof texto, seq);
        int len = snprintf(linea, sizeof linea, "PUB|%s|%s|%ld|%s\n", tema, id, seq, texto);
        if (enviar_todo(fd, linea, len) < 0) { perror("send"); break; }
        printf("[pub %s] enviado #%ld: %s\n", id, seq, texto);
        if (intervalo > 0) dormir_ms(intervalo);
    }

    close(fd);
    printf("[pub %s] terminado\n", id);
    return 0;
}
