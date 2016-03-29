#include <strings.h>
#include <json-c/json.h>
#include "jsonrpc.h"
#include "log.h"

#define JSONRPC_VERSION "2.0"

static const char *_jsonrpc_errmsg(enum jsonrpc_err err) {
    switch (err) {
        case PARSE_ERROR:
            return "Parse error";
        case INVALID_REQUEST:
            return "Invalid request";
        case METHOD_NOT_FOUND:
            return "Method not found";
        case INVALID_PARAMS:
            return "Invalid parameters";
        case INTERNAL_ERROR:
            return "Internal error";    
    }
    return "";
}

static const char *_get_string(json_object *msg, const char *key) {
    json_object *jstr = NULL;
    const char *str = NULL;

    json_object_object_get_ex(msg, key, &jstr);
    if (jstr != NULL) {
        enum json_type type = json_object_get_type(jstr);
        if (type == json_type_string)
            str = json_object_get_string(jstr);
    }
    return str;
}

static json_object *_init_version_string(json_object *msg) {
    json_object *jversion = json_object_new_string(JSONRPC_VERSION);
    nakd_assert(jversion != NULL);

    json_object_object_add(msg, "jsonrpc", jversion);
    return jversion; 
}

static int _keystrcmp(json_object *msg, const char *key, const char *value) {
    const char *_value = _get_string(msg, key);
    if (_value == NULL)
        return 0;

    return !strcasecmp(_value, value);
}

json_object *nakd_jsonrpc_params(json_object *msg) {
    json_object *jcmd = NULL;
    json_object_object_get_ex(msg, "params", &jcmd);
    return jcmd;
}

json_object *nakd_jsonrpc_id(json_object *msg) {
    json_object *jid = NULL;
    json_object_object_get_ex(msg, "id", &jid);
    return jid;
}

const char *nakd_jsonrpc_method(json_object *msg) {
    return _get_string(msg, "method");
}

const char *nakd_jsonrpc_version(json_object *msg) {
    return _get_string(msg, "jsonrpc");
}

int nakd_jsonrpc_isversion(json_object *msg, const char *version) {
    return _keystrcmp(msg, "jsonrpc", version);
}

int nakd_jsonrpc_validate_request(json_object *msg) {
    return nakd_jsonrpc_id(msg) != NULL &&
           nakd_jsonrpc_method(msg) != NULL &&
           nakd_jsonrpc_isversion(msg, JSONRPC_VERSION);
}

int nakd_jsonrpc_validate_notification(json_object *msg) {
    return nakd_jsonrpc_id(msg) == NULL &&
           nakd_jsonrpc_method(msg) != NULL &&
           nakd_jsonrpc_isversion(msg, JSONRPC_VERSION);
}

/* mutually exclusive */
int nakd_jsonrpc_is_request(json_object *msg) {
    return nakd_jsonrpc_validate_request(msg);
}

int nakd_jsonrpc_is_notification(json_object *msg) {
    return nakd_jsonrpc_validate_notification(msg);
}

int nakd_jsonrpc_has_id(json_object *msg) {
    return nakd_jsonrpc_id(msg) != NULL;
}

/* batch requests */
int nakd_jsonrpc_is_batch(json_object *msg) {
    return json_object_get_type(msg) == json_type_array;
}

json_object *nakd_jsonrpc_response(json_object *request) {
    json_object *jresponse = json_object_new_object();
    nakd_assert(jresponse != NULL);

    _init_version_string(jresponse);

    json_object *jid = NULL;
    if (request != NULL) {
        /* increase reference count w/o deep-copying */
        json_object_object_get_ex(request, "id", &jid);
    }

    /* Add "id" even if jid is NULL - json-c will insert a json null
       value here in this case. */
    json_object_object_add(jresponse, "id", jid); 

    return jresponse; 
}

json_object *nakd_jsonrpc_response_success(json_object *request,
                                          json_object *result) {
    nakd_assert(request != NULL);
    nakd_assert(result != NULL);

    /* nakd_jsonrpc_response will return a valid pointer even if the request
       doesn't have an id to handle PARSE_ERROR and INVALID_REQUEST. */
    if (!nakd_jsonrpc_has_id(request))
        return NULL;

    json_object *jresp = nakd_jsonrpc_response(request);
    if (jresp == NULL)
        return NULL;

    json_object_object_add(jresp, "result", result);
    return jresp;
}

/* message can be NULL */
json_object *nakd_jsonrpc_response_error(json_object *request,
                   enum jsonrpc_err ec, const char *message) {
    json_object *jresp = nakd_jsonrpc_response(request);
    json_object *jerr = nakd_jsonrpc_error(ec, message);
    json_object_object_add(jresp, "error", jerr);
    return jresp;
}

/* message can be NULL */
json_object *nakd_jsonrpc_error(enum jsonrpc_err err, const char *message) {
    json_object *jerror = json_object_new_object();
    nakd_assert(jerror != NULL);

    json_object *jcode = json_object_new_int((int)(err));
    nakd_assert(jcode != NULL);

    json_object_object_add(jerror, "code", jcode);

    const char *errstr = message == NULL ? _jsonrpc_errmsg(err) : message;
    json_object *jmessage = json_object_new_string(errstr);
    nakd_assert(jmessage != NULL);

    json_object_object_add(jerror, "message", jmessage);
    return jerror;
}
