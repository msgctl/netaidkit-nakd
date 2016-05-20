#include <unistd.h>
#include <string.h>
#include <linux/limits.h>
#include <pthread.h>
#include <json-c/json.h>
#include "stage.h"
#include "hooks.h"
#include "log.h"
#include "command.h"
#include "jsonrpc.h"
#include "shell.h"
#include "openvpn.h"
#include "nak_uci.h"
#include "module.h"
#include "connectivity.h"
#include "timer.h"
#include "workqueue.h"
#include "config.h"

#define NAKD_STAGE_SCRIPT_PATH NAKD_SCRIPT_PATH "stage/"
#define NAKD_STAGE_SCRIPT_DIR_FMT (NAKD_STAGE_SCRIPT_PATH "%s")

#define STAGE_UPDATE_INTERVAL 2500 /* ms */

static pthread_mutex_t _stage_mutex;
static struct nakd_timer *_stage_update_timer;

static void toggle_rule(const char *hook_name, const char *state,
                                      struct uci_option *option);
static struct nakd_uci_hook _firewall_hooks[] = {
    /* rewrite firewall rules */
    {"nak_rule_enable", toggle_rule},
    {"nak_rule_disable", toggle_rule},
    {NULL, NULL}
};

static int _run_stage_scripts(struct stage *stage);
static int _start_openvpn(struct stage *stage);
static int _stop_openvpn(struct stage *stage);
static int _run_uci_hooks(struct stage *stage);

static struct stage _stage_reset = {
    .name = "reset",
    .desc = "",
    .work = (struct stage_step[]){
       { 
            .name = "Stopping OpenVPN",
            .desc = "",
            .work = _stop_openvpn
       },
       { 
            .name = "Calling UCI hooks",
            .desc = "",
            .work = _run_uci_hooks 
       },
       { 
            .name = "Running stage shell script",
            .desc = "",
            .work = _run_stage_scripts 
       },
       {}
    },
    .hooks = _firewall_hooks,
    .connectivity_level = CONNECTIVITY_NONE,

    .err = NULL
};

static struct stage _stage_default = {
    .name = "default",
    .desc = "",
    .work = (struct stage_step[]){
       { 
            .name = "Stopping OpenVPN",
            .desc = "",
            .work = _stop_openvpn
       },
       { 
            .name = "Calling UCI hooks",
            .desc = "",
            .work = _run_uci_hooks 
       },
       { 
            .name = "Running stage shell script",
            .desc = "",
            .work = _run_stage_scripts 
       },
       {}
    },
    .hooks = _firewall_hooks,
    .connectivity_level = CONNECTIVITY_NONE,

    .err = NULL
};

static struct stage _stage_vpn = {
    .name = "vpn",
    .desc = "",
    .work = (struct stage_step[]){
       { 
            .name = "Calling UCI hooks",
            .desc = "",
            .work = _run_uci_hooks 
       },
       { 
            .name = "Running stage shell script",
            .desc = "",
            .work = _run_stage_scripts 
       },
       { 
            .name = "Starting OpenVPN",
            .desc = "",
            .work = _start_openvpn
       },
       {}
    },
    .hooks = _firewall_hooks,
    .connectivity_level = CONNECTIVITY_LOCAL,

    .err = NULL
};

static struct stage _stage_tor = {
    .name = "tor",
    .desc = "",
    .work = (struct stage_step[]){
       { 
            .name = "Stopping OpenVPN",
            .desc = "",
            .work = _stop_openvpn
       },
       { 
            .name = "Calling UCI hooks",
            .desc = "",
            .work = _run_uci_hooks 
       },
       { 
            .name = "Running stage shell script",
            .desc = "",
            .work = _run_stage_scripts 
       },
       {}
    },
    .hooks = _firewall_hooks,
    .connectivity_level = CONNECTIVITY_LOCAL,

    .err = NULL
};

static struct stage _stage_online = {
    .name = "online",
    .desc = "",
    .work = (struct stage_step[]){
       { 
            .name = "Stopping OpenVPN",
            .desc = "",
            .work = _stop_openvpn
       },
       { 
            .name = "Calling UCI hooks",
            .desc = "",
            .work = _run_uci_hooks 
       },
       { 
            .name = "Running stage shell script",
            .desc = "",
            .work = _run_stage_scripts 
       },
       {}
    },
    .hooks = _firewall_hooks,
    .connectivity_level = CONNECTIVITY_LOCAL,

    .err = NULL
};

