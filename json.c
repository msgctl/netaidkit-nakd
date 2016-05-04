#include <json-c/json.h>
#include <string.h>
#include "json.h"
#include "log.h"

/* json-c isn't thread-safe, and AFAIK it's the only way of making a deep
 * copy. Let's come back to it later.
 */
json_object *nakd_json_deepcopy(json_object *object) {
    const char *object_str = json_object_get_string(object);
    json_tokener *jtok = json_tokener_new();
    json_object *jresult = json_tokener_parse_ex(jtok, object_str,
                                              strlen(object_str));
    nakd_assert(json_tokener_get_error(jtok) == json_tokener_success);
    nakd_assert(jresult != NULL);
    json_tokener_free(jtok);
    return jresult;
}
