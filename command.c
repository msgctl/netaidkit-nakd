#include <stdio.h>
#include <stdlib.h>
#include "command.h"
#include "misc.h"
#include "shell.h"
#include "log.h"

static command commands[] = {
//    { "getapnam", get_ap_name, 0 },
    { "wifiscan", cmd_shell,   "/nak/scripts/iwinfo.sh"        },
    { "apconfig", cmd_shell,   "/nak/scripts/setup_ap.sh"      },
    { "wificonn", cmd_shell,   "/nak/scripts/setup_wan.sh"     },
    { "goonline", cmd_shell,   "/nak/scripts/go_online.sh"     },
    { "inetstat", cmd_shell,   "/nak/scripts/get_inetstat.sh"  },
    { "nrouting", cmd_shell,   "/nak/scripts/toggle_routing.sh"},
    { "wlaninfo", cmd_shell,   "/nak/scripts/wlan_info.sh"     },
    { "setstage", cmd_shell,   "/nak/scripts/set_stage.sh"     },
    { "getstage", cmd_shell,   "/nak/scripts/get_stage.sh"     },
    { "stagetor", cmd_shell,   "/nak/scripts/toggle_tor.sh"    },
    { "stagevpn", cmd_shell,   "/nak/scripts/toggle_vpn.sh"    },
    { "doupdate", cmd_shell,   "/nak/scripts/do_update.sh"     },
    { "broadcst", cmd_shell,   "/nak/scripts/toggle_broadcast.sh"},
    { "isportal", cmd_shell,   "/nak/scripts/detect_portal.sh"   }
};

command *nakd_get_command(const char *cmd_name) {
    command *cmd = NULL;
    int i;

    for (i = 0; i < N_ELEMENTS(commands); i++) {
        if ((strcmp(cmd_name, commands[i].name)) == 0) {
            cmd = &commands[i];
            break;
        }
    }

    return cmd;
}

json_object *nakd_call_command(const char *cmd_name, json_object *jcmd) {
    command *cmd = nakd_get_command(cmd_name);

    if (cmd == NULL) {
        nakd_log(L_NOTICE, "Couldn't find command %s.", cmd_name);
        return NULL;
    }

    return cmd->handler(jcmd, cmd->priv);
}
