#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <json-c/json.h>
#include "server.h"
#include "request.h"
#include "log.h"
#include "misc.h"
#include "jsonrpc.h"
#include "thread.h"

/* TODO nice to have: implement w/ epoll, threadpool and workqueue */

#define MAX_CONNECTIONS     32
#define SOCK_PATH      "/run/nakd/nakd.sock"

static struct sockaddr_un _nakd_sockaddr;
static int                _nakd_sockfd;

struct connection {
    struct nakd_thread *thread;
    int sockfd;
    int active;
    int shutdown;
} static _connections[MAX_CONNECTIONS];
static pthread_mutex_t _connections_mutex;

static sem_t _connections_sem;

static pthread_mutex_t _shutdown_mutex;
static pthread_cond_t _shutdown_cv;

static struct nakd_thread *_server_thread;
static int _server_shutdown;

static int _unit_initialized;

/* doubly-prefixed functions aren't thread-safe */

static struct connection *__get_connection_slot(void) {
    struct connection *conn = _connections;

    for (; conn < ARRAY_END(_connections) && conn->active; conn++);
    if (conn >= ARRAY_END(_connections))
        return NULL;
    return conn;
}

static struct connection *__add_connection(int sock,
                    struct sockaddr_un *sockaddr) {
    struct connection *conn;
    nakd_log_execution_point();

    conn = __get_connection_slot();
    if (conn == NULL)
        return NULL;

    conn->sockfd = sock;
    conn->active = 1;
    conn->shutdown = 0;
    return conn;
}

static void __free_connection(struct connection *conn) {
    nakd_log_execution_point();

    pthread_mutex_lock(&_shutdown_mutex);
    conn->active = 0;
    sem_post(&_connections_sem);
    pthread_cond_signal(&_shutdown_cv);
    pthread_mutex_unlock(&_shutdown_mutex);
}

static void __close_connection(struct connection *conn) {
    nakd_log_execution_point();

    close(conn->sockfd);
}

static int _message_loop(struct connection *conn) {
   struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(struct sockaddr_un);
    char message_buf[4096];
    int nb_read = 0, nb_sent;
    json_tokener *jtok;
    int nb_parse, parse_offset;
    json_object *jmsg = NULL;
    json_object *jresponse = NULL;
    const char *jrstr;
    int nb_resp;
    enum json_tokener_error jerr;
    int rval = 0;

    nakd_log_execution_point();

    jtok = json_tokener_new();

    for (;;) {
        do {
            if (conn->shutdown) {
                nakd_log(L_DEBUG, "Shutting down connection thread, "
                                          "sockfd=%d", conn->sockfd);
                goto ret;
            }

            if (jerr == json_tokener_continue) {
                nakd_log(L_DEBUG, "Parsing incoming message, offset: %d",
                                                      jtok->char_offset);
                nakd_log(L_DEBUG, "jerr == json_tokener_continue, "
                                              "reading some more");
            }

            /* If there was another JSON string in the middle of the buffer,
             * start with an offset and don't call recvfrom() just yet.
             */
            parse_offset = jtok->char_offset < nb_read ? jtok->char_offset : 0;
            if (!parse_offset) {
                /* Don't update nb_read if interrupted. */
                int rcvfrom_s;

                if ((rcvfrom_s = recvfrom(conn->sockfd, message_buf,
                      sizeof message_buf, 0, (struct sockaddr *) 
                              &client_addr, &client_len)) == -1) { 
                    if (errno == EINTR)
                        continue;

                    nakd_log(L_NOTICE, "Closing connection (%s)",
                                              strerror(nb_read));
                    rval = 1;
                    goto ret;
                } else if (!rcvfrom_s) {
                    /* Handle orderly shutdown. */
                    nakd_log(L_DEBUG, "Client hung up.");
                    goto ret;
                }
                nb_read = rcvfrom_s;
                nakd_log(L_DEBUG, "Read %d bytes.", nb_read);

                /* remaining bytes to parse */
                nb_parse = nb_read;
            } else {
                nb_parse = nb_read - jtok->char_offset;
            }

            /* partial JSON strings are stored in tokener context */
            jmsg = json_tokener_parse_ex(jtok, message_buf + parse_offset,
                                                                nb_parse);
        } while ((jerr = json_tokener_get_error(jtok)) == json_tokener_continue);

        if (jerr == json_tokener_success) {
            nakd_log(L_DEBUG, "Parsed a complete message of %d bytes.",
                                                    jtok->char_offset);

            /* doesn't allocate memory */
            const char *jmsg_string = json_object_to_json_string(jmsg);
            nakd_log(L_DEBUG, "Got message: %s", jmsg_string);

            jresponse = nakd_handle_message(jmsg);
        } else {
            nakd_log(L_NOTICE, "Couldn't parse client JSON message: %s.",
                                          json_tokener_error_desc(jerr));

            jresponse = nakd_jsonrpc_response_error(NULL, PARSE_ERROR, NULL);
            json_tokener_reset(jtok);
        }

        /* jresponse will be null while handling notifications. */
        if (jresponse != NULL) {
            jrstr = json_object_get_string(jresponse);

            while (nb_resp = strlen(jrstr)) {
                nb_sent = sendto(conn->sockfd, jrstr, nb_resp, 0,
                              (struct sockaddr *) &client_addr,
                                                   client_len);
                if (nb_sent = -1) {
                    if (errno == EINTR)
                        continue;

                    nakd_log(L_NOTICE,
                        "Couldn't send response, closing connection. (%s)",
                                                          strerror(errno));
                    rval = 1;
                    goto ret;
                }
                jrstr += nb_sent;
            }

            nakd_log(L_DEBUG, "Response sent: %s",
                json_object_to_json_string(jresponse));
            json_object_put(jresponse), jresponse = NULL;
        }

        json_object_put(jmsg), jmsg = NULL;
    }

ret:
    /* NULL-safe functions */
    json_object_put(jresponse);
    json_object_put(jmsg);
    json_tokener_free(jtok);
    return rval;
}

