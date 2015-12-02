#ifndef MESSAGE_H
#define MESSAGE_H
#include <json-c/json.h>

typedef enum {
    MSG_TYPE_UNKNOWN,
    MSG_TYPE_COMMAND,
    MSG_TYPE_REPLY
} msg_type;

typedef enum {
    MSG_STATUS_UNKNOWN,
    MSG_STATUS_SUCCESS,
    MSG_STATUS_ERROR
} msg_status;

const char *nakd_message_type_str(msg_type type);
const char *nakd_message_status_str(msg_status status);
void nakd_message_set_type(json_object *jmsg, msg_type type);
msg_type nakd_message_type(const char *typestr);
void nakd_message_set_status(json_object *jmsg, msg_status status);
msg_status nakd_message_status(const char *statusstr);
json_object *nakd_handle_message(json_object *jmsg);

#endif
