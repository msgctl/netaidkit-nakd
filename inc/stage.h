#ifndef NAKD_STAGE_H
#define NAKD_STAGE_H
#include <json-c/json.h>
#include "nak_uci.h"
#include "hooks.h"

struct stage;
typedef int (*stage_work)(struct stage *stage);
typedef json_object *(*stage_info)(struct stage *stage);

struct stage_step {
    const char *name;
    const char *desc;
    stage_work work;
};

struct stage {
    const char *name;
    const char *desc;
    const struct stage_step *work;

    struct nakd_uci_hook *hooks;

    /* user-friendly error message */
    char *err;
};

int nakd_stage_init(void);
int nakd_stage_spec(struct stage *stage);
int nakd_stage(const char *stage_name);

json_object *cmd_stage(json_object *jcmd, void *);

#endif
