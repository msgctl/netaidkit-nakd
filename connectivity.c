#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <json-c/json.h>
#include "connectivity.h"
#include "event.h"
#include "timer.h"
#include "netintf.h"
#include "wlan.h"
#include "log.h"
#include "module.h"
#include "workqueue.h"
#include "shell.h"
#include "jsonrpc.h"
#include "command.h"

#define CONNECTIVITY_SCRIPT_PATH(zone) NAKD_SCRIPT_PATH "connectivity/" zone
#define GW_ARPING_SCRIPT NAKD_SCRIPT("util/arping_gateway.sh")
#define GW_IP_SCRIPT NAKD_SCRIPT("util/gateway_ip.sh")
#define CONNECTIVITY_UPDATE_INTERVAL 10000 /* ms */

static pthread_mutex_t _connectivity_mutex;
static struct nakd_timer *_connectivity_update_timer;

static int _ethernet_wan_available(void) {
    if (!nakd_iface_state_available())
        return -1;
    if (nakd_carrier_present(NAKD_WAN))
        return 1;
    return 0;
}

static int _arping_gateway(void) {
    int status;
    nakd_assert((status = nakd_shell_exec(NAKD_SCRIPT_PATH,
                              NULL, GW_ARPING_SCRIPT " %s",
                              nakd_wlan_interface_name())) >= 0);
    return status;
}

static char *_gateway_ip(void) {
    char *ip = NULL;
    nakd_assert(nakd_shell_exec(NAKD_SCRIPT_PATH, &ip, GW_IP_SCRIPT) >= 0);
    return ip;
}

int nakd_online(void) {
    if (_ethernet_wan_available() == 1)
        return 1;

    if (!nakd_interface_disabled(NAKD_WLAN))
        return !_arping_gateway();
    return 0;
}

static void _connectivity_update(void *priv) {
    pthread_mutex_lock(&_connectivity_mutex);
    /* prefer ethernet */
    if (_ethernet_wan_available() != 0) {
        if (!nakd_interface_disabled(NAKD_WLAN))
            nakd_disable_interface(NAKD_WLAN);
        goto unlock; 
    }

    nakd_wlan_scan();
    nakd_log(L_DEBUG, "%d wireless networks available.", nakd_wlan_netcount());
    json_object *jcurrent = nakd_wlan_current();
    const char *current_ssid = NULL;
    if (jcurrent != NULL)
        current_ssid = nakd_net_ssid(jcurrent);

    int wan_disabled = nakd_interface_disabled(NAKD_WLAN);
    if (wan_disabled == -1) {
        nakd_log(L_CRIT, "Can't query WLAN interface UCI configuration.");
        goto unlock;
    } else if (!wan_disabled) {
        /* check if the network is still in range */
        if (current_ssid == NULL || !nakd_wlan_in_range(current_ssid)) {
            nakd_log(L_INFO, "\"%s\" WLAN is no longer in range.",
                                                    current_ssid);
            nakd_wlan_disconnect();
        } else {
            char *gw_ip = _gateway_ip(); 
            nakd_log(L_DEBUG, "\"%s\" WLAN is still in range,"
                       " arp-pinging the default gateway: %s",
                                         current_ssid, gw_ip);
            free(gw_ip);
            if (!_arping_gateway()) {
                nakd_log(L_DEBUG, "Gateway responsive.");
                goto unlock;
            } else {
                nakd_log(L_INFO, "Default gateway doesn't respond to ARP"
                                                               " ping.");
                nakd_wlan_disconnect();
            }
        }
    }

    nakd_log(L_INFO, "No Ethernet or wireless connection, looking for WLAN"
                                                            " candidate.");
    json_object *jnetwork = nakd_wlan_candidate();
    if (jnetwork == NULL) {
        nakd_log(L_INFO, "No available wireless networks");
        if (!wan_disabled)
            nakd_event_push(CONNECTIVITY_LOST);
        goto unlock;
    } 

    const char *ssid = nakd_net_ssid(jnetwork);
    nakd_log(L_INFO, "Connecting to wireless network \"%s\"", ssid);
    if (!nakd_wlan_connect(jnetwork)) {
        nakd_log(L_INFO, "Wireless connection configured, ssid: \"%s\"", ssid);
        nakd_event_push(CONNECTIVITY_OK);
    }

unlock:
    pthread_mutex_unlock(&_connectivity_mutex);
}

