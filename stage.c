#include <unistd.h>
#include <uci.h>
#include <string.h>
#include <linux/limits.h>
#include <json-c/json.h>
#include "stage.h"
#include "hooks.h"
#include "log.h"
#include "command.h"
#include "jsonrpc.h"
#include "shell.h"
#include "openvpn.h"

#define NAKD_STAGE_SCRIPT_PATH NAKD_SCRIPT_PATH "stage/"
#define NAKD_STAGE_SCRIPT_FMT (NAKD_STAGE_SCRIPT_PATH "%s" ".sh")

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

    struct uci_option *opt_enabled =
        uci_lookup_option(ctx, section, "enabled");

    const char *value = rule_disable ? "0" : "1";
    struct uci_ptr new_opt_enabled_ptr = {
        .package = option->section->package->e.name,
        .section = option->section->e.name,
        .option = "enabled",
        .value = value 
    };
    nakd_assert(!uci_set(ctx, &new_opt_enabled_ptr));
}

int nakd_run_stage_script(struct stage *stage) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, NAKD_STAGE_SCRIPT_FMT, stage->name);

    if (access(path, X_OK)) {
        nakd_log(L_DEBUG, "No executable script at %s, continuing.");
        return 0;
    }

    char *output = nakd_do_command(path);
    if (output != NULL) {
        nakd_log(L_DEBUG, "Stage script output: %s", output);
        free(output);
    }

    return output == NULL;
}

static int _start_openvpn(struct stage *stage) {
    return nakd_start_openvpn();
}

static int _stop_openvpn(struct stage *stage) {
    return nakd_stop_openvpn();
}

int nakd_run_uci_hooks(struct stage *stage) {
    return nakd_call_uci_hooks(stage->hooks, stage->name);
}

static struct nakd_uci_hook _firewall_hooks[] = {
    /* rewrite firewall rules */
    {"nak_rule_enable", toggle_rule},
    {"nak_rule_disable", toggle_rule},
    {NULL, NULL}
};

static struct stage _stages[] = {
    {
        .name = "stage_default",
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
                .work = nakd_run_uci_hooks 
           },
           { 
                .name = "Running stage shell script",
                .desc = "",
                .work = nakd_run_stage_script 
           },
           NULL
        },
        .hooks = _firewall_hooks,

        .err = NULL
    },
    {
        .name = "stage_online",
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
                .work = nakd_run_uci_hooks 
           },
           { 
                .name = "Running stage shell script",
                .desc = "",
                .work = nakd_run_stage_script 
           },
           NULL
        },
        .hooks = _firewall_hooks,

        .err = NULL
    },
    {
        .name = "stage_vpn",
        .desc = "",
        .work = (struct stage_step[]){
           { 
                .name = "Calling UCI hooks",
                .desc = "",
                .work = nakd_run_uci_hooks 
           },
           { 
                .name = "Running stage shell script",
                .desc = "",
                .work = nakd_run_stage_script 
           },
           { 
                .name = "Starting OpenVPN",
                .desc = "",
                .work = _start_openvpn
           },
           NULL           
        },
        .hooks = _firewall_hooks,

        .err = NULL
    },
    {
        .name = "stage_tor",
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
                .work = nakd_run_uci_hooks 
           },
           { 
                .name = "Running stage shell script",
                .desc = "",
                .work = nakd_run_stage_script 
           },
           NULL
        },
        .hooks = _firewall_hooks,

        .err = NULL
    },
    {}
};

static struct stage *_current_stage = NULL;
static struct stage *_default_stage = _stages;

int nakd_stage_init(void) {
    nakd_log_execution_point();

    if (_current_stage == NULL)
        return nakd_stage_spec(_default_stage);
    return 0;
}

int nakd_stage_spec(struct stage *stage) {
    nakd_log(L_INFO, "Stage %s", stage->name);

    _current_stage->err = NULL;
    _current_stage = stage;

    for (const struct stage_step *step = stage->work; step != NULL;
                                                          step++) {
        nakd_log(L_INFO, "Stage %s: running step %s", stage->name, step->name);
        if (step->work(stage))
            return 1;
    }
    nakd_log(L_INFO, "Stage %s: done!", stage->name);
    return 0;
}

int nakd_stage(const char *stage_name) {
    for (struct stage *stage = _stages; stage != NULL; stage++) {
        if (!strcmp(stage->name, stage_name))
            return nakd_stage_spec(stage);
    }
    return 1;
}

json_object *cmd_stage(json_object *jcmd, void *param) {
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
        const char *errstr = "Internal error while calling stage hooks";
        nakd_log(L_DEBUG, errstr);
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR, errstr);
    }

response:
    return jresponse;
}
