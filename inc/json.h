#ifndef NAKD_JSON_H
#define NAKD_JSON_H
#include <json-c/json.h>

json_object *nakd_json_deepcopy(json_object *object);
const char *nakd_json_get_string(json_object *jobject, const char *key);

#endif
