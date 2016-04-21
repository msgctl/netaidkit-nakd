#include <uci.h>
#include "config.h"
#include "log.h"

static struct uci_context *uci_ctx = NULL;

static int init_uci_ctx() {
    if (!uci_ctx)
        uci_ctx = uci_alloc_context();
    return !(uci_ctx != NULL);
}

struct uci_package *nakd_load_uci_package(const char *name) {
    struct uci_package *pkg = NULL;

    nakd_log(L_INFO, "Loading UCI package \"%s\"", name);
    nakd_assert(name != NULL);
    
    if (init_uci_ctx()) {
        nakd_log(L_CRIT, "Couldn't initialize UCI context");
        return NULL;
    }

    if (uci_load(uci_ctx, name, &pkg)) {
        nakd_log(L_CRIT, "Couldn't load UCI package \"%s\"", name);
        return NULL;
    }

    return pkg;
}

char **nakd_list_packages(void) {
    char **packages;

    if ((uci_list_configs(uci_ctx, &packages) != UCI_OK)) {
        nakd_log(L_CRIT, "Couldn't enumerate UCI packages");
        return NULL;
    }
    return packages;
}

/* Execute a callback for every option 'option_name' */
int nakd_uci_option_foreach(const char *option_name,
                      nakd_uci_option_foreach_cb cb,
                                    void *cb_priv) {
    char **uci_packages = nakd_list_packages();
    if (uci_packages == NULL)
        return -1;

    int cb_calls = 0;
    for (char **package = uci_packages; *package != NULL; package++) {
        int pkg_calls = nakd_uci_option_foreach_pkg(*package, option_name,
                                                             cb, cb_priv);
        if (pkg_calls < 0)
            return -1;
        cb_calls += pkg_calls;
    }
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
    
    nakd_log(L_DEBUG, "Loading UCI package %s", package);
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
    return uci_save(uci_ctx, pkg);
}

int nakd_uci_commit(struct uci_package **pkg, bool overwrite) {
    nakd_log(L_DEBUG, "Commiting changes to UCI package \"%s\"", (*pkg)->e.name);
    return uci_commit(uci_ctx, pkg, overwrite);
}

int nakd_unload_uci_package(struct uci_package *pkg) {
    nakd_log(L_DEBUG, "Unloading UCI package \"%s\"", pkg->e.name);
    return uci_unload(uci_ctx, pkg);
}
