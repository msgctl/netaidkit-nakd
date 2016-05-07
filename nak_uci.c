#include <pthread.h>
#include "nak_uci.h"
#include "log.h"
#include "module.h"

static pthread_mutex_t _uci_mutex;
static struct uci_context *_uci_ctx = NULL;

static int _uci_init(void) {
    pthread_mutex_init(&_uci_mutex, NULL);
    _uci_ctx = uci_alloc_context();
    if (_uci_ctx == NULL)
        nakd_terminate("Couldn't initialize UCI context.");
    return 0;
}

static int _uci_cleanup(void) {
    pthread_mutex_destroy(&_uci_mutex);
    return 0;
}

static struct uci_package *__load_uci_package(const char *name) {
    struct uci_package *pkg = NULL;

    nakd_log(L_INFO, "Loading UCI package \"%s\"", name);
    nakd_assert(name != NULL);
    
    if (uci_load(_uci_ctx, name, &pkg)) {
        nakd_log(L_CRIT, "Couldn't load UCI package \"%s\"", name);
        return NULL;
    }
    return pkg;
}

struct uci_package *nakd_load_uci_package(const char *name) {
    pthread_mutex_lock(&_uci_mutex);
    struct uci_package *ret = __load_uci_package(name);
    pthread_mutex_unlock(&_uci_mutex);
    return ret;
}

static int _uci_option_single_cb(struct uci_option *option, void *priv) {
    struct uci_option **result = (struct uci_option **)(priv);
    *result = option;
    return 0;
}

struct uci_option *nakd_uci_option_single(const char *option_name) {
    struct uci_option *result = NULL;
    nakd_uci_option_foreach(option_name, _uci_option_single_cb, &result);
    return result;
}

static int __uci_save(struct uci_package *pkg) {
    nakd_log(L_INFO, "Saving UCI package \"%s\"", pkg->e.name);
    return uci_save(_uci_ctx, pkg);
}

int nakd_uci_save(struct uci_package *pkg) {
    pthread_mutex_lock(&_uci_mutex);
    int status = __uci_save(pkg);
    pthread_mutex_unlock(&_uci_mutex);
    return status;
}

static int __uci_commit(struct uci_package **pkg, bool overwrite) {
    nakd_log(L_DEBUG, "Commiting changes to UCI package \"%s\"", (*pkg)->e.name);
    return uci_commit(_uci_ctx, pkg, overwrite);
}

int nakd_uci_commit(struct uci_package **pkg, bool overwrite) {
    pthread_mutex_lock(&_uci_mutex);
    int status = __uci_commit(pkg, overwrite);
    pthread_mutex_unlock(&_uci_mutex);
    return status;
}

static int __unload_uci_package(struct uci_package *pkg) {
    nakd_log(L_DEBUG, "Unloading UCI package \"%s\"", pkg->e.name);
    return uci_unload(_uci_ctx, pkg);
}

int nakd_unload_uci_package(struct uci_package *pkg) {
    pthread_mutex_lock(&_uci_mutex);
    int status = __unload_uci_package(pkg);
    pthread_mutex_unlock(&_uci_mutex);
    return status;
}

/* Execute a callback for every option 'option_name', in selected UCI package */
static int _uci_option_foreach_pkg(const char *package, const char *option_name,
                                 nakd_uci_option_foreach_cb cb, void *cb_priv) {
    struct uci_element *sel;
    struct uci_section *section;
    struct uci_option *option;
    struct uci_package *uci_pkg;
    int cb_calls = 0;
    
    uci_pkg = __load_uci_package(package);
    if (uci_pkg == NULL)
        return 1;

    /*
     * Iterate through sections, ie.
     *  config redirect
     */
    uci_foreach_element(&uci_pkg->sections, sel) {
        section = uci_to_section(sel);

        option = uci_lookup_option(uci_pkg->ctx, section, option_name);
        if (option == NULL)
            continue;

        if (cb(option, cb_priv))
            return 1;
        else
            cb_calls++;
    }

    if (__uci_save(uci_pkg))
        return -1;

    /* nakd probably wouldn't recover from these */
    nakd_assert(!__uci_commit(&uci_pkg, true));
    nakd_assert(!__unload_uci_package(uci_pkg));
    return cb_calls;
}

/* Execute a callback for every option 'option_name' */
int nakd_uci_option_foreach(const char *option_name,
                      nakd_uci_option_foreach_cb cb,
                                    void *cb_priv) {
    int cb_calls = 0;
    pthread_mutex_lock(&_uci_mutex);

    char **uci_packages;
    if ((uci_list_configs(_uci_ctx, &uci_packages) != UCI_OK)) {
        nakd_log(L_CRIT, "Couldn't enumerate UCI packages");
        cb_calls = -1;
        goto unlock;
    }

    for (char **package = uci_packages; *package != NULL; package++) {
        int pkg_calls = _uci_option_foreach_pkg(*package, option_name,
                                                         cb, cb_priv);
        if (pkg_calls < 0) {
            cb_calls = -1;
            goto unlock;
        }
        cb_calls += pkg_calls;
    }

unlock:
    pthread_mutex_unlock(&_uci_mutex);
    free(uci_packages);
    return cb_calls;
}

int nakd_uci_option_foreach_pkg(const char *package, const char *option_name,
                              nakd_uci_option_foreach_cb cb, void *cb_priv) {
    pthread_mutex_lock(&_uci_mutex);
    int status = _uci_option_foreach_pkg(package, option_name, cb, cb_priv);
    pthread_mutex_unlock(&_uci_mutex);
    return status;
}

int nakd_uci_set(struct uci_context *ctx, struct uci_ptr *ptr) {
    pthread_mutex_lock(&_uci_mutex);
    int status = uci_set(ctx, ptr);
    pthread_mutex_unlock(&_uci_mutex);
    return status;
}

static struct nakd_module module_uci = {
    .name = "uci",
    .deps = NULL,
    .init = _uci_init,
    .cleanup = _uci_cleanup
};

NAKD_DECLARE_MODULE(module_uci);
