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

static void _connectivity_update(void *priv) {
    pthread_mutex_lock(&_connectivity_mutex);
    /* prefer ethernet */
    if (_ethernet_wan_available() != 0)
        goto unlock; /* skip if either present or don't know yet */

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
            nakd_disable_interface(NAKD_WLAN);
            nakd_event_push(CONNECTIVITY_LOST);
        } else {
            nakd_log(L_INFO, "\"%s\" WLAN is still in range.",
                                                current_ssid);
            goto unlock;
        }
    }

    nakd_log(L_INFO, "No Ethernet or wireless connection, looking for WLAN"
                                                            " candidate.");
    json_object *jnetwork = nakd_wlan_candidate();
    if (jnetwork == NULL) {
        nakd_log(L_INFO, "No available wireless networks");
        if (!wan_disabled)
            nakd_wlan_disconnect();
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

static struct work _update = {
    .impl = _connectivity_update,
    .name = "connectivity update"
};

static void _connectivity_update_sighandler(siginfo_t *timer_info,
                                       struct nakd_timer *timer) {
    /* skip, if there's already a pending update in the workqueue */
    if (!nakd_work_pending(nakd_wq, _update.name))
        nakd_workqueue_add(nakd_wq, &_update);
}

static int _connectivity_init(void) {
    pthread_mutex_init(&_connectivity_mutex, NULL);
    _connectivity_update_timer = nakd_timer_add(CONNECTIVITY_UPDATE_INTERVAL,
                                      _connectivity_update_sighandler, NULL);
    nakd_workqueue_add(nakd_wq, &_update);
    return 0;
}

static int _connectivity_cleanup(void) {
    nakd_timer_remove(_connectivity_update_timer);
    pthread_mutex_destroy(&_connectivity_mutex);
}

static struct nakd_module module_connectivity = {
    .name = "connectivity",
    .deps = (const char *[]){ "workqueue", "event", "timer", "netintf", "wlan",
                                                                        NULL },
    .init = _connectivity_init,
    .cleanup = _connectivity_cleanup 
};

NAKD_DECLARE_MODULE(module_connectivity);
