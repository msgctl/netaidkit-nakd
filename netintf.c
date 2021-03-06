#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include "netintf.h"
#include "jsonrpc.h"
#include "json.h"
#include "ubus.h"
#include "log.h"
#include "timer.h"
#include "event.h"
#include "nak_uci.h"
#include "module.h"
#include "workqueue.h"
#include "command.h"

#define NETINTF_UBUS_SERVICE "network.device"
#define NETINTF_UBUS_METHOD "status"

#define NETINTF_UPDATE_INTERVAL 1000 /* ms */

/* eg. "option nak_lan_tag 1" for wired lan interface */
const char *nakd_uci_interface_tag[] = {
    [INTF_UNSPECIFIED] = "?",
    [NAKD_LAN] = "nak_lan_tag",
    [NAKD_WAN] = "nak_wan_tag",
    [NAKD_WLAN] = "nak_wlan_tag",
    [NAKD_AP] = "nak_ap_tag"
};

const char *nakd_interface_type[] = {
    [INTF_UNSPECIFIED] = "?",
    [NAKD_LAN] = "LAN",
    [NAKD_WAN] = "WAN",
    [NAKD_WLAN] = "WLAN",
    [NAKD_AP] = "AP"
};

static json_object *_previous_netintf_state = NULL;
static json_object *_current_netintf_state = NULL;
static struct nakd_timer *_netintf_update_timer;
static struct nakd_thread *_netintf_thread;

static pthread_cond_t _netintf_cv;
static pthread_mutex_t _netintf_mutex;

static int _netintf_updates_disabled;

struct carrier_event {
    /* eg. ETHERNET_WAN_PLUGGED */
    enum nakd_event event_carrier_present;
    /* eg. ETHERNET_LAN_LOST */
    enum nakd_event event_no_carrier;
};

struct interface {
    enum nakd_interface id;
    /* eg. eth1, filled by netintf code */
    char *name;

    struct carrier_event *carrier;
} static _interfaces[] = {
    {
        .id = NAKD_LAN,

        .carrier = &(struct carrier_event){
            .event_carrier_present = ETHERNET_LAN_PLUGGED,
            .event_no_carrier = ETHERNET_LAN_LOST
        }
    },
    {
        .id = NAKD_WAN,

        .carrier = &(struct carrier_event){
            .event_carrier_present = ETHERNET_WAN_PLUGGED,
            .event_no_carrier = ETHERNET_WAN_LOST
        }
    },
    {
        .id = NAKD_WLAN 
    },
    {
        .id = NAKD_AP
    },
    {}
};

int nakd_update_iface_config(enum nakd_interface id,
        nakd_uci_option_foreach_cb cb, void *priv) {
    /* Find interface tag, execute callback. */
    int tags_found = nakd_uci_option_foreach(
                  nakd_uci_interface_tag[id],
                                   cb, priv);
    if (tags_found < 0) {
        nakd_log(L_CRIT, "Couldn't read UCI interface tags.");
    } else if (!tags_found) {
        nakd_log(L_WARNING, "No UCI \"%s\" interface tags found.",
                                nakd_uci_interface_tag[id]);
    } else if (tags_found != 1) {
        nakd_log(L_WARNING, "Found more than one \"%s\" interface tag, "
                      "using interface \"%s\".", nakd_uci_interface_tag[
                                          id], nakd_interface_type[id]);
    } else {
        nakd_log(L_INFO, "Found \"%s\" interface tag. (intf: %s)",
                                       nakd_uci_interface_tag[id],
                                         nakd_interface_type[id]);
    }
    return tags_found;
}

static int _disable_interface(struct uci_option *option, void *priv) {
    struct interface *intf = priv;
    struct uci_section *ifs = option->section;
    struct uci_context *ctx = ifs->package->ctx;

    nakd_assert(ifs != NULL);
    nakd_assert(ctx != NULL);

    struct uci_ptr disabled_ptr = {
        .package = ifs->package->e.name,
        .section = ifs->e.name,
        .option = "disabled",
        .value = "1"
    };
    nakd_assert(!uci_set(ctx, &disabled_ptr));
}

int nakd_disable_interface(enum nakd_interface id) {
    int status = 0;

    nakd_log(L_INFO, "Disabling %s.", nakd_interface_type[id]);
    pthread_mutex_lock(&_netintf_mutex);

    if (nakd_update_iface_config(id, _disable_interface,
                                           NULL) != 1) {
        status = 1;
        goto unlock;
    }

unlock:
    pthread_mutex_unlock(&_netintf_mutex);
    return status;
}

static int _interface_disabled(struct uci_option *option, void *priv) {
    struct interface *intf = priv;
    struct uci_section *ifs = option->section;
    struct uci_context *ctx = ifs->package->ctx;

    nakd_assert(ifs != NULL);
    nakd_assert(ctx != NULL);

    const char *disabled = uci_lookup_option_string(ctx, ifs, "disabled");
    if (disabled == NULL) {
        *(int *)(priv) = 0; /* default */
        return 0;
    }

    *(int *)(priv) = atoi(disabled);
    return 0;
}

