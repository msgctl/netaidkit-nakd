#include <string.h>
#include <pthread.h>
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
    pthread_mutex_lock(&module->state_lock);
    module->state = NAKD_INITIALIZING;
    pthread_mutex_unlock(&module->state_lock);

    nakd_log(L_DEBUG, "Initializing module %s.", module->name);

    if (module->deps != NULL) {
        for (const char **name = module->deps; *name; name++) {
            struct nakd_module *depm = _module_byname(*name);
            nakd_assert(depm != NULL);

            pthread_mutex_lock(&module->state_lock);
            if (depm->state != NAKD_REMOVED) {
                pthread_mutex_unlock(&module->state_lock);
                continue;
            }
            pthread_mutex_unlock(&module->state_lock);

            nakd_log(L_DEBUG, "Initializing %s dependency: %s.", module->name,
                                                                       *name);
            _init_module(depm);
        }
    }

    if (module->init()) {
        nakd_terminate("Couldn't initialize module %s.", module->name);
    } else {
        pthread_mutex_lock(&module->state_lock);
        module->state = NAKD_INITIALIZED;
        pthread_mutex_unlock(&module->state_lock);
        nakd_log(L_DEBUG, "Initialized module %s.", module->name);
    }
}

static void _cleanup_module(struct nakd_module *module) {
    pthread_mutex_lock(&module->state_lock);
    module->state = NAKD_REMOVING;
    pthread_mutex_unlock(&module->state_lock);

    nakd_log(L_DEBUG, "Cleaning up module %s.", module->name);

    /* Clean up dependent modules first */
    for (struct nakd_module **depm = __nakd_module_list; *depm; depm++) {
        pthread_mutex_lock(&module->state_lock);
        if ((*depm)->state != NAKD_INITIALIZED) {
            pthread_mutex_unlock(&module->state_lock);
            continue;
        }
        pthread_mutex_unlock(&module->state_lock);

        for (const char **depname = (*depm)->deps; *depname; depname++) {
            if (!strcmp(*depname, module->name)) {
                nakd_log(L_DEBUG, "Cleaning up %s dependent module: %s", module->name,
                                                                       (*depm)->name);
                _cleanup_module(*depm);
                continue;
            }
        }
    }

    nakd_assert(!module->cleanup());
    pthread_mutex_lock(&module->state_lock);
    module->state = NAKD_REMOVED;
    pthread_mutex_unlock(&module->state_lock);
    nakd_log(L_DEBUG, "Cleaned up module: %s", module->name);
}

void nakd_init_modules(void) {
    for (struct nakd_module **module = __nakd_module_list; *module; module++)
        pthread_mutex_init(&(*module)->state_lock, NULL);

    for (struct nakd_module **module = __nakd_module_list; *module; module++) {
        if ((*module)->state != NAKD_REMOVED)
            continue;

        _init_module(*module);
    }
}

void nakd_cleanup_modules(void) {
    for (struct nakd_module **module = __nakd_module_list; *module; module++) {
        if ((*module)->state == NAKD_REMOVED)
            continue;

        _cleanup_module(*module);
    }

    for (struct nakd_module **module = __nakd_module_list; *module; module++)
        pthread_mutex_destroy(&(*module)->state_lock);
}

int nakd_module_state(struct nakd_module *module) {
    pthread_mutex_lock(&module->state_lock);
    int state = module->state;
    pthread_mutex_unlock(&module->state_lock);
    return state;
}
