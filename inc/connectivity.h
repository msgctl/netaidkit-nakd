#ifndef NAKD_CONNECTIVITY_H
#define NAKD_CONNECTIVITY_H
#include <json-c/json.h>

enum nakd_connectivity {
    CONNECTIVITY_NONE,
    CONNECTIVITY_LOCAL,
    CONNECTIVITY_INTERNET
};

int nakd_online(void);

int nakd_local_connectivity(void);
int nakd_internet_connectivity(void);
enum nakd_connectivity nakd_connectivity(void);

json_object *cmd_connectivity(json_object *jcmd, void *arg);

#endif
