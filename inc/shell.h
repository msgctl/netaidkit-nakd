#ifndef SHELL_H
#define SHELL_H
#include <json-c/json.h>
#include "command.h"

#define MAX_SHELL_RESULT_LEN 262144

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) (NAKD_SCRIPT_PATH filename)

char *nakd_do_command(const char **argv);
json_object *nakd_json_do_command(const char *script, json_object *jcmd);

struct cmd_shell_spec {
    const char **argv;
};

#define CMD_SHELL_ARGV(name, path, argv...) \
    { name, cmd_shell, &(struct cmd_shell_spec){ (const char*[]){ \
                                             path, argv, NULL } } }
#define CMD_SHELL(name, path) \
    { name, cmd_shell, &(struct cmd_shell_spec){ (const char*[]){ \
                                                   path, NULL } } }
#define CMD_SHELL_NAKD_ARGV(name, path, argv...) \
    { name, cmd_shell, &(struct cmd_shell_spec){ (const char*[]){ \
                                NAKD_SCRIPT(path), argv, NULL } } }
#define CMD_SHELL_NAKD(name, path) \
    { name, cmd_shell, &(struct cmd_shell_spec){ (const char*[]){ \
                                      NAKD_SCRIPT(path), NULL } } }


json_object *cmd_shell(json_object *jcmd, struct cmd_shell_spec *spec);

#endif
