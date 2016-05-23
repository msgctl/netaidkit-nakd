#ifndef NAKD_STAGE_H
#define NAKD_STAGE_H
#include <json-c/json.h>
#include "nak_uci.h"
#include "hooks.h"
#include "connectivity.h"
#include "led.h"

struct stage;
typedef int (*stage_work)(struct stage *stage);

struct stage_step {
    const char *name;
    const char *desc;
    stage_work work;
};

struct stage {
    const char *name;
    const char *desc;
    const struct stage_step *work;
    enum nakd_connectivity connectivity_level;
    struct led_condition led;

    struct nakd_uci_hook *hooks;

    /* user-friendly error message */
    char *err;
};

json_object *cmd_stage_set(json_object *jcmd, void *param);

void nakd_stage_spec(struct stage *stage);
int nakd_stage(const char *stage_name);

#endif
