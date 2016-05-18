#include <pthread.h>
#include "event.h"
#include "thread.h"
#include "log.h"
#include "misc.h"
#include "module.h"
#include "workqueue.h"

#define MAX_EVENT_HANDLERS 64

static pthread_mutex_t _event_mutex;
static struct event_handler _event_handlers[MAX_EVENT_HANDLERS];

#define EVENT_NAME_ENTRY(event) [event] = #event 
const char *nakd_event_name[] = {
    EVENT_NAME_ENTRY(EVENT_UNSPECIFIED),
    EVENT_NAME_ENTRY(ETHERNET_WAN_PLUGGED),
    EVENT_NAME_ENTRY(ETHERNET_WAN_LOST),

    EVENT_NAME_ENTRY(ETHERNET_LAN_PLUGGED),
    EVENT_NAME_ENTRY(ETHERNET_LAN_LOST),

    EVENT_NAME_ENTRY(WIRELESS_NETWORK_AVAILABLE),
    EVENT_NAME_ENTRY(WIRELESS_NETWORK_LOST),

    EVENT_NAME_ENTRY(CONNECTIVITY_LOST),
    EVENT_NAME_ENTRY(CONNECTIVITY_OK),

    EVENT_NAME_ENTRY(NETWORK_TRAFFIC)
};

static struct event_handler *__get_event_handler_slot(void) {
    struct event_handler *handler = _event_handlers;

    for (; handler < ARRAY_END(_event_handlers) && handler->active; handler++);
    if (handler >= ARRAY_END(_event_handlers))
        return NULL;
    return handler;
}

struct event_handler *nakd_event_add_handler(enum nakd_event event,
                              nakd_event_handler hnd, void *priv) {
    pthread_mutex_lock(&_event_mutex);
    struct event_handler *handler = __get_event_handler_slot();
    if (handler == NULL)
        nakd_terminate("Out of event handler slots.");

    handler->event = event;
    handler->impl = hnd;
    handler->priv = priv;
    handler->active = 1;
    pthread_mutex_unlock(&_event_mutex);

    nakd_log(L_DEBUG, "Added event handler for %s.", nakd_event_name[event]);
    return handler;
}

void nakd_event_remove_handler(struct event_handler *handler) {
    pthread_mutex_lock(&_event_mutex);
    handler->active = 0;
    pthread_mutex_unlock(&_event_mutex);
}

static void _call_handler(void *priv) {
    struct event_handler *handler = priv;
    handler->impl(handler->event, handler->priv);
}

void nakd_event_push(enum nakd_event event) {
    pthread_mutex_lock(&_event_mutex);
    for (struct event_handler *handler = _event_handlers;
         handler < ARRAY_END(_event_handlers); handler++) {
        if (handler->active && handler->event == event) {
            nakd_log(L_INFO, "Handling event %s.", nakd_event_name[event]);

            struct work_desc _event_desc = {
                .impl = _call_handler,
                .name = nakd_event_name[handler->event],
                .priv = handler
            };
            struct work *work = nakd_alloc_work(&_event_desc);
            nakd_workqueue_add(nakd_wq, work);
        }
    }
    pthread_mutex_unlock(&_event_mutex);
}
    
static int _event_init(void) {
    pthread_mutex_init(&_event_mutex, NULL);
}

static int _event_cleanup(void) {
    pthread_mutex_destroy(&_event_mutex);
}

static struct nakd_module module_event = {
    .name = "event",
    .deps = (const char *[]){ "thread", "workqueue", NULL },
    .init = _event_init,
    .cleanup = _event_cleanup
};

NAKD_DECLARE_MODULE(module_event);
