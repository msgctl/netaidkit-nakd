#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <json-c/json.h>
#include "command.h"
#include "shell.h"
#include "log.h"
#include "jsonrpc.h"

/* see: command.ld, command.h */
extern struct nakd_command *__nakd_command_list[];

struct nakd_command *nakd_get_command(const char *cmd_name) {
    for (struct nakd_command **command = __nakd_command_list; *command;
                                                           command++) {
        if (!strcmp(cmd_name, (*command)->name))
            return *command;
    }
    return NULL;
}

json_object *nakd_call_command(const char *cmd_name, json_object *jcmd) {
    struct nakd_command *cmd = nakd_get_command(cmd_name);
    if (cmd == NULL) {
        nakd_log(L_NOTICE, "Couldn't find command %s.", cmd_name);
        return NULL;
    }

    return cmd->handler(jcmd, cmd->priv);
}

static json_object *_desc_command(struct nakd_command *cmd) {
    json_object *jresult = json_object_new_object();
    
    if (cmd->name != NULL) {
        json_object *jname = json_object_new_string(cmd->name);
        json_object_object_add(jresult, "name", jname);
    }

    if (cmd->desc != NULL) {
        json_object *jdesc = json_object_new_string(cmd->desc);
        json_object_object_add(jresult, "description", jdesc);
    }

    if (cmd->usage != NULL) {
        json_object *jusage = json_object_new_string(cmd->usage);
        json_object_object_add(jresult, "usage", jusage);
    }
    return jresult;
}

json_object *cmd_list(json_object *jcmd, void *arg) {
    json_object *jresult = json_object_new_array();

    for (struct nakd_command **command = __nakd_command_list; *command;
                                                           command++) {
        json_object_array_add(jresult, _desc_command(*command));
    }
    return nakd_jsonrpc_response_success(jcmd, jresult);
}

struct nakd_command list = {
    .name = "list",
    .desc = "List available commands.",
    .usage = "{\"jsonrpc\": \"2.0\", \"method\": \"list\", \"id\": 42}",
    .handler = cmd_list,
    .access = ACCESS_USER
};
NAKD_DECLARE_COMMAND(list);

struct nakd_command update = CMD_SHELL_NAKD("update", "do_update.sh");
NAKD_DECLARE_COMMAND(update);
