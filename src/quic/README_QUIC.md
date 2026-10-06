# Bono QUIC - Documentación de la librería externa (picoquic)

## 1. Por qué se usa una librería externa
QUIC (RFC 9000) no es un servicio que entregue el sistema operativo como TCP o UDP con `socket()`.
Incluye en espacio de usuario, sobre UDP: **TLS 1.3 (cifrado obligatorio)**, confiabilidad, control
de congestión y de flujo, y **streams** independientes. Implementarlo solo con la biblioteca estándar
de C no es viable, por lo que se usó la librería **picoquic** (C, código abierto), que a su vez usa
**picotls + OpenSSL** para TLS. Se eligió por estar escrita en C, ser pequeña y exponer una API de
callbacks que permite documentar punto a punto su interacción con el programa.
Las versiones TCP y UDP **no** usan ninguna librería externa.

## 2. Instalación y uso (desde la raíz del repo, en Ubuntu)
```bash
./scripts/instalar_picoquic.sh        # clona y compila picotls + picoquic en third_party/
./scripts/generar_certificado.sh      # certs/cert.pem y certs/key.pem (autofirmado)
make quic                             # genera bin/broker_quic, publisher_quic, subscriber_quic
./scripts/demo.sh quic                # 1 broker + 2 suscriptores + 2 publicadores
```
Captura con llaves TLS (para que Wireshark descifre QUIC):
```bash
SSLKEYLOGFILE=$PWD/captures/quic_keys.log ./scripts/demo.sh quic
# Wireshark: Edit > Preferences > Protocols > TLS > (Pre)-Master-Secret log filename = captures/quic_keys.log
# Filtro de visualización: quic
```

## 3. Diseño
- Cada cliente abre **una conexión QUIC** y usa **un stream bidireccional** (id 0).
- Por el stream viaja el **mismo protocolo de texto** de TCP y UDP (`SUB|`, `PUB|`, `MSG|`, una línea por mensaje).
  Un stream es un flujo de bytes, así que se acumula en un buffer y se procesa por líneas (como en TCP).
- Puerto UDP **9302**. ALPN: `pubsub-lab3`.
- El certificado es autofirmado, por eso los clientes llaman a `picoquic_set_null_verifier()` (**solo laboratorio**).

## 4. Funciones de picoquic usadas (interacción punto a punto)

| Función / símbolo | Qué hace en la librería | Dónde y para qué se usa |
|---|---|---|
| `picoquic_create()` | Crea el contexto QUIC (certificado, llave, ALPN, callback por defecto, máximo de conexiones). | Broker: con cert/llave y `callback_broker`. Clientes: sin cert, sin callback por defecto. |
| `picoquic_free()` | Libera el contexto QUIC y sus conexiones. | Al terminar los 3 programas. |
| `picoquic_current_time()` | Hora actual en microsegundos usada por picoquic. | Para crear contextos/conexiones y pautar el envío del publicador. |
| `picoquic_set_default_idle_timeout()` | Fija el tiempo de inactividad tras el cual una conexión se da por caída. | Los 3 programas: 10 s (`IDLE_TIMEOUT_MS`). Define en cuánto se detecta un broker caído. |
| `picoquic_set_key_log_file_from_env()` | Si existe la variable `SSLKEYLOGFILE`, guarda las llaves TLS en ese archivo. | Los 3 programas; permite descifrar QUIC en Wireshark. |
| `picoquic_set_null_verifier()` | Desactiva la verificación del certificado del servidor. | Clientes, porque el certificado es autofirmado (solo laboratorio). |
| `picoquic_create_cnx()` | Crea el contexto de una conexión saliente hacia una dirección, con SNI y ALPN. | Publicador y suscriptor, hacia el broker. |
| `picoquic_set_callback()` | Asocia a una conexión su función callback y su contexto de aplicación. | Clientes: contexto propio. Broker: al llegar el primer evento de una conexión nueva, para asociarle su `struct Conexion`. Con `NULL, NULL` al cerrarse. |
| `picoquic_start_client_cnx()` | Inicia el handshake QUIC + TLS 1.3. | Publicador y suscriptor. |
| `picoquic_get_default_callback_context()` y `picoquic_get_quic_ctx()` | Permiten saber si el contexto recibido en el callback es el por defecto (conexión nueva). | Broker: para crear el contexto de cada conexión entrante. |
| `picoquic_get_next_local_stream_id()` | Devuelve el próximo id de stream local (0 = bidireccional). | Clientes, al quedar lista la conexión (stream 0). |
| `picoquic_add_to_stream()` | Encola bytes para enviar por un stream (con o sin FIN). QUIC se encarga de entrega confiable y control de congestión. | Cliente: envía `PUB\|...` / `SUB\|...`. Broker: envía `MSG\|...` al stream de cada suscriptor. Publicador: FIN al terminar. |
| `picoquic_enable_keep_alive()` | Envía paquetes periódicos para que la conexión no expire por inactividad. | Suscriptor (intervalo 0 = idle_timeout/2). |
| `picoquic_close()` | Cierra la conexión enviando CONNECTION_CLOSE. | Publicador, al terminar de enviar. |
| `picoquic_packet_loop()` | Bucle de red de la librería: abre el socket UDP, recibe datagramas, los pasa a QUIC y envía lo que QUIC prepara. Llama a un callback del programa. | Los 3 programas (broker con puerto 9302 y `AF_INET`; clientes con puerto 0). |

**Eventos del callback de conexión** (`picoquic_call_back_event_t`): `picoquic_callback_ready` (handshake terminado:
el cliente envía SUB / empieza a publicar), `picoquic_callback_stream_data` y `picoquic_callback_stream_fin` (llegan
bytes por un stream), `picoquic_callback_close`, `picoquic_callback_application_close` y
`picoquic_callback_stateless_reset` (la conexión terminó: se libera el contexto o se sale del bucle).

**Callback del bucle de red** (`picoquic_packet_loop_cb_fn`): modos `picoquic_packet_loop_ready` (se activa
`picoquic_packet_loop_options_t.do_time_check` para ser consultado antes de cada espera),
`picoquic_packet_loop_time_check` (con `packet_loop_time_check_arg_t` el publicador ajusta `delta_t` para
despertar a tiempo y enviar el siguiente mensaje), `picoquic_packet_loop_after_receive` y
`picoquic_packet_loop_after_send` (comprueban si hay que terminar). Devolver
`PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP` detiene el bucle. `PICOQUIC_ERROR_MEMORY` se usa al cerrar si falla un `calloc`.

picotls y OpenSSL **no se llaman directamente**: los usa picoquic internamente para TLS 1.3.

## 5. Limitaciones conocidas
- Con **Ctrl+C** los clientes salen sin enviar CONNECTION_CLOSE; el broker los da de baja por inactividad (~10 s).
- Si el broker muere, los clientes lo detectan también por inactividad (~10 s) y reciben el evento de cierre.
- Verificación de certificado desactivada (solo laboratorio).
