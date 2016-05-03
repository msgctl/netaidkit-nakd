#ifndef NAKD_NETINTF_H
#define NAKD_NETINTF_H
#include <json-c/json.h>

enum nakd_interface {
    INTF_UNSPECIFIED,
    NAKD_LAN,
    NAKD_WAN,
    NAKD_WLAN,
    NAKD_AP
};

extern const char *nakd_uci_interface_tag[];
extern const char *nakd_uci_interface_name[];

int nakd_carrier_present(enum nakd_interface id);

json_object *cmd_interface_state(json_object *jcmd, void *arg);

#endif
