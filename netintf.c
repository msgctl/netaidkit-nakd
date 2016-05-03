#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include "netintf.h"
#include "jsonrpc.h"
#include "ubus.h"
#include "log.h"
#include "timer.h"
#include "event.h"
#include "nak_uci.h"
#include "module.h"

#define NETINTF_UBUS_SERVICE "network.device"
#define NETINTF_UBUS_METHOD "status"

#define NETINTF_UPDATE_INTERVAL 500 /* ms */

static json_object *_previous_netintf_state = NULL;
static json_object *_current_netintf_state = NULL;
static pthread_mutex_t _netintf_mutex;
static struct nakd_timer *_netintf_update_timer;

struct intf_event {
    /* eg. "option nak_lan_tag 1" for wired lan interface */
    char *uci_tag;
    /* eg. eth1, filled by netintf code */
    char *intf_name;
    /* eg. ETHERNET_WAN_PLUGGED */
    enum nakd_event event_carrier_present;
    /* eg. ETHERNET_LAN_LOST */
    enum nakd_event event_no_carrier;
} static _intf_events[] = {
    {
        .uci_tag = "nak_lan_tag",
        .intf_name = NULL,
        .event_carrier_present = ETHERNET_LAN_PLUGGED,
        .event_no_carrier = ETHERNET_LAN_LOST
    },
    {
        .uci_tag = "nak_wan_tag",
        .intf_name = NULL,
        .event_carrier_present = ETHERNET_WAN_PLUGGED,
        .event_no_carrier = ETHERNET_WAN_LOST
    },
    {}
};

static int _update_intf_event(struct uci_option *option, void *priv) {
    struct intf_event *event = priv;
    struct uci_section *ifs = option->section;
    struct uci_context *ctx = ifs->package->ctx;
    const char *ifname = uci_lookup_option_string(ctx, ifs, "ifname");
    if (ifname == NULL) {
        nakd_log(L_WARNING, "UCI interface tag found, but there's no ifname "
                                                                 "defined.");
        return 1;
    }
    event->intf_name = strdup(ifname);
    return 0;
}

static void _load_interface_names(void) {
    /* update intf_event->intf_name with tags found in UCI */
    for (struct intf_event *event = _intf_events; event->uci_tag; event++) {
        int tags_found = nakd_uci_option_foreach(event->uci_tag,
                                     _update_intf_event, event);
        if (tags_found < 0) {
            nakd_log(L_CRIT, "Couldn't read UCI interface tags.");
        } else if (!tags_found) {
            nakd_log(L_WARNING, "No UCI \"%s\" interface tags found.",
                                                      event->uci_tag);
        } else if (tags_found != 1) {
            nakd_log(L_WARNING, "Found more than one \"%s\" interface tag, "
               "using interface \"%s\".", event->uci_tag, event->intf_name);
        } else {
            nakd_log(L_INFO, "Found \"%s\" interface tag. (intf: %s)",
                                    event->uci_tag, event->intf_name);
        }
    }
}

static void __push_carrier_events(void) {
    if (_previous_netintf_state == NULL ||
           _current_netintf_state == NULL)
        return;

    for (struct intf_event *event = _intf_events; event->uci_tag; event++) {
        if (event->intf_name == NULL) 
            continue;

        json_object *jnode_previous = NULL;
        json_object_object_get_ex(_previous_netintf_state, event->intf_name,
                                                           &jnode_previous);
        if (jnode_previous == NULL)
            continue;

        json_object *jnode_current = NULL;
        json_object_object_get_ex(_current_netintf_state, event->intf_name,
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
            event_id = event->event_no_carrier;
        else if (!carrier_previous && carrier_current)
            event_id = event->event_carrier_present;

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

static void _netintf_update_sighandler(siginfo_t *timer_info,
                                  struct nakd_timer *timer) {
    nakd_ubus_call(NETINTF_UBUS_SERVICE, NETINTF_UBUS_METHOD, "{}", /* all */
                                         _netintf_update_cb, NULL);
}

static int _netintf_init(void) {
    pthread_mutex_init(&_netintf_mutex, NULL);
    _load_interface_names();
    _netintf_update_timer = nakd_timer_add(NETINTF_UPDATE_INTERVAL,
                                 _netintf_update_sighandler, NULL);
    return 0;
}

static int _netintf_cleanup(void) {
    nakd_timer_remove(_netintf_update_timer);
    pthread_mutex_destroy(&_netintf_mutex);
    return 0;
}

json_object *cmd_interface_state(json_object *jcmd, void *arg) {
    json_object *jresponse;
    json_object *jparams;

    nakd_log_execution_point();
    if ((jparams = nakd_jsonrpc_params(jcmd)) == NULL ||
        json_object_get_type(jparams) != json_type_string) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
            "Invalid parameters - params should be a string");
        goto response;
    }

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    return jresponse;
}

static struct nakd_module module_netintf = {
    .name = "netintf",
    .deps = (const char *[]){ "uci", "ubus", "event", "timer", NULL },
    .init = _netintf_init,
    .cleanup = _netintf_cleanup
};

NAKD_DECLARE_MODULE(module_netintf);
