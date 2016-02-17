#ifndef SHELL_H
#define SHELL_H
#include <json-c/json.h>

#define MAX_SHELL_RESULT_LEN 4096

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) (NAKD_SCRIPT_PATH filename)

char *nakd_do_command(const char *script, char **argv);
json_object *nakd_json_do_command(const char *script, json_object *jcmd);

json_object *cmd_shell(json_object *jcmd, void *priv);

#endif
