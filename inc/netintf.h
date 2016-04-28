#ifndef NAKD_NETINTF_H
#define NAKD_NETINTF_H
#include <json-c/json.h>

void nakd_netintf_init(void);
void nakd_netintf_cleanup(void);

json_object *cmd_interface_state(json_object *jcmd, void *arg);

#endif
