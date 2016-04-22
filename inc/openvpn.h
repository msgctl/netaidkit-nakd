#ifndef NAKD_OPENVPN_H
#define NAKD_OPENVPN_H
#include <json-c/json.h>

int nakd_start_openvpn(void);
int nakd_stop_openvpn(void);
int nakd_restart_openvpn(void);
json_object *cmd_openvpn(json_object *jcmd, void *arg);

#endif
