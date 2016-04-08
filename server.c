#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <semaphore.h>
#include <json-c/json.h>
#include "server.h"
#include "request.h"
#include "log.h"
#include "misc.h"
#include "jsonrpc.h"

/* TODO nice to have: implement w/ epoll, threadpool and workqueue */

#define CONNECTION_THREAD_STACK_SIZE 65535
#define MAX_CONNECTIONS     32
#define SOCK_PATH      "/run/nakd/nakd.sock"

static struct sockaddr_un _nakd_sockaddr;
static int                _nakd_sockfd;

struct connection {
    pthread_t thread;

    int sockfd;
    int active;
} static _connections[MAX_CONNECTIONS];
pthread_mutex_t _connections_mutex;

sem_t _connections_sem;

static pthread_mutex_t _shutdown_mutex;
static pthread_cond_t _shutdown_cv;

static int _unit_initialized;
static int _shutdown;

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
    return conn;
}

static void __free_connection(struct connection *conn) {
    nakd_log_execution_point();

    conn->active = 0;
    pthread_mutex_lock(&_shutdown_mutex);
    sem_post(&_connections_sem);
    pthread_cond_signal(&_shutdown_cv);
    pthread_mutex_unlock(&_shutdown_mutex);
}

static void __close_connection(struct connection *conn) {
    nakd_log_execution_point();

    close(conn->sockfd);
}

static int _message_loop(int sock) {
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
                if (_shutdown) {
                    /* don't read another request if about to shut down */
                    nakd_log(L_DEBUG, "Shutting down connection thread, "
                                                      "sockfd=%d", sock);
                    goto ret;
                }

                if ((nb_read = recvfrom(sock, message_buf, sizeof message_buf, 0,
                          (struct sockaddr *) &client_addr, &client_len)) < 0) { 
                    nakd_log(L_DEBUG, "recvfrom() < 0, closing connection (%s)",
                                                             strerror(nb_read));
                    rval = 1;
                    goto ret;
                } else if (!nb_read) {
                    /* Handle orderly shutdown. */
                    nakd_log(L_DEBUG, "Client hung up.");
                    goto ret;
                }
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
                nb_sent = sendto(sock, jrstr, nb_resp, 0,
                        (struct sockaddr *) &client_addr,
                                             client_len);
                if (nb_sent < 0) {
                    nakd_log(L_NOTICE,
                        "Couldn't send response, closing connection.");
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

static void *_connection_thread(void *thr_data) {
    int rval;
    struct connection *conn = thr_data; 

    nakd_log(L_DEBUG, "Created connection thread, starting message loop");

    pthread_cleanup_push(_connection_cleanup, conn);
    rval = _message_loop(conn->sockfd);
    pthread_cleanup_pop(1);
    return rval;
}

static struct connection *_handle_connection(int sock,
                       struct sockaddr_un *sockaddr) {
    struct connection *conn;
    pthread_attr_t attr;

    nakd_log_execution_point();

    pthread_mutex_lock(&_connections_mutex);
    conn = __add_connection(sock, sockaddr);
    if (conn == NULL) {
        nakd_log(L_INFO, "Out of connection slots.");
        return NULL;
    }

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, CONNECTION_THREAD_STACK_SIZE);

    nakd_log(L_DEBUG, "Creating connection thread");
    if (pthread_create(&conn->thread, &attr, _connection_thread, conn)) {
        nakd_log(L_CRIT, "Couldn't create a connection thread");
        conn = NULL;
    }

    pthread_mutex_unlock(&_connections_mutex);
    return conn;
}

void nakd_server_init(void) {
    if (!_unit_initialized) {
        pthread_mutex_init(&_connections_mutex, NULL);
        pthread_mutex_init(&_shutdown_mutex, NULL);
        pthread_cond_init(&_shutdown_cv, NULL);
        sem_init(&_connections_sem, 0, MAX_CONNECTIONS);

        _unit_initialized = 1;
    }
}

void nakd_server_cleanup(void) {
    if (!_unit_initialized)
        return;

    nakd_shutdown_connections();
    sem_destroy(&_connections_sem);
    pthread_cond_destroy(&_shutdown_cv);
    pthread_mutex_destroy(&_shutdown_mutex);
    pthread_mutex_destroy(&_connections_mutex);
}

int nakd_active_connections(void) {
    int value;
    sem_getvalue(&_connections_sem, &value);
    return MAX_CONNECTIONS - value;
}

void nakd_accept_loop(void) {
    int c_sock;
    pid_t handler_pid;
    struct sockaddr_un c_sockaddr;
    socklen_t len = sizeof c_sockaddr;

    nakd_log_execution_point();

    /* Failure to set up the socket is unrecoverable, hence no return check. */
    if (!_nakd_sockfd)
        _create_unix_socket();

    while(!_shutdown) {
        sem_wait(&_connections_sem);
        nakd_log(L_DEBUG, "%d connection slot(s) available.",
                MAX_CONNECTIONS - nakd_active_connections());
        nakd_assert((c_sock = accept(_nakd_sockfd, (struct sockaddr *)
                                         (&c_sockaddr), &len)) != -1);
        nakd_log(L_INFO, "Connection accepted, %d connection(s) currently "
                                     "active.", nakd_active_connections());

        _handle_connection(c_sock, &c_sockaddr);
    }

    close(_nakd_sockfd);
    if (unlink(SOCK_PATH) == -1)
        nakd_terminate("unlink()");
}

void nakd_shutdown_connections(void) {
    int connections;
    _shutdown = 1;

    while (connections = nakd_active_connections()) {
        pthread_mutex_lock(&_shutdown_mutex);
        nakd_log(L_INFO, "Shutting down connections, %d remaining", connections);
        pthread_cond_wait(&_shutdown_cv, &_shutdown_mutex);
        pthread_mutex_unlock(&_shutdown_mutex);
    }
}
