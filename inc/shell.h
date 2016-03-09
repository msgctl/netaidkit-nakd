#ifndef SHELL_H
#define SHELL_H
#include <json-c/json.h>
#include "command.h"

#define MAX_SHELL_RESULT_LEN 262144

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) (NAKD_SCRIPT_PATH filename)

char *nakd_do_command(const char *script, char **argv);
json_object *nakd_json_do_command(const char *script, json_object *jcmd);

struct cmd_shell_args {
    const char *path;
    const char ***argv;
};

/* GNU extensions used here */
#define CMD_SHELL_ARGV(name, path, argv...) \
    { name, cmd_shell, &(struct cmd_shell_args){ path, \
                   &(const char*[]){ argv, NULL } } }
#define CMD_SHELL(name, path) \
    { name, cmd_shell, &(struct cmd_shell_args){ path, \
                            &(const char*[]){ NULL } } }
#define CMD_SHELL_NAKD_ARGV(name, path, argv...) \
    { name, cmd_shell, &(struct cmd_shell_args){ NAKD_SCRIPT(path), \
                                &(const char*[]){ argv, NULL } } }
#define CMD_SHELL_NAKD(name, path) \
    { name, cmd_shell, &(struct cmd_shell_args){ NAKD_SCRIPT(path), \
                                         &(const char*[]){ NULL } } }

json_object *cmd_shell(json_object *jcmd, void *priv);

#endif
