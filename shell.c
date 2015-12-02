#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <json-c/json.h>
#include "shell.h"
#include "message.h"
#include "log.h"

#define PIPE_READ       0
#define PIPE_WRITE      1

/* Will always at least return a pointer to {NULL} */
static char **parse_args(char *data) {
    char **args = malloc(sizeof(char *));
    char *line;
    int n_args = 0;

    strsep(&data, "\r\n");
    while ((line = strsep(&data, "\r\n")) != NULL) {
        if (strlen(line) < 1)
            continue;
        args = realloc(args, sizeof(char *) * (++n_args + 1));
        args[n_args - 1] = strdup(line);
    }

    args[n_args] = NULL;
    return args;
}

/* create {"/bin/sh", "script", args[0], ..., args[n], NULL} on heap */
static char **build_argv(const char *script, char *args[]) {
    int i, n_args = 0;
    char **argv = NULL;

    for (i = 0; args[i] != NULL; i++)
        n_args++;

    argv = malloc((n_args + 3) * sizeof(char *));
    argv[0] = strdup("/bin/sh");
    argv[1] = strdup(script);

    for (i = 0; args[i] != NULL; i++)
        argv[2+i] = strdup(args[i]);

    argv[2+i] = NULL;
    return argv;
}

static void free_argv(char **argv) {
    int i;

    for (i = 0; argv[i] != NULL; i++)
        free(argv[i]);

    free(argv);
}

/* Returns NULL if the command failed.
 * args must end with a NULL pointer.
 */
char *nakd_do_command(const char *script, char *args[]) {
    pid_t pid;
    int pipe_fd[2];
    char response[MAX_SHELL_RESULT_LEN + 1];

    memset(response, 0, MAX_SHELL_RESULT_LEN + 1);

    if (pipe(pipe_fd) == -1) {
        p_error("pipe()", "Could not create pipe.");
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        return NULL;
    } else if (pid == 0) { /* child */
        close(pipe_fd[PIPE_READ]);
        dup2(pipe_fd[PIPE_WRITE], 1);
        dup2(pipe_fd[PIPE_WRITE], 2);

        char **argv = build_argv(script, args);
        execve(argv[0], argv, NULL);

        free_argv(argv);
        p_error("execve()", "Could not execute command.");
        exit(-1);
    } else { /* parent */
        int n = 0;
        waitpid(pid, NULL, WUNTRACED);

        close(pipe_fd[PIPE_WRITE]);
        if ((n = read(pipe_fd[PIPE_READ], response, MAX_SHELL_RESULT_LEN) < 0)) {
            p_error("read()", "Could not read from pipe.");
            return NULL;
        }
        response[MAX_SHELL_RESULT_LEN] = 0;

        close(pipe_fd[PIPE_READ]);
    }

    return strdup(response);
}

static char **json_get_args(json_object *msg) {
    char *args;
    char **argv;
    json_object *jcmd = NULL;
    
    json_object_object_get_ex(msg, "args", &jcmd);

    if (jcmd == NULL ||
        !json_object_is_type(jcmd, json_type_string)) {
        nakd_log(L_NOTICE, "Couldn't get command line arguments from message.");
        return NULL;
    }

    args = strdup(json_object_get_string(jcmd));
    argv = parse_args(args);
    free(args);
    return argv;
}

json_object *nakd_json_do_command(const char *script, json_object *jcmd) {
    json_object *jresponse;
    json_object *jcmd_output;
    char *output;
    char **argv;
    
    jresponse = json_object_new_object();
    nakd_message_set_type(jresponse, MSG_TYPE_REPLY);

    if ((argv = json_get_args(jcmd)) == NULL) {
        nakd_log(L_NOTICE, "Couldn't get shell command arguments for %s", script);
        nakd_message_set_status(jresponse, MSG_STATUS_ERROR);
        goto response;
    }

    if ((output = nakd_do_command(script, argv)) == NULL) {
        nakd_log(L_NOTICE, "Error while running shell command %s", script);
        nakd_message_set_status(jresponse, MSG_STATUS_ERROR);
        goto response;
    }

    jcmd_output = json_object_new_string(output);
    json_object_object_add(jresponse, "result", jcmd_output);
    nakd_message_set_status(jresponse, MSG_STATUS_SUCCESS);

response:
    return jresponse;
}

json_object *cmd_shell(json_object *jcmd, void *priv) {
    const char *executable = priv;
    return nakd_json_do_command(executable, jcmd);
}
