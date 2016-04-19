#include <uci.h>
#include <string.h>
#include <json-c/json.h>
#include "stage.h"
#include "hooks.h"
#include "log.h"
#include "command.h"
#include "jsonrpc.h"

static void toggle_rule(const char *hook_name, const char *state,
                                    struct uci_option *option) {
    nakd_log_execution_point();
    nakd_assert(hook_name != NULL && state != NULL && option != NULL);
    
    struct uci_context *ctx = option->section->package->ctx;
    nakd_assert(ctx != NULL);

    struct uci_section *section = option->section;
    nakd_assert(section != NULL);

    const char *name = uci_lookup_option_string(ctx, section, "name");
    if (name == NULL)
        name = "";

    nakd_log(L_NOTICE, "%s firewall rule \"%s\"", strcasecmp(hook_name,
                        "nak_hooks_enable") ? "Disabling" : "Enabling",
                                                                 name);

    struct uci_option *opt_enabled =
        uci_lookup_option(ctx, section, "enabled");

    const char *value = strcasecmp(hook_name, "nak_hooks_enable")
                                                     ? "0" : "1";
    struct uci_ptr new_opt_enabled_ptr = {
        .package = option->section->package->e.name,
        .section = option->section->e.name,
        .option = "enabled",
        .value = value 
    };
    nakd_assert(!uci_set(ctx, &new_opt_enabled_ptr));
}

struct nakd_uci_hook rule_hooks[] = {
    {"nak_hooks_enable", toggle_rule},
    {"nak_hooks_disable", toggle_rule},
    {NULL, NULL}
};

int nakd_call_stage_hooks(const char *stage) {
    return nakd_call_uci_hooks("firewall", rule_hooks, stage);
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
    if (!nakd_call_stage_hooks(stage)) {
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
