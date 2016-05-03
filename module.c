#include <string.h>
#include "module.h"
#include "log.h"

static void _init_module(struct nakd_module *module);
static void _init_module_byname(const char *name);

/* see: module.ld, module.h */
extern struct nakd_module *__nakd_module_list[];

static struct nakd_module *_module_byname(const char *name) {
    for (struct nakd_module **module = __nakd_module_list; *module; module++) {
        if (!strcmp((*module)->name, name))
            return *module;
    }
    return NULL;
}

static void _init_module(struct nakd_module *module) {
    module->initialized = 1;

    if (module->deps != NULL) {
        for (const char **name = module->deps; *name; name++) {
            struct nakd_module *depm = _module_byname(*name);
            nakd_assert(depm != NULL);

            if (depm->initialized)
                continue;

            nakd_log(L_DEBUG, "Initializing %s dependency: %s.", module->name,
                                                                       *name);
            _init_module(depm);
        }
    }

    if (module->init()) {
        nakd_terminate("Couldn't initialize module %s.", module->name);
    } else {
        nakd_log(L_DEBUG, "Initialized module %s.", module->name);
    }
}

static void _cleanup_module(struct nakd_module *module) {
    module->initialized = 0;

    /* Clean up dependent modules first */
    for (struct nakd_module **depm = __nakd_module_list; *depm; depm++) {
        if (!(*depm)->initialized || (*depm)->deps == NULL)
            continue;

        for (const char **depname = (*depm)->deps; *depname; depname++) {
            if (!strcmp(*depname, module->name)) {
                nakd_log(L_DEBUG, "Cleaning up %s dependent module: %s", module->name,
                                                                            *depname);
                _cleanup_module(*depm);
                continue;
            }
        }
    }

    nakd_log(L_DEBUG, "Cleaning up module: %s", module->name);
    nakd_assert(!module->cleanup());
}

void nakd_init_modules(void) {
    for (struct nakd_module **module = __nakd_module_list; *module; module++)
        _init_module(*module);
}

void nakd_cleanup_modules(void) {
    for (struct nakd_module **module = __nakd_module_list; *module; module++)
        _cleanup_module(*module);
}
