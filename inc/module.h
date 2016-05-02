#ifndef NAKD_MODULE_H
#define NAKD_MODULE_H

typedef int (*nakd_module_init)(void);
typedef int (*nakd_module_cleanup)(void);

struct nakd_module {
    const char *name;
    const char **deps; /* must make a DAG */
    nakd_module_init init;
    nakd_module_cleanup cleanup;

    int initialized;
};

#define NAKD_DECLARE_MODULE(desc) \
    static struct nakd_module *_module_desc_ptr \
        __attribute__ ((section (".module"))) = &desc 

void nakd_init_modules(void);
void nakd_cleanup_modules(void);

#endif
