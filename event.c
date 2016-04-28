#include <pthread.h>
#include "event.h"
#include "thread.h"
#include "log.h"
#include "misc.h"

#define MAX_EVENTS 32
#define MAX_EVENT_HANDLERS 32

static pthread_mutex_t _event_mutex;
static struct nakd_thread *_event_thread;
static int _event_loop_shutdown;

static pthread_cond_t _event_thread_cv;

/* initialized to EVENT_UNSPECIFIED */
static enum nakd_event _events[MAX_EVENTS];

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
}

void nakd_event_remove_handler(struct event_handler *handler) {
    pthread_mutex_lock(&_event_mutex);
    handler->active = 0;
    pthread_mutex_unlock(&_event_mutex);
}

static void __handle_event(enum nakd_event event) {
    for (struct event_handler *handler = _event_handlers;
         handler < ARRAY_END(_event_handlers); handler++) {
        if (handler->active && handler->event == event) {
            nakd_log(L_INFO, "Handling event %s.", nakd_event_name[event]);
            handler->impl(event, handler->priv);
        }
    }
}

static void __handle_events(void) {
    for (enum nakd_event *ev = _events; ev < ARRAY_END(_events); ev++) {
        if (*ev != EVENT_UNSPECIFIED) {
            __handle_event(*ev);
            *ev = EVENT_UNSPECIFIED;
        }
    }
}

static void _event_loop(struct nakd_thread *thread) {
    pthread_mutex_lock(&_event_mutex);
    while (!_event_loop_shutdown) {
        pthread_cond_wait(&_event_thread_cv, &_event_mutex);
        __handle_events();
        pthread_mutex_unlock(&_event_mutex);
    }
}

static void _event_loop_shutdown_cb(struct nakd_thread *thread) {
    _event_loop_shutdown = 1;
}

static int _create_event_thread(void) {
    nakd_log(L_DEBUG, "Creating event thread.");
    if (nakd_thread_create_joinable(_event_loop,
                        _event_loop_shutdown_cb,
                        NULL, &_event_thread)) {
        nakd_log(L_CRIT, "Couldn't create an event thread.");
        return 1;
    }
    return 0;
}

void nakd_event_push(enum nakd_event event) {
    pthread_mutex_lock(&_event_mutex);
    /* only if there's already a handler for this type */
    for (struct event_handler *handler = _event_handlers;
         handler < ARRAY_END(_event_handlers); handler++) {
        if (handler->active && handler->event == event) {
            for (enum nakd_event *ev; ev < ARRAY_END(_events); ev++) {
                if (*ev != EVENT_UNSPECIFIED) {
                    *ev = event;
                    break;
                }
            }
            break;
        }
    }
    pthread_cond_signal(&_event_thread_cv);
    pthread_mutex_unlock(&_event_mutex);
}

void nakd_event_init(void) {
    pthread_mutex_init(&_event_mutex, NULL);
    pthread_cond_init(&_event_thread_cv, NULL);
    _create_event_thread();
}

void nakd_event_cleanup(void) {
    nakd_thread_kill(_event_thread);
    pthread_cond_destroy(&_event_thread_cv);
    pthread_mutex_destroy(&_event_mutex);
}