static void _create_unix_socket(void) {
    struct stat sock_path_st;

    /* Create the nakd server socket. */
    nakd_assert((_nakd_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) != -1);

    /* Check if SOCK_PATH is strncpy safe. */
    nakd_assert(sizeof SOCK_PATH < UNIX_PATH_MAX);

    /* Set domain socket path to SOCK_PATH. */
    strncpy(_nakd_sockaddr.sun_path, SOCK_PATH, sizeof SOCK_PATH);
    _nakd_sockaddr.sun_family = AF_UNIX;

    /* Remove domain socket file if it exists. */
    if (stat(SOCK_PATH, &sock_path_st) == 0)
        if (unlink(SOCK_PATH) == -1)
            nakd_terminate("Couldn't remove socket at %s",
                                 _nakd_sockaddr.sun_path);

    nakd_log(L_INFO, "Using socket at %s", _nakd_sockaddr.sun_path);

    /* Bind nakd server socket to the domain socket. */
    nakd_assert(bind(_nakd_sockfd, (struct sockaddr *) &_nakd_sockaddr,
                                     sizeof(struct sockaddr_un)) >= 0);

    /* Set domain socket world writable, permissions via credentials passing */
    nakd_assert(chmod(SOCK_PATH, 0777) != -1);

    /* Listen on local domain socket. */
    nakd_assert(listen(_nakd_sockfd, MAX_CONNECTIONS) != -1);
}

static void _connection_cleanup(void *arg) {
    struct connection *conn = arg;

    nakd_log_execution_point();

    pthread_mutex_lock(&_connections_mutex);
    __close_connection(conn);
    __free_connection(conn);
    pthread_mutex_unlock(&_connections_mutex);
}

static void _connection_setup(struct nakd_thread *thread) {
    struct connection *conn = thread->priv; 

    nakd_log(L_DEBUG, "Created connection thread, starting message loop");

    pthread_cleanup_push(_connection_cleanup, conn);
    _message_loop(conn);
    pthread_cleanup_pop(1);
}

static void _connection_shutdown(struct nakd_thread *thread) {
    struct connection *conn = thread->priv;
    conn->shutdown = 1;
}

