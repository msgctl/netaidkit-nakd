#include <pthread.h>
#include <json-c/json.h>
#include "connectivity.h"
#include "event.h"
#include "timer.h"
#include "netintf.h"
#include "wlan.h"
#include "log.h"
#include "thread.h"
#include "module.h"

#define CONNECTIVITY_UPDATE_INTERVAL 10000 /* ms */

static pthread_mutex_t _connectivity_mutex;
static struct nakd_timer *_connectivity_update_timer;
static struct nakd_thread *_connectivity_thread;
static pthread_cond_t _connectivity_cv;
static int _connectivity_shutdown;

static int _ethernet_wan_available(void) {
    if (!nakd_iface_state_available())
        return -1;
    if (nakd_carrier_present(NAKD_WAN))
        return 1;
    return 0;
}

static void _connectivity_update(void) {
    /* prefer ethernet */
    if (_ethernet_wan_available() != 0)
        return; /* skip if either present or don't know yet */

    if (!nakd_interface_disabled(NAKD_WLAN))
        return; /* skip if there's already a wifi connection */

    nakd_log(L_INFO, "No Ethernet or wireless connection, looking for WLAN"
                                                            " candidate.");

    json_object *jnetwork = nakd_wlan_candidate();
    if (jnetwork == NULL) {
        /* if there's no candidate, rescan */
        nakd_wlan_scan();
        /* retry */
        jnetwork = nakd_wlan_candidate();
        if (jnetwork == NULL) {
            nakd_log(L_INFO, "No available wireless networks");
            nakd_event_push(CONNECTIVITY_LOST);
            return;
        }
    } 

    const char *ssid = nakd_net_ssid(jnetwork);
    nakd_log(L_INFO, "Connecting to wireless network \"%s\"", ssid);
    if (!nakd_wlan_connect(jnetwork)) {
        nakd_log(L_INFO, "Wireless connection configured, ssid: \"%s\"", ssid);
        nakd_event_push(CONNECTIVITY_OK);
    }
}

static void _connectivity_update_sighandler(siginfo_t *timer_info,
                                       struct nakd_timer *timer) {
    /* this might take some time */
    pthread_cond_signal(&_connectivity_cv);
}

static void _connectivity_loop(struct nakd_thread *thread) {
    pthread_mutex_lock(&_connectivity_mutex);
    while (!_connectivity_shutdown) {
        pthread_cond_wait(&_connectivity_cv, &_connectivity_mutex);
        _connectivity_update();
    }
    pthread_mutex_unlock(&_connectivity_mutex);

}

static void _connectivity_shutdown_cb(struct nakd_thread *thread) {
    _connectivity_shutdown = 1;
}

static int _create_connectivity_thread(void) {
    /* TODO workqueue */
    nakd_log(L_DEBUG, "Creating connectivity thread.");
    if (nakd_thread_create_joinable(_connectivity_loop,
                             _connectivity_shutdown_cb,
                        NULL, &_connectivity_thread)) {
        nakd_log(L_CRIT, "Couldn't create connectivity thread.");
        return 1;
    }
    return 0;
}

static int _connectivity_init(void) {
    pthread_mutex_init(&_connectivity_mutex, NULL);
    pthread_cond_init(&_connectivity_cv, NULL);
    _connectivity_update_timer = nakd_timer_add(CONNECTIVITY_UPDATE_INTERVAL,
                                      _connectivity_update_sighandler, NULL);
    _create_connectivity_thread();
    return 0;
}

static int _connectivity_cleanup(void) {
    nakd_timer_remove(_connectivity_update_timer);
    nakd_thread_kill(_connectivity_thread);
    pthread_cond_destroy(&_connectivity_cv);
    pthread_mutex_destroy(&_connectivity_mutex);
}

static struct nakd_module module_connectivity = {
    .name = "connectivity",
    .deps = (const char *[]){ "thread", "event", "timer", "netintf", "wlan",
                                                                     NULL },
    .init = _connectivity_init,
    .cleanup = _connectivity_cleanup 
};

NAKD_DECLARE_MODULE(module_connectivity);
