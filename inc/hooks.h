#ifndef NAKD_HOOKS_H
#define NAKD_HOOKS_H
#include "nak_uci.h"

struct nakd_uci_hook {
    const char *name;
    void (*handler)(const char *name, const char *state,
        struct uci_option *option);
};

int nakd_call_uci_hooks(struct nakd_uci_hook *hook_list, const char *state);

#endif
