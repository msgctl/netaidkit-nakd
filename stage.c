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

static char *_run_stage_script(const char *stage) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, NAKD_STAGE_SCRIPT_FMT, stage);

    if (access(path, X_OK)) {
        nakd_log(L_DEBUG, "No executable script at %s, continuing.");
        return NULL;
    }

    return nakd_do_command(path);
}

static struct nakd_uci_hook _stage_hooks[] = {
    /* rewrite firewall rules */
    {"nak_rule_enable", toggle_rule},
    {"nak_rule_disable", toggle_rule},
    {NULL, NULL}
};

int nakd_stage(const char *stage) {
    nakd_log(L_INFO, "Stage %s", stage);

    if (nakd_call_uci_hooks(_stage_hooks, stage))
        return 1;

    char *result = _run_stage_script(stage);
    if (result != NULL) {
        nakd_log(L_DEBUG, "Stage script output: %s", result);
        free(result);
    }
    return 0;
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
