#include <stdio.h>
#include <stdlib.h>
#include "command.h"
#include "shell.h"
#include "stage.h"
#include "log.h"
#include "misc.h"
#include "openvpn.h"

static command commands[] = {
//    { "getapnam", get_ap_name, 0 },
    CMD_SHELL_NAKD_ARGV("wifiscan", "iwinfo.sh", "wlan0", "scan"),
    CMD_SHELL_NAKD("apconfig", "setup_ap.sh"),
    CMD_SHELL_NAKD("wificonn", "setup_wan.sh"),
    CMD_SHELL_NAKD("goonline", "go_online.sh"),
    CMD_SHELL_NAKD("inetstat", "get_inetstat.sh"),
    CMD_SHELL_NAKD("nrouting", "toggle_routing.sh"),
    CMD_SHELL_NAKD("wlaninfo", "wlan_info.sh"),
    CMD_SHELL_NAKD("setstage", "set_stage.sh"),
    CMD_SHELL_NAKD("getstage", "get_stage.sh"),
    CMD_SHELL_NAKD("stagetor", "toggle_tor.sh"),
    CMD_SHELL_NAKD("stagevpn", "toggle_vpn.sh"),
    CMD_SHELL_NAKD("doupdate", "do_update.sh"),
    CMD_SHELL_NAKD("broadcst", "toggle_broadcast.sh"),
    CMD_SHELL_NAKD("isportal", "detect_portal.sh"),
    { "stage", cmd_stage, NULL },
    { "openvpn", cmd_openvpn, NULL }
};

command *nakd_get_command(const char *cmd_name) {
    command *cmd = NULL;
    int i;

    for (i = 0; i < N_ELEMENTS(commands); i++) {
        if ((strcasecmp(cmd_name, commands[i].name)) == 0) {
            cmd = &commands[i];
            break;
        }
    }

    return cmd;
}

json_object *nakd_call_command(const char *cmd_name, json_object *jcmd) {
    nakd_log_execution_point();
    
    command *cmd = nakd_get_command(cmd_name);
    if (cmd == NULL) {
        nakd_log(L_NOTICE, "Couldn't find command %s.", cmd_name);
        return NULL;
    }

    return cmd->handler(jcmd, cmd->priv);
}
