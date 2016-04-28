#include <string.h>
#include <pthread.h>
#include <json-c/json.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>
#include "netintf.h"
#include "jsonrpc.h"
#include "ubus.h"
#include "log.h"
#include "timer.h"

#define NETINTF_UBUS_SERVICE "network.device"
#define NETINTF_UBUS_METHOD "status"

#define NETINTF_UPDATE_INTERVAL 500 /* ms */

static json_object *_previous_netintf_state = NULL;
static json_object *_last_netintf_state = NULL;
static pthread_mutex_t _netintf_mutex;
static struct nakd_timer *_netintf_update_timer;

static void __netintf_diff(void) {

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
    _previous_netintf_state = _last_netintf_state;
    _last_netintf_state = jstate;
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

void nakd_netintf_init(void) {
    pthread_mutex_init(&_netintf_mutex, NULL);
    _netintf_update_timer = nakd_timer_add(NETINTF_UPDATE_INTERVAL,
                                 _netintf_update_sighandler, NULL);
}

void nakd_netintf_cleanup(void) {
    nakd_timer_remove(_netintf_update_timer);
    pthread_mutex_destroy(&_netintf_mutex);
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
