/*
 * publisher_quic.c - Publicador (periodista) sobre QUIC (BONO)
 * Lab 3 - Grupo 9, Seccion 3
 *
 * COMANDOS (desde la raiz del repo, despues de instalar picoquic):
 *   Compilar : make quic
 *   Uso      : ./bin/publisher_quic <ip_broker> <puerto> <tema> [num_mensajes=10] [intervalo_ms=500]
 *   Ejemplos : ./bin/publisher_quic 127.0.0.1 9302 PartidoA
 *              ./bin/publisher_quic 127.0.0.1 9302 PartidoB 20 200
 *              SSLKEYLOGFILE=$PWD/captures/quic_keys.log ./bin/publisher_quic 127.0.0.1 9302 PartidoA
 *
 * FUNCIONAMIENTO
 *   - picoquic_create() crea el contexto QUIC de cliente; picoquic_create_cnx() +
 *     picoquic_start_client_cnx() inician la conexion (handshake QUIC+TLS 1.3).
 *   - El certificado del broker es autofirmado: picoquic_set_null_verifier() desactiva
 *     la verificacion (SOLO para el laboratorio).
 *   - Cuando la conexion esta lista (picoquic_callback_ready) se envian los mensajes
 *     con picoquic_add_to_stream() en el stream 0, uno cada 'intervalo_ms'. El ritmo
 *     se controla desde el callback del bucle (picoquic_packet_loop_time_check).
 *   - Al terminar: se cierra el stream (fin=1), se espera 1.5 s a que se entreguen
 *     los datos y se llama a picoquic_close().
 */
#define _DEFAULT_SOURCE
#include <unistd.h>
#include <arpa/inet.h>
#include "quic_comun.h"

typedef struct {
    picoquic_cnx_t *cnx;
    uint64_t stream_id;
    char     tema[MAX_TEMA];
    char     id[MAX_ID];
    long     n, enviados;
    int      intervalo_ms;
    int      listo;
    int      desconectado;
    uint64_t proximo_envio;     /* microsegundos (reloj de picoquic_current_time) */
    uint64_t cierre_en;         /* 0 = aun no programado */
    int      cierre_pedido;
} PubCtx;

