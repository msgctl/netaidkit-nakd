#ifndef NAKD_WLAN_H
#define NAKD_WLAN_H
#include <json-c/json.h>

json_object *nakd_wlan_candidate(void);
int nakd_wlan_netcount(void);
int nakd_wlan_scan(void);
int nakd_wlan_connect(json_object *jnetwork);
int nakd_wlan_disconnect(void);

const char *nakd_net_key(json_object *jnetwork);
const char *nakd_net_ssid(json_object *jnetwork);

json_object *cmd_wlan_list_stored(json_object *jcmd, void *arg);
json_object *cmd_wlan_list(json_object *jcmd, void *arg);
json_object *cmd_wlan_scan(json_object *jcmd, void *arg);
json_object *cmd_wlan_connect(json_object *jcmd, void *arg);

#endif