static struct stage *_stages[] = {
    &_stage_reset,
    &_stage_default,
    &_stage_vpn,
    &_stage_tor,
    &_stage_online,
    NULL
};

static struct stage *_current_stage = NULL;
static struct stage *_requested_stage = NULL;

static void toggle_rule(const char *hook_name, const char *state,
                                    struct uci_option *option) {
    nakd_assert(hook_name != NULL && state != NULL && option != NULL);

    struct uci_context *ctx = option->section->package->ctx;
    nakd_assert(ctx != NULL);

    struct uci_section *section = option->section;
    nakd_assert(section != NULL);

    const char *name = uci_lookup_option_string(ctx, section, "name");
    if (name == NULL)
        name = "";

    int rule_disable = strcasecmp(hook_name, "nak_rule_enable");

    nakd_log(L_NOTICE, "%s rule \"%s\"", rule_disable ? "Disabling" :
                                                   "Enabling", name);

    const char *value = rule_disable ? "0" : "1";
    struct uci_ptr new_opt_enabled_ptr = {
        .package = option->section->package->e.name,
        .section = option->section->e.name,
        .option = "enabled",
        .value = value 
    };
    nakd_assert(!uci_set(ctx, &new_opt_enabled_ptr));
}

static int _run_stage_scripts(struct stage *stage) {
    char dirpath[PATH_MAX];
    snprintf(dirpath, sizeof dirpath, NAKD_STAGE_SCRIPT_DIR_FMT, stage->name);
    nakd_shell_run_scripts(dirpath);
}

static int _start_openvpn(struct stage *stage) {
    if (nakd_start_openvpn()) {
        stage->err = "Internal error while starting OpenVPN daemon";
        return 1;
    }
    return 0;
}

static int _stop_openvpn(struct stage *stage) {
    if (nakd_stop_openvpn()) {
        stage->err = "Internal error while stopping OpenVPN daemon";
        return 1;
    }
    return 0;
}

static int _run_uci_hooks(struct stage *stage) {
    if (nakd_call_uci_hooks(stage->hooks, stage->name)) {
        stage->err = "Internal error while rewriting UCI configuration";
        return 1;
    }
    return 0;
}

static void _stage_spec(void *priv) {
    struct stage *stage = *(struct stage **)(priv);

    enum nakd_connectivity current_connectivity = nakd_connectivity();
    if ((int)(current_connectivity) < (int)(stage->connectivity_level)) {
        /* wait for CONNECTIVITY_OK event */

        nakd_log(L_INFO, "Insufficient connectivity level for stage %s. "
          "(current: %s, required: %s) - change postponed.", stage->name,
                   nakd_connectivity_string[(int)(current_connectivity)],
             nakd_connectivity_string[(int)(stage->connectivity_level)]);
        return;
    }

    nakd_log(L_INFO, "Stage %s", stage->name);
    pthread_mutex_lock(&_stage_mutex);
    stage->err = NULL;
    for (const struct stage_step *step = stage->work; step->name != NULL;
                                                                step++) {
        nakd_log(L_INFO, "Stage %s: running step %s", stage->name, step->name);
        if (step->work(stage))
            goto unlock; /* stage->err set in step->work() */
    }

    _current_stage = stage;
    _current_stage->err = NULL;
    nakd_log(L_INFO, "Stage %s: done!", stage->name);

unlock:
    pthread_mutex_unlock(&_stage_mutex);
}

static struct work_desc _stage_work_desc = {
    .impl = _stage_spec,
    .name = "stage",
    .priv = &_requested_stage
};

static void _stage_update_cb(siginfo_t *timer_info,
                        struct nakd_timer *timer) {
    pthread_mutex_lock(&_stage_mutex);
    if (_current_stage != _requested_stage) {
        if (!nakd_work_pending(nakd_wq, _stage_work_desc.name)) {
            struct work *stage_wq_entry = nakd_alloc_work(&_stage_work_desc);
            nakd_workqueue_add(nakd_wq, stage_wq_entry);
        }
    }
    pthread_mutex_unlock(&_stage_mutex);
}

static struct stage *_get_stage(const char *name) {
    for (struct stage **stage = _stages; *stage != NULL; stage++) {
        if (!strcmp((*stage)->name, name))
            return *stage;
    }
    return NULL;
}

static int _stage_init(void) {
    pthread_mutex_init(&_stage_mutex, NULL);

    char *config_stage;
    nakd_config_key("stage", &config_stage);
    nakd_assert((_requested_stage = _get_stage(config_stage)) != NULL);

    _stage_update_timer = nakd_timer_add(STAGE_UPDATE_INTERVAL,
                                       _stage_update_cb, NULL);

    nakd_stage_spec(_requested_stage);
    return 0;
}

