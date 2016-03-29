#ifndef REQUEST_H
#define REQUEST_H
#include <json-c/json.h>

json_object *nakd_handle_message(json_object *jmsg);
json_object *nakd_handle_single(json_object *jmsg);
json_object *nakd_handle_batch(json_object *jmsg);

#endif
