#include <string.h>
#include <json-c/json.h>
#include "request.h"
#include "command.h"
#include "log.h"
#include "misc.h"
#include "jsonrpc.h"

json_object *nakd_handle_message(json_object *jmsg) {
    nakd_log_execution_point();
    nakd_assert(jmsg != NULL);

    if (nakd_jsonrpc_is_batch(jmsg))
        return nakd_handle_batch(jmsg);
    
    if (nakd_jsonrpc_is_request(jmsg) || nakd_jsonrpc_is_notification(jmsg))
        return nakd_handle_single(jmsg);

    return nakd_jsonrpc_response_error(jmsg, INVALID_REQUEST, NULL);
}

json_object *nakd_handle_single(json_object *jreq) {
    nakd_log_execution_point();
    nakd_assert(jreq != NULL);

    const char *method_name = nakd_jsonrpc_method(jreq);
    if (method_name == NULL) {
        nakd_log(L_WARNING, "Couldn't get method name from request");
        return nakd_jsonrpc_response_error(jreq, METHOD_NOT_FOUND, NULL);
    }
    
    nakd_log(L_DEBUG, "Handling request, method=\"%s\".", method_name);
    return nakd_call_command(method_name, jreq);
}

json_object *nakd_handle_batch(json_object *jmsg) {
    nakd_log_execution_point();
    nakd_assert(jmsg != NULL);

    json_object *jresponse = json_object_new_array();
    nakd_assert(jresponse != NULL);

    for (int i = 0; i < json_object_array_length(jmsg); i++) {
        /* TODO nice to have - process independently in workqueue */
        json_object *jsingle = json_object_array_get_idx(jmsg, i);
        json_object *jresult = nakd_handle_single(jsingle);
        if (jresult != NULL)
            json_object_array_add(jresponse, jresult);
    }
    return jresponse;
}
