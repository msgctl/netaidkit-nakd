#include <string.h>
#include <json-c/json.h>
#include "message.h"
#include "command.h"
#include "misc.h"
#include "log.h"

typedef json_object *(*msg_handler)(json_object *jmsg);

static const char *msg_type_str[] = {
    [MSG_TYPE_UNKNOWN] = "UNKNOWN",
    [MSG_TYPE_COMMAND] = "COMMAND",
    [MSG_TYPE_REPLY] = "REPLY"
};

static const char *msg_status_str[] = {
    [MSG_STATUS_UNKNOWN] = "UNKNOWN",
    [MSG_STATUS_SUCCESS] = "SUCCESS",
    [MSG_STATUS_ERROR] = "ERROR"
};

const char *nakd_message_type_str(msg_type type) {
    return msg_type_str[type];
}

msg_type nakd_message_type(const char *typestr) {
   const char **iter = msg_type_str;
   for (; iter < ARRAY_END(msg_type_str); iter++) {
        if (!strcmp(typestr, *iter))
            return (msg_type)(ARRAY_ELEMENT_NUMBER(iter, msg_type_str));
   }
   return MSG_TYPE_UNKNOWN;
}

const char *nakd_message_status_str(msg_status status) {
    return msg_status_str[status];
}

msg_status nakd_message_status(const char *statusstr) {
   const char **iter = msg_status_str;
   for (; iter < ARRAY_END(msg_status_str); iter++) {
        if (!strcmp(statusstr, *iter))
            return (msg_status)(ARRAY_ELEMENT_NUMBER(iter, msg_status_str));
   }
   return MSG_STATUS_UNKNOWN;
}

void nakd_message_set_status(json_object *jmsg, msg_status status) {
    json_object *jstatus = json_object_new_string(
        nakd_message_status_str(status));
    json_object_object_add(jmsg, "status", jstatus);
}

void nakd_message_set_type(json_object *jmsg, msg_type type) {
    json_object *jtype = json_object_new_string(
        nakd_message_type_str(type));
    json_object_object_add(jmsg, "type", jtype);
}

static json_object *handle_command(json_object *jcmd) {
    json_object *jcmd_name = NULL;
    json_object *result = NULL;
    const char *command_name; 
    command *command;

    json_object_object_get_ex(jcmd, "command", &jcmd_name);
    if (jcmd_name == NULL ||
        !json_object_is_type(jcmd_name, json_type_string)) {
        nakd_log(L_NOTICE, "Couldn't get command from JSON message.");
        return NULL;
    }
    command_name = json_object_get_string(jcmd_name);

    nakd_log(L_DEBUG, "Handling command \"%s\".", command_name);

    return nakd_call_command(command_name, jcmd);
}

static msg_handler msg_handlers[] = {
    [MSG_TYPE_UNKNOWN] = NULL,
    [MSG_TYPE_COMMAND] = handle_command,
    [MSG_TYPE_REPLY] = NULL
};

json_object *nakd_handle_message(json_object *jmsg) {
    json_object *jresponse = NULL;
    msg_type type;
    json_object *jtype = NULL;
    const char *typestr;
    msg_handler handler;

    json_object_object_get_ex(jmsg, "type", &jtype);

    if (jtype == NULL ||
        !json_object_is_type(jtype, json_type_string)) {
        nakd_log(L_NOTICE, "Couldn't get message type.");
        goto ret;
    }
    typestr = json_object_get_string(jtype);

    type = nakd_message_type(typestr);
    handler = msg_handlers[type];
    if (handler == NULL) {
        nakd_log(L_NOTICE, "Message type %s handler not implemented.", typestr);
        goto ret;
    }

    nakd_log(L_DEBUG, "Handling message of type \"%s\".", typestr);

    jresponse = handler(jmsg);

ret:
    if (jresponse == NULL) {
        jresponse = json_object_new_object();
        nakd_message_set_status(jresponse, MSG_STATUS_ERROR);
        nakd_log(L_DEBUG, "Couldn't handle message of type \"%s\". "
            "Replying with MSG_STATUS_ERROR.", typestr);
    }
    
    return jresponse;
}

