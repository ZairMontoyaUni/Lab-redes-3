#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include "quic_comun.h"

typedef struct Conexion {
    struct Conexion *sig;
    struct Conexion *ant;
    picoquic_cnx_t  *cnx;
    int              num;
    uint64_t         stream_id;
    int              tiene_stream;
    char             buf[TAM_LINEA];
    int              len;
    char             temas[MAX_TEMAS_SUB][MAX_TEMA];
    int              ntemas;
} Conexion;

static Conexion *g_lista = NULL;
static int  g_total_conexiones = 0;
static long g_total_pub = 0, g_total_reenvios = 0;

static int esta_suscrito(const Conexion *c, const char *tema) {
    for (int i = 0; i < c->ntemas; i++)
        if (strcmp(c->temas[i], tema) == 0) return 1;
    return 0;
}

static void procesar_linea(Conexion *c, uint64_t stream_id, char *linea) {
    Mensaje m;
    if (!parsear_mensaje(linea, &m)) {
        printf("[broker] linea invalida de la conexion #%d\n", c->num);
        return;
    }

    if (strcmp(m.tipo, "SUB") == 0) {
        c->stream_id = stream_id;
        c->tiene_stream = 1;
        if (!esta_suscrito(c, m.tema) && c->ntemas < MAX_TEMAS_SUB) {
            snprintf(c->temas[c->ntemas], MAX_TEMA, "%s", m.tema);
            c->ntemas++;
        }
        printf("[broker] SUB  conexion #%d (stream %llu) -> tema '%s'\n",
               c->num, (unsigned long long)stream_id, m.tema);
    } else if (strcmp(m.tipo, "PUB") == 0) {
        char salida[TAM_LINEA + 8];
        int len = snprintf(salida, sizeof salida, "MSG|%s|%s|%ld|%s\n", m.tema, m.id, m.seq, m.texto);
        int enviados = 0;
        g_total_pub++;
        for (Conexion *d = g_lista; d != NULL; d = d->sig) {
            if (d == c || !d->tiene_stream || !esta_suscrito(d, m.tema)) continue;
            if (picoquic_add_to_stream(d->cnx, d->stream_id, (const uint8_t *)salida, (size_t)len, 0) == 0) {
                enviados++;
                g_total_reenvios++;
            }
        }
        printf("[broker] PUB  %s #%ld (%s) -> %d suscriptor(es)\n", m.tema, m.seq, m.id, enviados);
    }
}

static void procesar_buffer(Conexion *c, uint64_t stream_id) {
    char *ini = c->buf, *fin = c->buf + c->len, *nl;
    while ((nl = memchr(ini, '\n', (size_t)(fin - ini))) != NULL) {
        *nl = '\0';
        procesar_linea(c, stream_id, ini);
        ini = nl + 1;
    }
    int resto = (int)(fin - ini);
    memmove(c->buf, ini, (size_t)resto);
    c->len = resto;
}

static void quitar_conexion(Conexion *c) {
    if (c->ant) c->ant->sig = c->sig; else g_lista = c->sig;
    if (c->sig) c->sig->ant = c->ant;
    printf("[broker] conexion #%d cerrada\n", c->num);
    free(c);
}

static int callback_broker(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes, size_t length,
                           picoquic_call_back_event_t evento, void *ctx_cb, void *ctx_stream)
{
    (void)ctx_stream;
    Conexion *c = (Conexion *)ctx_cb;

    if (c == NULL || ctx_cb == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        c = calloc(1, sizeof *c);
        if (c == NULL) { picoquic_close(cnx, PICOQUIC_ERROR_MEMORY); return -1; }
        c->cnx = cnx;
        c->num = ++g_total_conexiones;
        c->sig = g_lista;
        if (g_lista) g_lista->ant = c;
        g_lista = c;
        picoquic_set_callback(cnx, callback_broker, c);
        printf("[broker] nueva conexion QUIC #%d\n", c->num);
    }

    switch (evento) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin: {
        size_t off = 0;
        while (off < length) {
            size_t libre = sizeof c->buf - 1 - (size_t)c->len;
            size_t n = (length - off < libre) ? (length - off) : libre;
            memcpy(c->buf + c->len, bytes + off, n);
            c->len += (int)n;
            off += n;
            procesar_buffer(c, stream_id);
            if (c->len >= (int)sizeof c->buf - 1) c->len = 0;
        }
        break;
    }
    case picoquic_callback_close:
    case picoquic_callback_application_close:
    case picoquic_callback_stateless_reset:
        quitar_conexion(c);
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo, void *ctx, void *arg) {
    (void)quic; (void)ctx;
    if (modo == picoquic_packet_loop_ready) {
        ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
    } else if (modo == picoquic_packet_loop_time_check) {
        if (g_salir) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int puerto = (argc > 1) ? atoi(argv[1]) : PUERTO_QUIC;
    const char *cert = (argc > 2) ? argv[2] : "certs/cert.pem";
    const char *llave = (argc > 3) ? argv[3] : "certs/key.pem";
    instalar_senales();

    picoquic_quic_t *quic = picoquic_create(256, cert, llave, NULL, ALPN_PUBSUB,
        callback_broker, NULL, NULL, NULL, NULL, picoquic_current_time(), NULL, NULL, NULL, 0);
    if (quic == NULL) {
        fprintf(stderr, "No se pudo crear el contexto QUIC. Revisa que existan %s y %s "
                        "(./scripts/generar_certificado.sh)\n", cert, llave);
        return 1;
    }
    picoquic_set_default_idle_timeout(quic, IDLE_TIMEOUT_MS);
    picoquic_set_key_log_file_from_env(quic);

    printf("[broker] QUIC escuchando en el puerto UDP %d (Ctrl+C para salir)\n", puerto);
    int ret = picoquic_packet_loop(quic, puerto, AF_INET, 0, 0, 0, loop_cb, NULL);

    printf("\n[broker] Publicaciones recibidas: %ld | Reenvios realizados: %ld | Conexiones: %d\n",
           g_total_pub, g_total_reenvios, g_total_conexiones);
    (void)ret;
    picoquic_free(quic);
    return 0;
}
