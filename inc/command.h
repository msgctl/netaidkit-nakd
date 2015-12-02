#ifndef COMMAND_H
#define COMMAND_H
#include <json-c/json.h>

typedef json_object *(*cmd_handler)(json_object *jcmd, void *priv);

typedef struct {
    char *name;
    cmd_handler handler;
    void *priv;
} command;

command *nakd_get_command(const char *cmd_name);
json_object *nakd_call_command(const char *cmd_name, json_object *jcmd);

#endif
