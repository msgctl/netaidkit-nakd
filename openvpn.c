#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <linux/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <json-c/json.h>
#include "openvpn.h"
#include "log.h"
#include "jsonrpc.h"
#include "misc.h"

#define SOCK_PATH "/run/openvpn/openvpn.sock"
#define CONFIG_PATH "/nak/ovpn/current.ovpn"

static char * const argv[] = {
    "/usr/sbin/openvpn",
    "--log-append", "/var/log/openvpn.log",
    "--daemon",
    "--management", SOCK_PATH, "unix",
    "--config", CONFIG_PATH,
    NULL
};

static struct sockaddr_un _openvpn_sockaddr;
static int                _openvpn_sockfd = -1;
static int                _openvpn_pid;

static pthread_mutex_t _command_mutex = PTHREAD_MUTEX_INITIALIZER;

static int _kill_openvpn(int signal) {
    nakd_log(L_INFO, "Sending %s to OpenVPN, PID %d", strsignal(signal),
                                                          _openvpn_pid);

    int result = kill(_openvpn_pid, signal);
    if (result == -1) {
        nakd_log(L_WARNING, "Couldn't send %s to OpenVPN, PID %d: %s",
                    strsignal(signal), _openvpn_pid, strerror(errno));
        return -1;
    }
    return 0;
}

static int _access_config_file(void) {
    return access(CONFIG_PATH, R_OK);
}

static int _access_mgmt_socket(void) {
    return access(SOCK_PATH, R_OK);
}

static char *_getline(void) {
    const size_t line_max = 1024;
    char *buf = calloc(line_max, 1);
    nakd_assert(buf != NULL);

    errno = 0;
    for (char *bptr = buf; bptr < buf + line_max - 1;) {
        if (read(_openvpn_sockfd, bptr, 1) == -1) {
            if (errno == EAGAIN) {
                continue;
            } else {
                nakd_log(L_WARNING, "Error while reading from OpenVPN management "
                                                   "socket: %s", strerror(errno));
                goto err;
            }
        }

        if (*bptr == '\n') {
            *(bptr + 1) = 0;
            goto response;
        }
        bptr++;
    }

err:
    free(buf), buf = NULL;
response:
    if (buf != NULL)
        nakd_log(L_DEBUG, "<<%s", buf);
    return buf;
}

static int _open_mgmt_socket(void) {
    /* Check if there's already a valid descriptor open. */
    if (_openvpn_sockfd != -1 && fcntl(_openvpn_sockfd, F_GETFD) != -1)
        return 0;

    if (_access_mgmt_socket()) {
        nakd_log(L_WARNING, "Can't access OpenVPN management socket at "
                                                             SOCK_PATH);
        return -1;
    }

    nakd_assert((_openvpn_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) != -1);

    /* Check if SOCK_PATH is strncpy safe. */
    nakd_assert(sizeof SOCK_PATH < UNIX_PATH_MAX);

    _openvpn_sockaddr.sun_family = AF_UNIX;
    strncpy(_openvpn_sockaddr.sun_path, SOCK_PATH, sizeof SOCK_PATH);
    int len = sizeof(_openvpn_sockaddr.sun_family) + sizeof SOCK_PATH - 1;
    if (connect(_openvpn_sockfd, (struct sockaddr *)(&_openvpn_sockaddr), len)
                                                                      == -1) {
        nakd_log(L_WARNING, "Couldn't connect to OpenVPN management socket %s",
                                                                    SOCK_PATH);
        return -1;
    }

    nakd_log(L_DEBUG, "Connected to OpenVPN management socket %s", SOCK_PATH);

    char *info = _getline();
    if (info == NULL) {
        nakd_log(L_WARNING, "Couldn't get OpenVPN challenge line");
        return -1;
    } else {
        nakd_log(L_DEBUG, "OpenVPN challenge line: %s", info);
        free(info);
    }

    return 0;
}

static void _flush(void) {
    char buf[128];

    errno = 0;
    do {
        recv(_openvpn_sockfd, buf, sizeof buf, MSG_DONTWAIT);
    } while(errno != EWOULDBLOCK);
}

