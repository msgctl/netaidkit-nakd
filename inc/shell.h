#ifndef SHELL_H
#define SHELL_H
#include <json-c/json.h>

#define MAX_SHELL_RESULT_LEN 4096

char *nakd_do_command(const char *script, char **argv);
json_object *nakd_json_do_command(const char *script, json_object *jcmd);

json_object *cmd_shell(json_object *jcmd, void *priv);

#endif
