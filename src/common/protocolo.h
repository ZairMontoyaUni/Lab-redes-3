/*
 * protocolo.h - Definiciones comunes del sistema publicador-suscriptor
 * Laboratorio 3 - Grupo 9, Seccion 3 - ISIS2311L Redes y Comunicaciones
 *
 * Solo usa la biblioteca estandar de C y llamadas POSIX (signal, time).
 * Lo usan las versiones TCP, UDP (y QUIC, bono) para tener EXACTAMENTE
 * el mismo formato de mensajes.
 *
 * PROTOCOLO (texto plano, un mensaje por linea terminada en '\n'):
 *   Suscriptor -> Broker : SUB|tema
 *   Publicador -> Broker : PUB|tema|id_publicador|seq|texto
 *   Broker -> Suscriptor : MSG|tema|id_publicador|seq|texto
 *
 * 'seq' empieza en 1 y crece de a 1 por cada (tema, id_publicador).
 * Con ese numero el suscriptor detecta huecos (perdidos), desorden y duplicados.
 *
 * Puertos: TCP 9300 | UDP 9301 | QUIC 9302
 */
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>

/* Algunos macOS no definen MSG_NOSIGNAL; da igual porque SIGPIPE se ignora en instalar_senales() */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define PUERTO_TCP    9300
#define PUERTO_UDP    9301
#define PUERTO_QUIC   9302

#define MAX_TEMA       64
#define MAX_ID         32
#define MAX_TEXTO      512
#define MAX_TEMAS_SUB  8
#define TAM_LINEA      1024
#define MAX_FUENTES    64

typedef struct {
    char tipo[4];            /* "SUB", "PUB" o "MSG" */
    char tema[MAX_TEMA];
    char id[MAX_ID];
    long seq;
    char texto[MAX_TEXTO];
} Mensaje;

/* ---------- Manejo de senales (Ctrl+C imprime el resumen y sale) ---------- */
static volatile sig_atomic_t g_salir = 0;

static void manejador_senal(int sig) { (void)sig; g_salir = 1; }

static inline void instalar_senales(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = manejador_senal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                 /* sin SA_RESTART: recv/recvfrom devuelven EINTR */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);        /* escribir en un socket cerrado no mata el proceso */
    setvbuf(stdout, NULL, _IOLBF, 0);/* salida por lineas, util al redirigir a archivos */
}

/* ---------- Utilidades ---------- */
static inline void dormir_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static inline int env_int(const char *nombre, int defecto) {
    const char *v = getenv(nombre);
    return v ? atoi(v) : defecto;
}

static inline void hora_actual(char *dst, size_t n) {
    struct timespec ts;
    struct tm tmv;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tmv);
    size_t l = strftime(dst, n, "%H:%M:%S", &tmv);
    snprintf(dst + l, n - l, ".%03ld", ts.tv_nsec / 1000000);
}

/* Genera el texto de un evento de partido segun el numero de secuencia */
static inline void construir_evento(char *dst, size_t n, long seq) {
    static const char *plantillas[] = {
        "Gol de Equipo A al minuto %d",
        "Tarjeta amarilla al numero 10 de Equipo B (min %d)",
        "Cambio: jugador 10 entra por jugador 20 (min %d)",
        "Tiro de esquina para Equipo B (min %d)",
        "Falta de Equipo A cerca del area (min %d)"
    };
    int minuto = (int)((seq * 3) % 90) + 1;
    snprintf(dst, n, plantillas[(seq - 1) % 5], minuto);
}

/* ---------- Parser de lineas ----------
 * Devuelve 1 si la linea es valida (y llena 'm'), 0 si no.
 * El texto (ultimo campo) puede contener '|'. */
static inline int parsear_mensaje(const char *linea, Mensaje *m) {
    char copia[TAM_LINEA];
    char *campos[5];
    int nc = 0;

    memset(m, 0, sizeof *m);
    strncpy(copia, linea, sizeof copia - 1);   /* copia truncada a TAM_LINEA-1 */
    copia[sizeof copia - 1] = '\0';
    size_t l = strlen(copia);
    while (l > 0 && (copia[l - 1] == '\n' || copia[l - 1] == '\r')) copia[--l] = '\0';

    char *p = copia;
    campos[nc++] = p;
    while (nc < 5 && (p = strchr(p, '|')) != NULL) {
        *p++ = '\0';
        campos[nc++] = p;
    }

    if (strcmp(campos[0], "SUB") == 0 && nc >= 2) {
        snprintf(m->tipo, sizeof m->tipo, "SUB");
        snprintf(m->tema, sizeof m->tema, "%s", campos[1]);
        return 1;
    }
    if ((strcmp(campos[0], "PUB") == 0 || strcmp(campos[0], "MSG") == 0) && nc == 5) {
        snprintf(m->tipo, sizeof m->tipo, "%s", campos[0]);
        snprintf(m->tema, sizeof m->tema, "%s", campos[1]);
        snprintf(m->id, sizeof m->id, "%s", campos[2]);
        m->seq = atol(campos[3]);
        snprintf(m->texto, sizeof m->texto, "%s", campos[4]);
        return 1;
    }
    return 0;
}