static void _close_mgmt_socket(void) {
    nakd_log(L_DEBUG, "Closing OpenVPN management socket %s", SOCK_PATH);
    close(_openvpn_sockfd);
    _openvpn_sockfd = -1;
}

static int _writeline(const char *line) { 
    nakd_log(L_DEBUG, ">>%s", line);

    const int len = strlen(line);                                                 
    for (int written = 0, d = 0;                                                  
         written != len;                                                          
         d = write(_openvpn_sockfd, line + d, len - written)) {                   
        if (d == -1) {                                                            
            nakd_log(L_WARNING, "Couldn't write to OpenVPN management socket: %s",
                                                                 strerror(errno));
            return 1;                                                             
        }                                                                         
        written += d;                                                             
    }                                                                             
    write(_openvpn_sockfd, "\n", 1);
    return 0;                                                                     
}                                                                                 

static char *_call_command(const char *command) {
    nakd_log(L_DEBUG, "Calling OpenVPN management command: %s", command);

    if (_open_mgmt_socket())
        return NULL;
    _flush();
    if (_writeline(command))
        return NULL;
    return _getline();
}

static int _mgmt_signal(const char *signal) {
    char buf[256];
    snprintf(buf, sizeof buf, "signal %s", signal);

    char *result = _call_command(buf);
    if (result != NULL) {
        free(result);
        return 0;
    }
    return 1;
}

static void _free_multiline(char ***lines) {
    /* a NULL-terminated array */
    for (char **line = *lines; *line != NULL; line++)
        free(*line);
    free(*lines);
    *lines = NULL;
}

static char **_call_command_multiline(const char *command) {
    nakd_log(L_DEBUG, "Calling OpenVPN management command: %s", command);

    if (_open_mgmt_socket())
        return NULL;
    _flush();
    if (_writeline(command))
        return NULL;

    /* read response */
    const size_t lines_max = 128;
    char **lines = malloc(sizeof(*lines) * lines_max);
    nakd_assert(lines != NULL);
    for (char **line = lines; line < lines + lines_max; line++)
        *line = NULL;

    for (char **line = lines; line < lines + lines_max - 1; line++) {
        char *_line = _getline();
        if (_line == NULL)
            goto err;

        if (!strncmp(_line, "END", sizeof("END") - 1)) {
            free(_line);
            goto response;
        }

        *line = _line;        
    }

err:
     _free_multiline(&lines);
response:
    return lines;
}

int nakd_start_openvpn(void) {
    nakd_log_execution_point();

    int pid = fork();
    nakd_assert(pid >= 0);

    if (pid == 0) /* child */ {
        execve(argv[0], argv, NULL);
        nakd_log(L_CRIT, "Couldn't start OpenVPN: %s", strerror(errno));
        return -1;
    } else if (pid == -1) {
        nakd_log(L_CRIT, "fork() failed: %s", strerror(errno));
        return -1;
    } 

    /* parent */
    _openvpn_pid = pid;
    nakd_log(L_INFO, "Started OpenVPN, PID %d", pid);
    return 0;
}

int nakd_stop_openvpn(void) {
    if (!_openvpn_pid) {
        nakd_log(L_INFO, "Attempted to stop OpenVPN, but it isn't running.");
        return 0;
    }

    /* Sending a SIGTERM to _openvpn_pid wouldn't deliver it to its child
     * processes, hence delivery via the management console.
     */

    /* TODO kill process group */

    if (_mgmt_signal("SIGTERM")) {
        /* In case the signal couldn't have been sent this way: */
        if (_kill_openvpn(SIGTERM))
            _kill_openvpn(SIGKILL);
    }
    _close_mgmt_socket();

    nakd_log(L_INFO, "Waiting for OpenVPN to terminate, PID %d: ",
                                                    _openvpn_pid);
    waitpid(_openvpn_pid, NULL, 0);
    _openvpn_pid = 0;

    return 0;
} 

int nakd_restart_openvpn(void) {
    /* Cause OpenVPN to close all TUN/TAP and network connections, restart, 
     * re-read the configuration file (if any), and reopen TUN/TAP and network
     * connections. -- OpeVPN manpage
     */
    return _mgmt_signal("SIGHUP");
}