static int _stage_cleanup(void) {
    timer_delete(_stage_update_timer);
    pthread_mutex_destroy(&_stage_mutex);
    return 0;
}

void nakd_stage_spec(struct stage *stage) {
    pthread_mutex_lock(&_stage_mutex);
    _requested_stage = stage;
    nakd_config_set("stage", stage->name);
    pthread_mutex_unlock(&_stage_mutex);

    struct work *stage_wq_entry = nakd_alloc_work(&_stage_work_desc);
    nakd_workqueue_add(nakd_wq, stage_wq_entry);
}

int nakd_stage(const char *stage_name) {
    struct stage *stage = _get_stage(stage_name);
    if (stage == NULL) {
        nakd_log(L_CRIT, "No such stage: \"%s\".", stage_name);
        return 1;
    }

    nakd_stage_spec(stage);
    return 0;
}

json_object *cmd_stage_set(json_object *jcmd, void *param) {
    json_object *jresponse;

    nakd_log_execution_point();
    nakd_assert(jcmd != NULL);

    json_object *jparams = nakd_jsonrpc_params(jcmd);
    if (jparams == NULL || json_object_get_type(jparams) != json_type_string) {
        nakd_log(L_NOTICE, "Couldn't get stage parameter");
        jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
            "Invalid parameters - params should be a string");
        goto response;
    }

    const char *stage = json_object_get_string(jparams);
    if (!nakd_stage(stage)) {
        json_object *jresult = json_object_new_string("OK");
        jresponse = nakd_jsonrpc_response_success(jcmd, jresult);
    } else {
        const char *errstr = _current_stage->err != NULL ? _current_stage->err
                                      : "Internal error while changing stage";
        nakd_log(L_DEBUG, errstr);
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR, errstr);
    }

response:
    return jresponse;
}

static json_object *__desc_stage_step(struct stage_step *step) {
    json_object *jresult = json_object_new_object();
    json_object *jname = json_object_new_string(step->name);
    json_object *jdesc = json_object_new_string(step->desc);
    json_object_object_add(jresult, "name", jname);
    json_object_object_add(jresult, "desc", jdesc);
    return jresult;
}

static json_object *__desc_stage(struct stage *stage) {
    json_object *jresult = json_object_new_object();
    json_object *jname = json_object_new_string(stage->name);
    json_object *jdesc = json_object_new_string(stage->desc);
    json_object *jconnectivity = json_object_new_string(
        nakd_connectivity_string[stage->connectivity_level]);
    json_object *jerr = json_object_new_string(stage->err);
    json_object_object_add(jresult, "name", jname);
    json_object_object_add(jresult, "desc", jdesc);
    json_object_object_add(jresult, "connectivity", jconnectivity);
    json_object_object_add(jresult, "errmsg", jerr);
    return jresult;
}

json_object *cmd_stage_info(json_object *jcmd, void *param) {
    pthread_mutex_lock(&_stage_mutex);

    json_object *jresult;
    if (_current_stage != NULL)
        jresult = __desc_stage(_current_stage);
    else
        jresult = json_object_new_string("No stage set.");

    json_object *jresponse = nakd_jsonrpc_response_success(jcmd, jresult);
    pthread_mutex_unlock(&_stage_mutex);
    return jresponse;
}

static struct nakd_module module_stage = {
    .name = "stage",
    .deps = (const char *[]){ "workqueue", "connectivity", "notification",
                                                "timer", "config", NULL },
    .init = _stage_init,
    .cleanup = _stage_cleanup
};
NAKD_DECLARE_MODULE(module_stage);

static struct nakd_command stage_set = {
    .name = "stage_set",
    .desc = "Requests asynchronous change of NAK stage.",
    .usage = "{\"jsonrpc\": \"2.0\", \"method\": \"stage_set\", \"params\":"
                                                     "\"vpn\", \"id\": 42}",
    .handler = cmd_stage_set,
    .access = ACCESS_USER,
    .module = &module_stage
};
NAKD_DECLARE_COMMAND(stage_set);

static struct nakd_command stage_info = {
    .name = "stage_info",
    .desc = "Returns current stage together with possible error description.",
    .usage = "{\"jsonrpc\": \"2.0\", \"method\": \"stage_info\", \"id\": 42}",
    .handler = cmd_stage_info,
    .access = ACCESS_USER,
    .module = &module_stage
};
NAKD_DECLARE_COMMAND(stage_info);
