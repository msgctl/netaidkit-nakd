#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <json-c/json.h>
#include "shell.h"
#include "request.h"
#include "log.h"
#include "jsonrpc.h"

#define PIPE_READ       0
#define PIPE_WRITE      1

#define NAKD_SHELL "/bin/sh"

#define NAKD_MAX_ARG_STRLEN 8192

/* create {"/bin/sh", argv[0], ..., argv[n], NULL} on heap */
static char **build_argv_json(const char *path, json_object *params) {
    int argn = json_object_array_length(params);
    char **argv = malloc((argn + 3) * sizeof(char *));
    nakd_assert(argv != NULL);

    argv[0] = strdup(NAKD_SHELL);
    argv[1] = strdup(path);

    int i = 0;
    for (; i < argn; i++) {
        const char *param = "";
        json_object *jparam = json_object_array_get_idx(params, i);

        if (jparam != NULL)
            param = json_object_get_string(jparam);

        argv[2+i] = strdup(param);
    }

    argv[2+i] = NULL;
    return argv;
}

static int _get_argc(const char *args) {
    int argc = 1;
    int prev = 0;

    for (const char *p = args; *p; p++) {
        if (isspace(*p) && prev) {
            argc++;
            prev = 0;
        } else {
            prev = 1;
        }
    }
    return argc;
}

static char **build_argv(const char *args) {
    const char *delim = "\f\n\r\t\v ";
    char *argscp = strdup(args);
    char *pos = argscp;
    nakd_assert(argscp != NULL);

    int argc = _get_argc(args);

    char **argv = malloc((argc + 2) * sizeof(char *));
    nakd_assert(argv != NULL);

    argv[0] = strdup(NAKD_SHELL);

    int i = 0;
    for (; i < argc; i++) {
        const char *param = strsep(&pos, delim);
        argv[1+i] = strdup(param);
    }

    argv[1+i] = NULL;
    return argv;
}

static void free_argv(const char **argv) {
    if (argv == NULL)
        return;

    for (int i = 0; argv[i] != NULL; i++)
        free((void *)(argv[i]));
    free((void *)(argv));
}

static void log_execve(const char *argv[]) {
    int format_len = 0;
    char *execve_log = malloc(NAKD_MAX_ARG_STRLEN);
    nakd_assert(execve_log != NULL);

    for (; *argv != NULL; argv++)
        format_len += snprintf(execve_log + format_len, NAKD_MAX_ARG_STRLEN
                                               - format_len, " %s", *argv);

    nakd_log(L_DEBUG, execve_log);
    free(execve_log);
}

int nakd_do_command(const char *cwd, char **output, const char *fmt, ...) {
    va_list vl;
    char *args = malloc(NAKD_MAX_ARG_STRLEN);
    nakd_assert(args != NULL);

    va_start(vl, fmt);
    vsnprintf(args, NAKD_MAX_ARG_STRLEN, fmt, vl);
    va_end(vl);

    const char **argv = (const char **)(build_argv(args));
    nakd_assert(argv != NULL);

    int status = nakd_do_command_argv(argv, cwd, output);
    free_argv(argv);
    free(args);
    return status;
}

/* Returns NULL if the command failed.
 */
int nakd_do_command_argv(const char **argv, const char *cwd, char **output) {
    pid_t pid;
    int pipe_fd[2];
    char *response = NULL;
    char *truncated = NULL;
    int status = -1;

    nakd_log_execution_point();
    log_execve((const char **)(argv));

    if (access(argv[0], X_OK)) {
        nakd_log(L_CRIT, "The file at %s isn't an executable.", argv[0]);
        goto ret;
    }

    response = malloc(MAX_SHELL_RESULT_LEN);
    if (response == NULL) {
        nakd_log(L_WARNING, "Couldn't allocate %d bytes for command response",
                                                        MAX_SHELL_RESULT_LEN);
        goto ret;
    }

    memset(response, 0, MAX_SHELL_RESULT_LEN);

    if (pipe(pipe_fd) == -1) {
        nakd_terminate("pipe()");
        goto ret;
    }

    pid = fork();
    if (pid < 0) {
        goto ret;
    } else if (pid == 0) { /* child */
        close(pipe_fd[PIPE_READ]);
        dup2(pipe_fd[PIPE_WRITE], 1);
        dup2(pipe_fd[PIPE_WRITE], 2);

        if (cwd != NULL)
            chdir(cwd);
        execve(argv[0], (char * const *)(argv), NULL);

        nakd_terminate("execve()");
    } else { /* parent */
        int wstatus;
        waitpid(pid, &wstatus, WUNTRACED);
        close(pipe_fd[PIPE_WRITE]);

        int n;
        if ((n = read(pipe_fd[PIPE_READ], response, MAX_SHELL_RESULT_LEN - 1) < 0)) {
            nakd_terminate("read()");
            goto ret;
        }
        response[MAX_SHELL_RESULT_LEN - 1] = 0;
        close(pipe_fd[PIPE_READ]);

        if (WIFEXITED(wstatus)) {
            status = WEXITSTATUS(wstatus);
            nakd_log(L_DEBUG, "%s exited with status %d.", argv[1], status);
        }
    }

ret:
    if (response != NULL) {
        /* truncate */
        if (output != NULL)
            *output = strdup(response);
        free(response);
    }
    return status;
}

json_object *cmd_shell(json_object *jcmd, struct cmd_shell_spec *spec) {
    json_object *jresponse;
    json_object *jparams;
    const char **argv;
    const char **cleanup_argv = NULL;

    nakd_log_execution_point();
    nakd_assert(spec->argv[0] != NULL);
 
    /* argv defined in nakd take precedence over request */
    if (spec->argv[1] != NULL) {
        nakd_log(L_DEBUG, "Using predefined arguments for \"%s\"",
                                                   spec->argv[0]);
        argv = spec->argv;
    } else {
        if ((jparams = nakd_jsonrpc_params(jcmd)) == NULL ||
             json_object_get_type(jparams) != json_type_array) {
            nakd_log(L_NOTICE, "Couldn't get shell command arguments for %s",
                                                              spec->argv[0]);
            jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
                "Invalid parameters - params should be an array of strings");
            goto response;
        } else {
            argv = cleanup_argv = (const char **) build_argv_json(spec->argv[0],
                                                                       jparams);
        }
    }

    int status;
    char *output;
    if ((status = nakd_do_command_argv(argv, spec->cwd, &output)) < 0) {
        nakd_log(L_NOTICE, "Error while running shell command %s", spec->argv[0]);
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR, NULL);
        goto response;
    }
    json_object *jresult = json_object_new_object();
    json_object *joutput = json_object_new_string(output);
    free(output);
    json_object *jstatus = json_object_new_int(status);
    json_object_object_add(jresult, "output", joutput);
    json_object_object_add(jresult, "status", jstatus);
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    free_argv(cleanup_argv);
    nakd_log(L_DEBUG, "Returning response.");
    return jresponse;
}
