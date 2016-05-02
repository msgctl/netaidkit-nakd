#include "nak_uci.h"
#include "log.h"
#include "module.h"

static struct uci_context *_uci_ctx = NULL;

static int _uci_init(void) {
    _uci_ctx = uci_alloc_context();
    if (_uci_ctx == NULL)
        nakd_terminate("Couldn't initialize UCI context.");
    return 0;
}

static int _uci_cleanup(void) {
    return 0;
}

struct uci_package *nakd_load_uci_package(const char *name) {
    struct uci_package *pkg = NULL;

    nakd_log(L_INFO, "Loading UCI package \"%s\"", name);
    nakd_assert(name != NULL);
    
    if (uci_load(_uci_ctx, name, &pkg)) {
        nakd_log(L_CRIT, "Couldn't load UCI package \"%s\"", name);
        return NULL;
    }

    return pkg;
}

/* Execute a callback for every option 'option_name' */
int nakd_uci_option_foreach(const char *option_name,
                      nakd_uci_option_foreach_cb cb,
                                    void *cb_priv) {
    char **uci_packages;
    if ((uci_list_configs(_uci_ctx, &uci_packages) != UCI_OK)) {
        nakd_log(L_CRIT, "Couldn't enumerate UCI packages");
        return -1;
    }

    int cb_calls = 0;
    for (char **package = uci_packages; *package != NULL; package++) {
        int pkg_calls = nakd_uci_option_foreach_pkg(*package, option_name,
                                                             cb, cb_priv);
        if (pkg_calls < 0) {
            cb_calls = -1;
            goto free;
        }
        cb_calls += pkg_calls;
    }

free:
    free(uci_packages);
    return cb_calls;
}

/* Execute a callback for every option 'option_name', in selected UCI package */
int nakd_uci_option_foreach_pkg(const char *package, const char *option_name,
                              nakd_uci_option_foreach_cb cb, void *cb_priv) {
    struct uci_element *sel;
    struct uci_section *section;
    struct uci_option *option;
    struct uci_package *uci_pkg;
    int cb_calls = 0;
    
    uci_pkg = nakd_load_uci_package(package);
    if (package == NULL)
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

    if (nakd_uci_save(uci_pkg))
        return -1;

    /* nakd probably wouldn't recover from these */
    nakd_assert(!nakd_uci_commit(&uci_pkg, true));
    nakd_assert(!nakd_unload_uci_package(uci_pkg));
    return cb_calls;
}

int nakd_uci_save(struct uci_package *pkg) {
    nakd_log(L_INFO, "Saving UCI package \"%s\"", pkg->e.name);
    return uci_save(_uci_ctx, pkg);
}

int nakd_uci_commit(struct uci_package **pkg, bool overwrite) {
    nakd_log(L_DEBUG, "Commiting changes to UCI package \"%s\"", (*pkg)->e.name);
    return uci_commit(_uci_ctx, pkg, overwrite);
}

int nakd_unload_uci_package(struct uci_package *pkg) {
    nakd_log(L_DEBUG, "Unloading UCI package \"%s\"", pkg->e.name);
    return uci_unload(_uci_ctx, pkg);
}

static struct nakd_module module_uci = {
    .name = "uci",
    .deps = NULL,
    .init = _uci_init,
    .cleanup = _uci_cleanup
};

NAKD_DECLARE_MODULE(module_uci);