/* ---------- Rastreador de secuencia (lo usan los suscriptores) ----------
 * Lleva el ultimo 'seq' visto por cada (tema, publicador). Asume que los
 * publicadores empiezan en seq=1, por eso los suscriptores deben iniciarse
 * ANTES que los publicadores. */
typedef struct {
    char clave[MAX_TEMA + MAX_ID + 2];
    long ultimo;
    int  usado;
} Fuente;

typedef struct {
    Fuente fuentes[MAX_FUENTES];
    long recibidos;
    long perdidos;       /* estimado: huecos detectados que nunca se rellenaron */
    long desordenados;   /* llegaron con seq menor al ultimo visto */
    long duplicados;
} Rastreador;

enum { SEQ_OK = 0, SEQ_HUECO = 1, SEQ_DESORDENADO = 2, SEQ_DUPLICADO = 3 };

static inline int rastrear(Rastreador *r, const Mensaje *m, long *faltan) {
    char clave[MAX_TEMA + MAX_ID + 2];
    Fuente *f = NULL;
    snprintf(clave, sizeof clave, "%s|%s", m->tema, m->id);

    for (int i = 0; i < MAX_FUENTES; i++)
        if (r->fuentes[i].usado && strcmp(r->fuentes[i].clave, clave) == 0) { f = &r->fuentes[i]; break; }
    if (!f) {
        for (int i = 0; i < MAX_FUENTES; i++)
            if (!r->fuentes[i].usado) { f = &r->fuentes[i]; break; }
        if (!f) return SEQ_OK;
        f->usado = 1;
        f->ultimo = 0;
        snprintf(f->clave, sizeof f->clave, "%s", clave);
    }

    r->recibidos++;
    *faltan = 0;
    if (m->seq == f->ultimo + 1) { f->ultimo = m->seq; return SEQ_OK; }
    if (m->seq > f->ultimo + 1) {
        *faltan = m->seq - f->ultimo - 1;
        r->perdidos += *faltan;
        f->ultimo = m->seq;
        return SEQ_HUECO;
    }
    if (m->seq == f->ultimo) { r->duplicados++; return SEQ_DUPLICADO; }
    r->desordenados++;                       /* llego tarde: no estaba perdido */
    if (r->perdidos > 0) r->perdidos--;
    return SEQ_DESORDENADO;
}

static inline void reportar_mensaje(const Mensaje *m, int estado, long faltan) {
    char hora[24], extra[48] = "";
    const char *etq = "OK";
    hora_actual(hora, sizeof hora);
    if (estado == SEQ_HUECO) { etq = "HUECO"; snprintf(extra, sizeof extra, " (faltaron %ld)", faltan); }
    else if (estado == SEQ_DESORDENADO) etq = "DESORDENADO";
    else if (estado == SEQ_DUPLICADO) etq = "DUPLICADO";
    printf("[%s] %-9s %-8s #%-4ld %s  <%s%s>\n", hora, m->tema, m->id, m->seq, m->texto, etq, extra);
}

static inline void imprimir_resumen(const Rastreador *r, const char *proto) {
    printf("\n========== RESUMEN SUSCRIPTOR (%s) ==========\n", proto);
    printf("Mensajes recibidos      : %ld\n", r->recibidos);
    printf("Perdidos (estimado)     : %ld\n", r->perdidos);
    printf("Desordenados            : %ld\n", r->desordenados);
    printf("Duplicados              : %ld\n", r->duplicados);
    for (int i = 0; i < MAX_FUENTES; i++)
        if (r->fuentes[i].usado)
            printf("  fuente %-20s ultimo seq visto: %ld\n", r->fuentes[i].clave, r->fuentes[i].ultimo);
    printf("(Si perdiste los ULTIMOS mensajes de una fuente, compara con los enviados por el publicador)\n");
    printf("==============================================\n");
}

#endif /* PROTOCOLO_H */
