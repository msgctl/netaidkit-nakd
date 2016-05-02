#ifndef NAKD_SHELL_H
#define NAKD_SHELL_H
#include <json-c/json.h>
#include "command.h"

#define MAX_SHELL_RESULT_LEN 262144

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) (NAKD_SCRIPT_PATH filename)

char *nakd_do_command(const char *args, const char *cwd);
char *nakd_do_command_argv(const char **argv, const char *cwd);
json_object *nakd_json_do_command(const char *script, json_object *jcmd);

struct cmd_shell_spec {
    const char **argv;
    const char *cwd;
};

#define CMD_SHELL_ARGV(name, cwd, path, argv...) \
    { name, (cmd_handler)(cmd_shell), &(struct cmd_shell_spec) \
    { (const char*[]){ path, argv, NULL }, cwd } }
#define CMD_SHELL(name, cwd, path) \
    { name, (cmd_handler)(cmd_shell), &(struct cmd_shell_spec) \
    { (const char*[]){ path, NULL }, cwd } }
#define CMD_SHELL_NAKD_ARGV(name, path, argv...) \
    { name, (cmd_handler)(cmd_shell), &(struct cmd_shell_spec) \
    { (const char*[]){ NAKD_SCRIPT(path), argv, NULL }, NAKD_SCRIPT_PATH } }
#define CMD_SHELL_NAKD(name, path) \
    { name, (cmd_handler)(cmd_shell), &(struct cmd_shell_spec) \
    { (const char*[]){ NAKD_SCRIPT(path), NULL }, NAKD_SCRIPT_PATH } }

json_object *cmd_shell(json_object *jcmd, struct cmd_shell_spec *spec);

#endif
