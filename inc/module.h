#ifndef NAKD_MODULE_H
#define NAKD_MODULE_H
#include <pthread.h>

typedef int (*nakd_module_init)(void);
typedef int (*nakd_module_cleanup)(void);

enum nakd_module_state {
    NAKD_REMOVED,
    NAKD_REMOVING,
    NAKD_INITIALIZING,
    NAKD_INITIALIZED
};

struct nakd_module {
    const char *name;
    const char **deps; /* must make a DAG */
    nakd_module_init init;
    nakd_module_cleanup cleanup;

    enum nakd_module_state state;
    pthread_mutex_t state_lock;
};

#define NAKD_DECLARE_MODULE(desc) \
    struct nakd_module * desc ## _ptr \
        __attribute__ ((section (".module"))) = &desc 

void nakd_init_modules(void);
void nakd_cleanup_modules(void);

int nakd_module_state(struct nakd_module *module);

#endif