static struct connection *_handle_connection(int sock,
                       struct sockaddr_un *sockaddr) {
    struct connection *conn;

    nakd_log_execution_point();

    pthread_mutex_lock(&_connections_mutex);
    conn = __add_connection(sock, sockaddr);
    if (conn == NULL) {
        nakd_log(L_INFO, "Out of connection slots.");
        return NULL;
    }

    nakd_log(L_DEBUG, "Creating connection thread");
    if (nakd_thread_create_detached(_connection_setup,
                                 _connection_shutdown,
                               conn, &conn->thread)) {
        nakd_log(L_CRIT, "Couldn't create a connection thread");
        conn = NULL;
    }

    pthread_mutex_unlock(&_connections_mutex);
    return conn;
}

int nakd_active_connections(void) {
    int value;
    sem_getvalue(&_connections_sem, &value);
    return MAX_CONNECTIONS - value;
}

static void _accept_loop(void) {
    int c_sock;
    pid_t handler_pid;
    struct sockaddr_un c_sockaddr;
    socklen_t len = sizeof c_sockaddr;

    nakd_log_execution_point();

    /* Failure to set up the socket is unrecoverable, hence no return check. */
    if (!_nakd_sockfd)
        _create_unix_socket();

    while(!_server_shutdown) {
        sem_wait(&_connections_sem);
        nakd_log(L_DEBUG, "%d connection slot(s) available.",
                MAX_CONNECTIONS - nakd_active_connections());
        c_sock = accept(_nakd_sockfd, (struct sockaddr *)(&c_sockaddr), &len);
        if (c_sock == -1) {
            sem_post(&_connections_sem);

            if (errno == EINTR)
                continue;

            nakd_terminate("Can't accept new connections (%s)", strerror(errno));
        }

        nakd_log(L_INFO, "Connection accepted, %d connection(s) currently "
                                     "active.", nakd_active_connections());
        _handle_connection(c_sock, &c_sockaddr);
    }

    close(_nakd_sockfd);
    if (unlink(SOCK_PATH) == -1)
        nakd_terminate("unlink()");
}

static void _shutdown_connections(void) {
    for (struct connection *conn = _connections;
         conn < ARRAY_END(_connections); conn++) {
        if (conn->active)
            nakd_thread_kill(conn->thread);
    }

    int connections;
    pthread_mutex_lock(&_shutdown_mutex);
    while (connections = nakd_active_connections()) {
        nakd_log(L_INFO, "Shutting down connections, %d remaining", connections);
        pthread_cond_wait(&_shutdown_cv, &_shutdown_mutex);
        pthread_mutex_unlock(&_shutdown_mutex);
    }
}

/* inside newly-created thread */
static void _server_thread_setup(struct nakd_thread *thread) {
    _accept_loop();
    /* _accept_loop() will return only if _server_shutdown == 1 */
    _shutdown_connections();
}

static void _server_thread_shutdown(struct nakd_thread *thread) {
    _server_shutdown = 1;
}

static int _create_server_thread(void) {
    nakd_log(L_DEBUG, "Creating server thread.");
    if (nakd_thread_create_joinable(_server_thread_setup,
                                 _server_thread_shutdown,
                                  NULL, &_server_thread)) {
        nakd_log(L_CRIT, "Couldn't create server thread.");
        return 1;
    }
    return 0;
}

void nakd_server_init(void) {
    if (!_unit_initialized) {
        pthread_mutex_init(&_connections_mutex, NULL);
        pthread_mutex_init(&_shutdown_mutex, NULL);
        pthread_cond_init(&_shutdown_cv, NULL);
        sem_init(&_connections_sem, 0, MAX_CONNECTIONS);

        _unit_initialized = 1;
        _create_server_thread();
    }
}

void nakd_server_cleanup(void) {
    nakd_log_execution_point();

    if (!_unit_initialized)
        return;

    nakd_thread_kill(_server_thread);

    sem_destroy(&_connections_sem);
    pthread_cond_destroy(&_shutdown_cv);
    pthread_mutex_destroy(&_shutdown_mutex);
    pthread_mutex_destroy(&_connections_mutex);
    _unit_initialized = 0;
}
