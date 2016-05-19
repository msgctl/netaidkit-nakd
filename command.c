#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "command.h"
#include "shell.h"
#include "stage.h"
#include "log.h"
#include "openvpn.h"
#include "netintf.h"
#include "wlan.h"

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

struct nakd_command update = CMD_SHELL_NAKD("update", "do_update.sh");
NAKD_DECLARE_COMMAND(update);
