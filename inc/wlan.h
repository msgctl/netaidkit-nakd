#ifndef NAKD_WLAN_H
#define NAKD_WLAN_H
#include <json-c/json.h>

json_object *nakd_wlan_candidate(void);
int nakd_wlan_scan(void);
int nakd_wlan_connect(json_object *jnetwork);
int nakd_wlan_disconnect(void);

json_object *cmd_wlan_list(json_object *jcmd, void *arg);
json_object *cmd_wlan_scan(json_object *jcmd, void *arg);
json_object *cmd_wlan_connect(json_object *jcmd, void *arg);

#endif
