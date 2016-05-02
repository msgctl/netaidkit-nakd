#ifndef NAKD_UBUS_H
#define NAKD_UBUS_H

/* -std=c99 */
#define typeof __typeof
#include <libubox/blobmsg_json.h>
#include <libubus.h>
#undef typeof

int nakd_ubus_call(const char *namespace, const char* procedure,
       const char *arg, ubus_data_handler_t cb, void *cb_priv);

#endif
