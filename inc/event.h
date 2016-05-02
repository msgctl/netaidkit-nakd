#ifndef NAKD_EVENT_H
#define NAKD_EVENT_H

enum nakd_event {
    EVENT_UNSPECIFIED,

    ETHERNET_WAN_PLUGGED,
    ETHERNET_WAN_LOST,

    ETHERNET_LAN_PLUGGED,
    ETHERNET_LAN_LOST,

    WIRELESS_NETWORK_AVAILABLE,
    WIRELESS_NETWORK_LOST,

    CONNECTIVITY_LOST,
    CONNECTIVITY_OK,

    NETWORK_TRAFFIC
};

extern const char *nakd_event_name[];

typedef void (*nakd_event_handler)(enum nakd_event event, void *priv);

struct event_handler {
    enum nakd_event event;
    nakd_event_handler impl;
    void *priv;

    int active;
};

void nakd_event_push(enum nakd_event event);

struct event_handler *nakd_event_add_handler(enum nakd_event event,
                               nakd_event_handler hnd, void *priv);
void nakd_event_remove_handler(struct event_handler *handler);

#endif
