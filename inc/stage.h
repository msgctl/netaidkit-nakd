#ifndef STAGE_H
#define STAGE_H
#include <json-c/json.h>

int nakd_call_stage_hooks(const char *stage);
json_object *cmd_stage(json_object *jcmd, void *);

#endif