static struct work_desc _update_desc = {
    .impl = _connectivity_update,
    .name = "connectivity update",
};

static void _connectivity_update_sighandler(siginfo_t *timer_info,
                                       struct nakd_timer *timer) {
    /* skip, if there's already a pending update in the workqueue */
    if (!nakd_work_pending(nakd_wq, _update_desc.name)) {
        struct work *work = nakd_alloc_work(&_update_desc);
        nakd_workqueue_add(nakd_wq, work);
    }
}

static int _connectivity_init(void) {
    pthread_mutex_init(&_connectivity_mutex, NULL);
    _connectivity_update_timer = nakd_timer_add(CONNECTIVITY_UPDATE_INTERVAL,
                                      _connectivity_update_sighandler, NULL);

    nakd_event_push(CONNECTIVITY_LOST);

    struct work *update = nakd_alloc_work(&_update_desc);
    nakd_workqueue_add(nakd_wq, update);
    return 0;
}

static int _connectivity_cleanup(void) {
    nakd_timer_remove(_connectivity_update_timer);
    pthread_mutex_destroy(&_connectivity_mutex);
    return 0;
}

/* returns 1 if just one script returns with 0 exit status */
static int _run_connectivity_scripts(const char *dirpath) {
    int status = 0;
    DIR *dir = opendir(dirpath);

    if (dir == NULL) {
        nakd_log(L_CRIT, "Couldn't access %s (opendir(): %s)", dirpath,
                                                      strerror(errno));
        return 0;
    }

    char path[PATH_MAX];
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        snprintf(path, sizeof path, "%s/%s", dirpath, de->d_name);

        if (access(path, X_OK))
            continue;

        if (!nakd_shell_exec(NAKD_SCRIPT_PATH, NULL, path)) {
            status = 1;
            break;
        }
    }

    closedir(dir);
    return status;
}

int nakd_local_connectivity(void) {
    return _arping_gateway();
}

int nakd_internet_connectivity(void) {
    if (!nakd_local_connectivity())
        return 0;
    return _run_connectivity_scripts(CONNECTIVITY_SCRIPT_PATH("internet"));
}

enum nakd_connectivity nakd_connectivity(void) {
    if (nakd_internet_connectivity())
        return CONNECTIVITY_INTERNET;
    if (nakd_local_connectivity())
        return CONNECTIVITY_LOCAL;
    return CONNECTIVITY_NONE;
}

static struct nakd_module module_connectivity = {
    .name = "connectivity",
    .deps = (const char *[]){ "workqueue", "event", "timer", "netintf", "wlan",
                                   "notification" /* event handlers */, NULL },
    .init = _connectivity_init,
    .cleanup = _connectivity_cleanup 
};

NAKD_DECLARE_MODULE(module_connectivity);

json_object *cmd_connectivity(json_object *jcmd, void *arg) {
    json_object *jresponse;

    json_object *jresult = json_object_new_object();
    json_object *jlocal = json_object_new_boolean(nakd_local_connectivity());
    json_object *jinternet = json_object_new_boolean(
                       nakd_internet_connectivity());
    json_object_object_add(jresult, "local", jlocal);
    json_object_object_add(jresult, "internet", jinternet);

    return nakd_jsonrpc_response_success(jcmd, jresult);
}

static struct nakd_command connectivity = {
    .name = "connectivity",
    .desc = "Connectivity status - local: gateway, internet: probabilistic"
                   "based on a group of services that should be reachable "
                                                  "anywhere in the world.",
    .usage = "{\"jsonrpc\": \"2.0\", \"method\": \"connectivity\", \"params\":"
                                                           "\"\", \"id\": 42}",
    .handler = cmd_connectivity,
    .access = ACCESS_USER,
    .module = &module_connectivity
};
NAKD_DECLARE_COMMAND(connectivity);
