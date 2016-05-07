#ifndef NAKD_NETINTF_H
#define NAKD_NETINTF_H
#include <json-c/json.h>
#include "nak_uci.h"

enum nakd_interface {
    INTF_UNSPECIFIED,
    NAKD_LAN,
    NAKD_WAN,
    NAKD_WLAN,
    NAKD_AP
};

int nakd_update_iface_config(enum nakd_interface id,
         nakd_uci_option_foreach_cb cb, void *priv);
int nakd_disable_interface(enum nakd_interface id);
char *nakd_interface_name(enum nakd_interface id);
int nakd_carrier_present(enum nakd_interface id);

json_object *cmd_interface_state(json_object *jcmd, void *arg);

extern const char *nakd_uci_interface_tag[];
extern const char *nakd_interface_type[];

#endif
