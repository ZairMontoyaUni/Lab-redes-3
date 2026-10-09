#define _DEFAULT_SOURCE
#include <unistd.h>
#include <arpa/inet.h>
#include "quic_comun.h"

typedef struct {
    picoquic_cnx_t *cnx;
    uint64_t stream_id;
    int      ntemas;
    char   **temas;
    char     buf[TAM_LINEA * 4];
    int      len;
    int      retraso_ms;
    int      desconectado;
    Rastreador r;
} SubCtx;

static void procesar_buffer(SubCtx *s) {
    char *ini = s->buf, *fin = s->buf + s->len, *nl;
    while ((nl = memchr(ini, '\n', (size_t)(fin - ini))) != NULL) {
        *nl = '\0';
        Mensaje m;
        long faltan = 0;
        if (parsear_mensaje(ini, &m) && strcmp(m.tipo, "MSG") == 0) {
            int est = rastrear(&s->r, &m, &faltan);
            reportar_mensaje(&m, est, faltan);
            if (s->retraso_ms > 0) dormir_ms(s->retraso_ms);
        }
        ini = nl + 1;
    }
    int resto = (int)(fin - ini);
    memmove(s->buf, ini, (size_t)resto);
    s->len = resto;
}

static int callback_sub(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes, size_t length,
                        picoquic_call_back_event_t evento, void *ctx_cb, void *ctx_stream)
{
    (void)stream_id; (void)ctx_stream;
    SubCtx *s = (SubCtx *)ctx_cb;
    switch (evento) {
    case picoquic_callback_ready:
        s->stream_id = picoquic_get_next_local_stream_id(cnx, 0);
        for (int i = 0; i < s->ntemas; i++) {
            char linea[TAM_LINEA];
            int len = snprintf(linea, sizeof linea, "SUB|%s\n", s->temas[i]);
            picoquic_add_to_stream(cnx, s->stream_id, (const uint8_t *)linea, (size_t)len, 0);
            printf("[sub] suscrito al tema '%s'\n", s->temas[i]);
        }
        picoquic_enable_keep_alive(cnx, 0);
        break;
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin: {
        size_t off = 0;
        while (off < length) {
            size_t libre = sizeof s->buf - 1 - (size_t)s->len;
            size_t n = (length - off < libre) ? (length - off) : libre;
            memcpy(s->buf + s->len, bytes + off, n);
            s->len += (int)n;
            off += n;
            procesar_buffer(s);
            if (s->len >= (int)sizeof s->buf - 1) s->len = 0;
        }
        break;
    }
    case picoquic_callback_close:
    case picoquic_callback_application_close:
    case picoquic_callback_stateless_reset:
        printf("[sub] la conexion QUIC con el broker se cerro (broker caido o cierre)\n");
        s->desconectado = 1;
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo, void *ctx, void *arg) {
    (void)quic;
    SubCtx *s = (SubCtx *)ctx;
    if (modo == picoquic_packet_loop_ready) {
        ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
    } else if (modo == picoquic_packet_loop_time_check || modo == picoquic_packet_loop_after_receive
               || modo == picoquic_packet_loop_after_send) {
        if (g_salir || s->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }
    static SubCtx s;
    memset(&s, 0, sizeof s);
    const char *ip = argv[1];
    int puerto = atoi(argv[2]);
    s.ntemas = argc - 3;
    s.temas = &argv[3];
    s.retraso_ms = env_int("RETRASO_MS", 0);
    instalar_senales();

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }

    picoquic_quic_t *quic = picoquic_create(1, NULL, NULL, NULL, ALPN_PUBSUB, NULL, NULL,
        NULL, NULL, NULL, picoquic_current_time(), NULL, NULL, NULL, 0);
    if (quic == NULL) { fprintf(stderr, "No se pudo crear el contexto QUIC\n"); return 1; }
    picoquic_set_null_verifier(quic);
    picoquic_set_default_idle_timeout(quic, IDLE_TIMEOUT_MS);
    picoquic_set_key_log_file_from_env(quic);

    s.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&dir, picoquic_current_time(), 0, SNI_DEFECTO, ALPN_PUBSUB, 1);
    if (s.cnx == NULL) { fprintf(stderr, "No se pudo crear la conexion\n"); return 1; }
    picoquic_set_callback(s.cnx, callback_sub, &s);
    if (picoquic_start_client_cnx(s.cnx) < 0) { fprintf(stderr, "No se pudo iniciar la conexion\n"); return 1; }
    printf("[sub] conectando por QUIC a %s:%d\n", ip, puerto);

    picoquic_packet_loop(quic, 0, dir.sin_family, 0, 0, 0, loop_cb, &s);

    imprimir_resumen(&s.r, "QUIC");
    picoquic_free(quic);
    return 0;
}
