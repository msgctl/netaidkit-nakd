#include <unistd.h>
#include <pthread.h>
#include <libubox/blobmsg_json.h>
#include <libubus.h>
#include "log.h"
#include "module.h"

#define UBUS_CALL_TIMEOUT 15 * 1000

static struct ubus_context *ubus_ctx = NULL;
static struct blob_buf ubus_buf;

static pthread_mutex_t _ubus_mutex;

static int _ubus_init(void) {
    pthread_mutex_init(&_ubus_mutex, NULL);

    /* defaults to UBUS_UNIX_SOCKET */
    ubus_ctx = ubus_connect(NULL);
    return ubus_ctx == NULL ? 1 : 0;
}

int nakd_ubus_call(const char *namespace, const char* procedure,
       const char *arg, ubus_data_handler_t cb, void *cb_priv) {
    int status;

    nakd_assert(namespace != NULL && procedure != NULL &&
                                arg != NULL && cb!= NULL);

    /* ubus isn't thread-safe */
    pthread_mutex_lock(&_ubus_mutex);
    /* subsequent inits free previous data */
    blob_buf_init(&ubus_buf, 0);

    if (arg != NULL) {
        if (!blobmsg_add_json_from_string(&ubus_buf, arg)) {
            nakd_log(L_CRIT, "ubus call parameter parse error.");
            status = 1;
            goto unlock;
        }
    }

    int namespace_id;
    status = ubus_lookup_id(ubus_ctx, namespace, &namespace_id);
    if (status)
        goto unlock;

    status = ubus_invoke(ubus_ctx, namespace_id, procedure, ubus_buf.head,
                                          cb, cb_priv, UBUS_CALL_TIMEOUT);
    if (status) {
        const char *errstr = ubus_strerror(status);
        nakd_log(L_WARNING, "ubus call status: %d - %s (%s %s %s)", status,
                                        errstr, namespace, procedure, arg);
    }
unlock:
    pthread_mutex_unlock(&_ubus_mutex);
    return status;
}

static int _ubus_cleanup(void) {
    pthread_mutex_destroy(&_ubus_mutex);

    if (ubus_buf.buf != NULL)
        blob_buf_free(&ubus_buf);
    if (ubus_ctx != NULL)
        ubus_free(ubus_ctx);
    return 0;
}

static struct nakd_module module_ubus = {
    .name = "ubus",
    .deps = NULL,
    .init = _ubus_init,
    .cleanup = _ubus_cleanup 
};

NAKD_DECLARE_MODULE(module_ubus);
