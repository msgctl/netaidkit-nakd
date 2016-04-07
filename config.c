#include <uci.h>
#include "config.h"
#include "log.h"

static struct uci_context *uci_ctx;

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
