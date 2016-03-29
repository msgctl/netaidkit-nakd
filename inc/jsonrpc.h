#ifndef JSONRPC_H
#define JSONRPC_H

enum jsonrpc_err {
    PARSE_ERROR = -32700,
    INVALID_REQUEST = -32600,
    METHOD_NOT_FOUND = -32601,
    INVALID_PARAMS = -32602,
    INTERNAL_ERROR = -32603
};

json_object *nakd_jsonrpc_params(json_object *msg);
json_object *nakd_jsonrpc_id(json_object *id);
const char *nakd_jsonrpc_method(json_object *msg);
const char *nakd_jsonrpc_version(json_object *msg);
int nakd_jsonrpc_isversion(json_object *msg, const char *version);
int nakd_jsonrpc_validate_request(json_object *msg);
int nakd_jsonrpc_validate_notification(json_object *msg);
int nakd_jsonrpc_is_request(json_object *msg);
int nakd_jsonrpc_is_notification(json_object *msg);
int nakd_jsonrpc_has_id(json_object *msg);
int nakd_jsonrpc_is_batch(json_object *msg);
json_object *nakd_jsonrpc_response(json_object *request);
json_object *nakd_jsonrpc_response_success(json_object *request,
                                           json_object *result);
json_object *nakd_jsonrpc_response_error(json_object *request,
                    enum jsonrpc_err ec, const char *message);
json_object *nakd_jsonrpc_error(enum jsonrpc_err err, const char *message);

#endif