int nakd_interface_disabled(enum nakd_interface id) {
    int status;
    pthread_mutex_lock(&_netintf_mutex);
    if (nakd_update_iface_config(id, _interface_disabled,
                                         &status) != 1) {
        status = -1;
        goto unlock;
    }

unlock:
    pthread_mutex_unlock(&_netintf_mutex);
    return status;
}

static int _read_intf_config(struct uci_option *option, void *priv) {
    struct interface *intf = priv;
    struct uci_section *ifs = option->section;
    struct uci_context *ctx = ifs->package->ctx;
    const char *ifname = uci_lookup_option_string(ctx, ifs, "ifname");
    if (ifname == NULL) {
        nakd_log(L_WARNING, "UCI interface tag found, but there's no ifname "
                                                                 "defined.");
        return 1;
    }
    intf->name = strdup(ifname);
    return 0;
}

static void _read_config(void) {
    /* update interface->name with tags found in UCI */
    for (struct interface *intf = _interfaces; intf->id; intf++)
        nakd_update_iface_config(intf->id, _read_intf_config, intf);
}

static int __carrier_present(const char *intf) {
    json_object *jnode = NULL;
    json_object_object_get_ex(_current_netintf_state, intf, &jnode);

    if (jnode == NULL)
        return -1;

    json_object *jcarrier = NULL;
    json_object_object_get_ex(jnode, "carrier", &jcarrier);

    if (jcarrier == NULL)
        return -1;

    nakd_assert(json_object_get_type(jcarrier) == json_type_boolean);
    return json_object_get_boolean(jcarrier);
}

static char *__interface_name(enum nakd_interface id) {
    for (struct interface *intf = _interfaces; intf->id; intf++) {
        if (intf->id == id)
            return intf->name;
    }
    return NULL;
}

char *nakd_interface_name(enum nakd_interface id) {
    pthread_mutex_lock(&_netintf_mutex);
    char *name = __interface_name(id);
    pthread_mutex_unlock(&_netintf_mutex);
    return name;
}

int nakd_carrier_present(enum nakd_interface id) {
    int status = 1;

    pthread_mutex_lock(&_netintf_mutex);
    char *intf_name = __interface_name(id);
    if (intf_name == NULL) {
        nakd_log(L_CRIT, "There's no interface with id %s",
                                  nakd_interface_type[id]);
        status = -1;
        goto unlock;
    }

    status = __carrier_present(intf_name);

unlock:
    pthread_mutex_unlock(&_netintf_mutex);
    return status;
}

int nakd_iface_state_available(void) {
    pthread_mutex_lock(&_netintf_mutex);
    int s = _current_netintf_state != NULL;
    pthread_mutex_unlock(&_netintf_mutex);
    return s;
}

static void __push_carrier_events(void) {
    if (_previous_netintf_state == NULL ||
           _current_netintf_state == NULL)
        return;

    for (struct interface *intf = _interfaces; intf->id; intf++) {
        if (intf->name == NULL || intf->carrier == NULL) 
            continue;

        json_object *jnode_previous = NULL;
        json_object_object_get_ex(_previous_netintf_state, intf->name,
                                                     &jnode_previous);
        if (jnode_previous == NULL)
            continue;

        json_object *jnode_current = NULL;
        json_object_object_get_ex(_current_netintf_state, intf->name,
                                                     &jnode_current);
        if (jnode_current == NULL) {
            nakd_log(L_CRIT, "An interface is missing from current "
                    NETINTF_UBUS_SERVICE " " NETINTF_UBUS_METHOD " "
                                                          "state.");
            continue;
        }

        json_object *jcarrier_previous = NULL;
        json_object_object_get_ex(jnode_previous, "carrier",
                                        &jcarrier_previous);
        if (jcarrier_previous == NULL)
            continue;
        nakd_assert(json_object_get_type(jcarrier_previous) ==
                                           json_type_boolean);

        json_object *jcarrier_current = NULL;
        json_object_object_get_ex(jnode_current, "carrier",
                                        &jcarrier_current);
        nakd_assert(jcarrier_current != NULL);
        nakd_assert(json_object_get_type(jcarrier_current) ==
                                          json_type_boolean);

        int carrier_previous = json_object_get_boolean(jcarrier_previous); 
        int carrier_current = json_object_get_boolean(jcarrier_current);

        enum nakd_event event_id = EVENT_UNSPECIFIED;
        if (carrier_previous && !carrier_current)
            event_id = intf->carrier->event_no_carrier;
        else if (!carrier_previous && carrier_current)
            event_id = intf->carrier->event_carrier_present;

        if (event_id != EVENT_UNSPECIFIED) {
            nakd_log(L_DEBUG, "Generating event: %s",
                          nakd_event_name[event_id]);
            nakd_event_push(event_id);
        }
    }
}

