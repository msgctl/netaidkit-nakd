#include <strings.h>
#include <uci.h>
#include "hooks.h"
#include "config.h"
#include "log.h"

static void _call_hook(struct nakd_uci_hook *hook, const char *state,
                                         struct uci_option *option) {
    hook->handler(hook->name, state, option);
}

struct hook_cb_data {
    const char *state;
    struct nakd_uci_hook *hook;
};

static int _hook_foreach_cb(struct uci_option *option,
                          struct hook_cb_data *priv) {
    struct uci_element *lel;

    if (option->type == UCI_TYPE_STRING) {
        if (!strcasecmp(option->v.string, priv->state))
            _call_hook(priv->hook, priv->state, option); 
    } else if (option->type == UCI_TYPE_LIST) {
        /*
         * ...and through options, which are in fact lists
         *  list nak_hooks_disable   'stage_online'
         *  list nak_hooks_disable   'stage_vpn'
         */
        uci_foreach_element(&option->v.list, lel) {
            if (!strcasecmp(lel->name, priv->state))
                _call_hook(priv->hook, priv->state, option);
        }
    } else {
        /* unreachable */
        nakd_assert(0 && "unreachable");
    }
    return 0;
}

int nakd_call_uci_hooks(struct nakd_uci_hook *hook_list, const char *state) {
    for (struct nakd_uci_hook *hook = hook_list; hook->name != NULL; hook++) {
        struct hook_cb_data cb_data = {
            .state = state,
            .hook = hook
        };

        int calls = nakd_uci_option_foreach(hook->name,
            (nakd_uci_option_foreach_cb)(_hook_foreach_cb),
                                                 &cb_data);
        if (calls < 0)
            return 1;
    
        nakd_log(L_DEBUG, "%s hook called %d times.", hook->name, calls);
    }
    return 0;
}