static json_object *_parse_state_line(const char *resp) {
    char *respcp = strdup(resp);
    char *pos = respcp;
    const char *delim = ",";

    char *time = strsep(&pos, delim);
    char *state = strsep(&pos, delim);

    if (time == NULL || state == NULL) {
        nakd_log(L_WARNING, "Couldn't parse OpenVPN state: %s", resp);
        return NULL;
    }

    nakd_log(L_DEBUG, "Parsed OpenVPN state line: time=%s, state=%s", time,
                                                                    state);

    json_object *jresult = json_object_new_object();
    nakd_assert(jresult != NULL);
    
    json_object *jtime = json_object_new_string(time);
    nakd_assert(jtime != NULL);

    json_object *jstate = json_object_new_string(state);
    nakd_assert(jstate != NULL);

    json_object_object_add(jresult, "timestamp", jtime);
    json_object_object_add(jresult, "state", jstate);

    free(respcp);
    return jresult;
}

/* Commands in OpenVPN management console have different semantics, hence
 * the need for specialized handlers.
 */
json_object *_call_state(json_object *jcmd) {
    json_object *jresult = NULL;
    json_object *jresponse;

    char **lines = _call_command_multiline("state");
    if (lines == NULL) {
        nakd_log(L_WARNING, "Couldn't get current state from OpenVPN daemon.");
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                  "Internal error - while receiving OpenVPN response");
        goto response;
    }

    jresult = json_object_new_array();
    nakd_assert(jresult != NULL);  

    /* a NULL-terminated array */
    for (char **line = lines; *line != NULL; line++) {
        json_object *jline = _parse_state_line(*line);
        if (jline == NULL) {
            jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                      "Internal error - while parsing OpenVPN response");
            goto free_result;
        }

        json_object_array_add(jresult, jline);
    }

    jresponse = nakd_jsonrpc_response_success(jcmd, jresult); 
    goto free_input;

free_result:
    json_object_put(jresult);
free_input:
    _free_multiline(&lines);
response:
    return jresponse;
}

json_object *_call_start(json_object *jcmd) {
    json_object *jresponse;

    if (_access_config_file()) {
         jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                "Internal error - can't access OpenVPN configuration "
                                                   "at " CONFIG_PATH);
         goto response;
    }

    if (nakd_start_openvpn()) {
         jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                           "Internal error - couldn't start OpenVPN");
         goto response;
    }

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    return jresponse;
}

json_object *_call_stop(json_object *jcmd) {
    json_object *jresponse;

    if (nakd_stop_openvpn()) {
         jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                              "Internal error - OpenVPN not running");
         goto response;
    }

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    return jresponse;
}

json_object *_call_restart(json_object *jcmd) {
    json_object *jresponse;

    if (nakd_restart_openvpn()) {
         jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                              "Internal error - OpenVPN not running");
         goto response;
    }

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    return jresponse;
}

struct openvpn_command{
    const char *name;
    json_object *(*impl)(json_object *);
} static const _openvpn_commands[] = {
    { "state", _call_state },
    { "start", _call_start },
    { "stop", _call_stop },
    { "restart", _call_restart }
};

json_object *cmd_openvpn(json_object *jcmd, void *arg) {
    json_object *jresponse;
    json_object *jparams;

    nakd_log_execution_point();

    if ((jparams = nakd_jsonrpc_params(jcmd)) == NULL ||
        json_object_get_type(jparams) != json_type_string) {
        nakd_log(L_NOTICE, "Couldn't get arguments for OpenVPN management "
                                                                "command");
        jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
                   "Invalid parameters - params should be a string");
        goto response;
    }

    const char *command = json_object_get_string(jparams);
    for (const struct openvpn_command *cmd = _openvpn_commands;
         cmd < ARRAY_END(_openvpn_commands); cmd++) {
        if (!strcasecmp(cmd->name, command)) {
            pthread_mutex_lock(&_command_mutex);
            jresponse = cmd->impl(jcmd);
            pthread_mutex_unlock(&_command_mutex);
            goto response;
        }
    }

    jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
      "Invalid parameters - no such OpenVPN management command");

response:
    return jresponse;
}
