#define _DEFAULT_SOURCE
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "protocolo.h"

#define MAX_CLIENTES 1024

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  puerto;
    char buf[TAM_LINEA];
    int  len;
    char temas[MAX_TEMAS_SUB][MAX_TEMA];
    int  ntemas;
} Cliente;

static Cliente        clientes[MAX_CLIENTES];
static struct pollfd  fds[MAX_CLIENTES + 1];
static int            nclientes = 0;
static long           total_pub = 0, total_reenvios = 0;

static int esta_suscrito(const Cliente *c, const char *tema) {
    for (int i = 0; i < c->ntemas; i++)
        if (strcmp(c->temas[i], tema) == 0) return 1;
    return 0;
}

static void procesar_linea(int idx, char *linea) {
    Cliente *c = &clientes[idx];
    Mensaje m;

    if (!parsear_mensaje(linea, &m)) {
        printf("[broker] linea invalida de %s:%d\n", c->ip, c->puerto);
        return;
    }

    if (strcmp(m.tipo, "SUB") == 0) {
        if (!esta_suscrito(c, m.tema) && c->ntemas < MAX_TEMAS_SUB) {
            snprintf(c->temas[c->ntemas], MAX_TEMA, "%s", m.tema);
            c->ntemas++;
        }
        printf("[broker] SUB  %s:%d -> tema '%s'\n", c->ip, c->puerto, m.tema);
    } else if (strcmp(m.tipo, "PUB") == 0) {
        char salida[TAM_LINEA + 8];
        int len = snprintf(salida, sizeof salida, "MSG|%s|%s|%ld|%s\n", m.tema, m.id, m.seq, m.texto);
        int enviados = 0;
        total_pub++;
        for (int j = 0; j < nclientes; j++) {
            if (j == idx || !esta_suscrito(&clientes[j], m.tema)) continue;
            if (send(clientes[j].fd, salida, len, MSG_NOSIGNAL) >= 0) { enviados++; total_reenvios++; }
        }
        printf("[broker] PUB  %s #%ld (%s) -> %d suscriptor(es)\n", m.tema, m.seq, m.id, enviados);
    }
}

static void eliminar_cliente(int i) {
    printf("[broker] cliente %s:%d desconectado\n", clientes[i].ip, clientes[i].puerto);
    close(clientes[i].fd);
    clientes[i] = clientes[nclientes - 1];
    fds[i + 1]  = fds[nclientes];
    nclientes--;
}

static int leer_cliente(int i) {
    Cliente *c = &clientes[i];
    ssize_t r = recv(c->fd, c->buf + c->len, TAM_LINEA - c->len - 1, 0);
    if (r == 0) return -1;
    if (r < 0) return (errno == EINTR || errno == EAGAIN) ? 0 : -1;

    c->len += (int)r;
    char *ini = c->buf, *fin = c->buf + c->len, *nl;
    while ((nl = memchr(ini, '\n', fin - ini)) != NULL) {
        *nl = '\0';
        procesar_linea(i, ini);
        ini = nl + 1;
    }
    int resto = (int)(fin - ini);
    memmove(c->buf, ini, resto);
    c->len = resto;
    if (c->len >= TAM_LINEA - 1) c->len = 0;
    return 0;
}

static void aceptar_cliente(int srv) {
    struct sockaddr_in ca;
    socklen_t cl = sizeof ca;
    int fd = accept(srv, (struct sockaddr *)&ca, &cl);
    if (fd < 0) return;
    if (nclientes >= MAX_CLIENTES) { close(fd); return; }

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

    Cliente *c = &clientes[nclientes];
    memset(c, 0, sizeof *c);
    c->fd = fd;
    inet_ntop(AF_INET, &ca.sin_addr, c->ip, sizeof c->ip);
    c->puerto = ntohs(ca.sin_port);
    fds[nclientes + 1].fd = fd;
    fds[nclientes + 1].events = POLLIN;
    fds[nclientes + 1].revents = 0;
    nclientes++;
    printf("[broker] cliente conectado %s:%d (total %d)\n", c->ip, c->puerto, nclientes);
}

int main(int argc, char *argv[]) {
    int puerto = (argc > 1) ? atoi(argv[1]) : PUERTO_TCP;
    instalar_senales();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_ANY);
    dir.sin_port = htons(puerto);
    if (bind(srv, (struct sockaddr *)&dir, sizeof dir) < 0) { perror("bind"); return 1; }
    if (listen(srv, 128) < 0) { perror("listen"); return 1; }

    fds[0].fd = srv;
    fds[0].events = POLLIN;
    printf("[broker] TCP escuchando en el puerto %d (Ctrl+C para salir)\n", puerto);

    while (!g_salir) {
        int r = poll(fds, nclientes + 1, -1);
        if (r < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        for (int i = 0; i < nclientes; ) {
            if (fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (leer_cliente(i) < 0) { eliminar_cliente(i); continue; }
            }
            i++;
        }
        if (fds[0].revents & POLLIN) aceptar_cliente(srv);
    }

    printf("\n[broker] Publicaciones recibidas: %ld | Reenvios realizados: %ld\n", total_pub, total_reenvios);
    for (int i = 0; i < nclientes; i++) close(clientes[i].fd);
    close(srv);
    return 0;
}
