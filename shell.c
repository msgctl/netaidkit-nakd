#include <stdio.h>
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
    char execve_log[1024];
    int format_len = 0;

    for (; *argv != NULL; argv++)
        format_len += snprintf(execve_log + format_len, sizeof(execve_log)
                                               - format_len, " %s", *argv);

    nakd_log(L_DEBUG, execve_log);
}

char *nakd_do_command(const char *args, const char *cwd) {
    const char **argv = (const char **)(build_argv(args));
    nakd_assert(argv != NULL);

    char *result = nakd_do_command_argv(argv, cwd);
    free_argv(argv);
    return result;
}

/* Returns NULL if the command failed.
 */
char *nakd_do_command_argv(const char **argv, const char *cwd) {
    pid_t pid;
    int pipe_fd[2];
    char *response = NULL;
    char *truncated = NULL;

    nakd_log_execution_point();
    log_execve((const char **)(argv));

    if (access(argv[0], X_OK)) {
        nakd_log(L_CRIT, "The file at %s isn't an executable.", argv[0]);
        return NULL;
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
        int n = 0;
        waitpid(pid, NULL, WUNTRACED);

        close(pipe_fd[PIPE_WRITE]);
        if ((n = read(pipe_fd[PIPE_READ], response, MAX_SHELL_RESULT_LEN - 1) < 0)) {
            nakd_terminate("read()");
            goto ret;
        }
        response[MAX_SHELL_RESULT_LEN - 1] = 0;

        close(pipe_fd[PIPE_READ]);
    }

ret:
    if (response != NULL) {
        truncated = strdup(response);
        free(response);
    }
    return truncated;
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

    char *output;
    if ((output = nakd_do_command_argv(argv, spec->cwd)) == NULL) {
        nakd_log(L_NOTICE, "Error while running shell command %s", spec->argv[0]);
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR, NULL);
        goto response;
    }
    json_object *jcmd_output = json_object_new_string(output);
    free(output);
    jresponse = nakd_jsonrpc_response_success(jcmd, jcmd_output);

response:
    free_argv(cleanup_argv);
    nakd_log(L_DEBUG, "Returning response.");
    return jresponse;
}
