#include <json-c/json.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>
#include "netintf.h"
#include "jsonrpc.h"
#include "ubus.h"
#include "log.h"

static void _handler(struct ubus_request *req, int type,
                                struct blob_attr *msg) {
    nakd_log(L_DEBUG, blobmsg_format_json(msg, true));
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

    nakd_ubus_call("network.device", "status", "{}", _handler, NULL);

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);

response:
    return jresponse;
}