static void __netintf_diff(void) {
    __push_carrier_events();
}

static void _netintf_update_cb(struct ubus_request *req, int type,
                                          struct blob_attr *msg) {
    json_tokener *jtok = json_tokener_new();

    char *json_str = blobmsg_format_json(msg, true);
    nakd_assert(json_str != NULL);
    if (strlen(json_str) <= 2)
        goto badmsg;

    json_object *jstate = json_tokener_parse_ex(jtok, json_str, strlen(json_str));
    if (json_tokener_get_error(jtok) != json_tokener_success)
        goto badmsg;

    pthread_mutex_lock(&_netintf_mutex);
    if (_previous_netintf_state != NULL)
        json_object_put(_previous_netintf_state);
    _previous_netintf_state = _current_netintf_state;
    _current_netintf_state = jstate;
    __netintf_diff();    
    pthread_mutex_unlock(&_netintf_mutex);
    goto cleanup;

badmsg:
    nakd_log(L_WARNING, "Got an unusual response from " NETINTF_UBUS_SERVICE
                                 " " NETINTF_UBUS_METHOD ": %s.", json_str);
cleanup:
    free(json_str);
    json_tokener_free(jtok);
}

void nakd_netintf_disable_updates(void) {
    pthread_mutex_lock(&_netintf_mutex);
    _netintf_updates_disabled = 1;
    pthread_mutex_unlock(&_netintf_mutex);
    nakd_log(L_DEBUG, "Network interface state updates disabled.");
}

void nakd_netintf_enable_updates(void) {
    pthread_mutex_lock(&_netintf_mutex);
    _netintf_updates_disabled = 0;
    pthread_mutex_unlock(&_netintf_mutex);
    nakd_log(L_DEBUG, "Network interface state updates enabled.");
}

static void _netintf_update(void *priv) {
    pthread_mutex_lock(&_netintf_mutex);
    int updates_disabled = _netintf_updates_disabled;
    pthread_mutex_unlock(&_netintf_mutex);
    if (updates_disabled)
        return;

    nakd_ubus_call(NETINTF_UBUS_SERVICE, NETINTF_UBUS_METHOD, "{}", /* all */
                                         _netintf_update_cb, NULL);
}

static struct work_desc _update_desc = {
    .impl = _netintf_update,
    .name = "netintf update"
};

static void _netintf_update_sighandler(siginfo_t *timer_info,
                                  struct nakd_timer *timer) {
    /* skip, if there's already a pending update in the workqueue */
    if (!nakd_work_pending(nakd_wq, _update_desc.name)) {
        struct work *work = nakd_alloc_work(&_update_desc);
        nakd_workqueue_add(nakd_wq, work);
    }
}

static int _netintf_init(void) {
    pthread_mutex_init(&_netintf_mutex, NULL);
    _read_config();
    nakd_netintf_enable_updates();
    _netintf_update(NULL);
    _netintf_update_timer = nakd_timer_add(NETINTF_UPDATE_INTERVAL,
                                 _netintf_update_sighandler, NULL);
    return 0;
}

static int _netintf_cleanup(void) {
    nakd_netintf_disable_updates();
    nakd_timer_remove(_netintf_update_timer);
    pthread_mutex_destroy(&_netintf_mutex);
    return 0;
}

json_object *cmd_interface_state(json_object *jcmd, void *arg) {
    json_object *jresult;
    json_object *jresponse;

    pthread_mutex_lock(&_netintf_mutex);
    if (_current_netintf_state == NULL) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                          "Internal error - please try again later");
        goto unlock;
    }

    jresult = json_object_new_object();
    for (struct interface *intf = _interfaces; intf->id; intf++) {
        json_object *jstate = NULL;
        if (intf->name != NULL)
            json_object_object_get_ex(_current_netintf_state, intf->name,
                                                                &jstate);
        if (jstate == NULL) {
            nakd_log(L_DEBUG, "There's no %s interface in current interface "
                       "status, continuing.", nakd_interface_type[intf->id]);
        }

        json_object_object_add(jresult, nakd_interface_type[intf->id],
                                          nakd_json_deepcopy(jstate));
    }

    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

unlock:    
    pthread_mutex_unlock(&_netintf_mutex);
    return jresponse;
}

static struct nakd_module module_netintf = {
    .name = "netintf",
    .deps = (const char *[]){ "uci", "ubus", "event", "timer", "workqueue",
                                                                    NULL },
    .init = _netintf_init,
    .cleanup = _netintf_cleanup
};

NAKD_DECLARE_MODULE(module_netintf);

static struct nakd_command interfaces = {
    .name = "interfaces",
    .desc = "Returns current network interface state.",
    .usage = "{\"jsonrpc\": \"2.0\", \"method\": \"interfaces\", \"id\": 42}",
    .handler = cmd_interface_state,
    .access = ACCESS_USER,
    .module = &module_netintf
};
NAKD_DECLARE_COMMAND(interfaces);