static int callback_pub(picoquic_cnx_t *cnx, uint64_t stream_id, uint8_t *bytes, size_t length,
                        picoquic_call_back_event_t evento, void *ctx_cb, void *ctx_stream)
{
    (void)stream_id; (void)bytes; (void)length; (void)ctx_stream;
    PubCtx *p = (PubCtx *)ctx_cb;
    switch (evento) {
    case picoquic_callback_ready:               /* handshake completado */
        p->listo = 1;
        p->stream_id = picoquic_get_next_local_stream_id(cnx, 0);   /* 0 = bidireccional */
        p->proximo_envio = picoquic_current_time();
        printf("[pub %s] conexion QUIC establecida\n", p->id);
        break;
    case picoquic_callback_close:
    case picoquic_callback_application_close:
    case picoquic_callback_stateless_reset:
        printf("[pub %s] conexion QUIC cerrada\n", p->id);
        p->desconectado = 1;
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static int loop_cb(picoquic_quic_t *quic, picoquic_packet_loop_cb_enum modo, void *ctx, void *arg) {
    (void)quic;
    PubCtx *p = (PubCtx *)ctx;
    if (modo == picoquic_packet_loop_ready) {
        ((picoquic_packet_loop_options_t *)arg)->do_time_check = 1;
        return 0;
    }
    if (modo == picoquic_packet_loop_after_receive || modo == picoquic_packet_loop_after_send) {
        /* Sale apenas la conexion queda cerrada, sin esperar al siguiente temporizador */
        if (g_salir || p->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        return 0;
    }
    if (modo != picoquic_packet_loop_time_check) return 0;

    packet_loop_time_check_arg_t *t = (packet_loop_time_check_arg_t *)arg;
    uint64_t ahora = t->current_time;
    if (g_salir || p->desconectado) return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
    if (!p->listo) return 0;

    int encolo = 0;
    if (p->enviados < p->n && ahora >= p->proximo_envio) {
        long lote = (p->intervalo_ms == 0) ? (p->n - p->enviados) : 1;     /* intervalo 0 = rafaga */
        for (long k = 0; k < lote; k++) {
            char texto[MAX_TEXTO], linea[TAM_LINEA];
            long seq = ++p->enviados;
            construir_evento(texto, sizeof texto, seq);
            int len = snprintf(linea, sizeof linea, "PUB|%s|%s|%ld|%s\n", p->tema, p->id, seq, texto);
            picoquic_add_to_stream(p->cnx, p->stream_id, (const uint8_t *)linea, (size_t)len, 0);
            printf("[pub %s] enviado #%ld: %s\n", p->id, seq, texto);
        }
        p->proximo_envio = ahora + (uint64_t)p->intervalo_ms * 1000;
        encolo = 1;
        if (p->enviados >= p->n) {
            picoquic_add_to_stream(p->cnx, p->stream_id, (const uint8_t *)"", 0, 1);   /* fin del stream */
            p->cierre_en = ahora + 1500000;                                            /* dar tiempo a entregar */
        }
    }

    if (p->cierre_en != 0 && ahora >= p->cierre_en) {
        if (!p->cierre_pedido) {
            picoquic_close(p->cnx, 0);                                                 /* CONNECTION_CLOSE */
            p->cierre_pedido = 1;
            p->cierre_en = ahora + 1000000;                                            /* tope de espera */
        } else {
            return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
        }
    }

    if (encolo) {
        t->delta_t = 0;                                                                /* enviar de inmediato */
    } else {
        int64_t espera = 200000;
        if (p->cierre_en != 0) espera = (int64_t)p->cierre_en - (int64_t)ahora;
        else if (p->enviados < p->n) espera = (int64_t)p->proximo_envio - (int64_t)ahora;
        if (espera < 0) espera = 0;
        if (espera < t->delta_t) t->delta_t = espera;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes] [intervalo_ms]\n", argv[0]);
        return 1;
    }
    PubCtx p;
    memset(&p, 0, sizeof p);
    const char *ip = argv[1];
    int puerto = atoi(argv[2]);
    if (strlen(argv[3]) >= MAX_TEMA) { fprintf(stderr, "Tema demasiado largo\n"); return 1; }
    snprintf(p.tema, sizeof p.tema, "%s", argv[3]);
    p.n = (argc > 4) ? atol(argv[4]) : 10;
    p.intervalo_ms = (argc > 5) ? atoi(argv[5]) : 500;
    snprintf(p.id, sizeof p.id, "P%d", (int)getpid());
    instalar_senales();

    struct sockaddr_in dir;
    memset(&dir, 0, sizeof dir);
    dir.sin_family = AF_INET;
    dir.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip, &dir.sin_addr) != 1) { fprintf(stderr, "IP invalida: %s\n", ip); return 1; }

    picoquic_quic_t *quic = picoquic_create(1, NULL, NULL, NULL, ALPN_PUBSUB, NULL, NULL,
        NULL, NULL, NULL, picoquic_current_time(), NULL, NULL, NULL, 0);
    if (quic == NULL) { fprintf(stderr, "No se pudo crear el contexto QUIC\n"); return 1; }
    picoquic_set_null_verifier(quic);                       /* certificado autofirmado: solo laboratorio */
    picoquic_set_default_idle_timeout(quic, IDLE_TIMEOUT_MS);
    picoquic_set_key_log_file_from_env(quic);

    p.cnx = picoquic_create_cnx(quic, picoquic_null_connection_id, picoquic_null_connection_id,
        (struct sockaddr *)&dir, picoquic_current_time(), 0, SNI_DEFECTO, ALPN_PUBSUB, 1);
    if (p.cnx == NULL) { fprintf(stderr, "No se pudo crear la conexion\n"); return 1; }
    picoquic_set_callback(p.cnx, callback_pub, &p);
    if (picoquic_start_client_cnx(p.cnx) < 0) { fprintf(stderr, "No se pudo iniciar la conexion\n"); return 1; }
    printf("[pub %s] conectando por QUIC a %s:%d, tema '%s', %ld mensajes\n", p.id, ip, puerto, p.tema, p.n);

    picoquic_packet_loop(quic, 0, dir.sin_family, 0, 0, 0, loop_cb, &p);

    printf("[pub %s] terminado (%ld mensajes enviados)\n", p.id, p.enviados);
    picoquic_free(quic);
    return 0;
}
