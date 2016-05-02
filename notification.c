#include <stddef.h>
#include "notification.h"
#include "led.h"
#include "event.h"
#include "log.h"
#include "module.h"

static struct led_condition _led_cable_plugged = {
    .name = "Ethernet plugged",
    .priority = LED_PRIORITY_NOTIFICATION,
    .states = (struct led_state[]){
        { "LED1_path", NULL, 1 },
        { "LED2_path", NULL, 0 },
        {}
    },
    .blink.on = 1,
    .blink.interval = 50,
    .blink.count = 4,
    .blink.state = 1
};

static struct led_condition _led_cable_removed = {
    .name = "Ethernet plugged",
    .priority = LED_PRIORITY_NOTIFICATION,
    .states = (struct led_state[]){
        { "LED1_path", NULL, 0 },
        { "LED2_path", NULL, 1 },
        {}
    },
    .blink.on = 1,
    .blink.interval = 50,
    .blink.count = 4,
    .blink.state = 1
};

static struct led_condition _led_traffic = {
    .name = "Network traffic",
    .priority = LED_PRIORITY_NOTIFICATION,
    .states = (struct led_state[]){
        { "LED1_path", NULL, 0 },
        { "LED2_path", NULL, 0 },
        {}
    },
    .blink.on = 1,
    .blink.interval = 50,
    .blink.count = 1,
    .blink.state = 1
};

static struct led_condition _connectivity_lost = {
    .name = "Network traffic",
    .priority = LED_PRIORITY_NOTIFICATION,
    .states = (struct led_state[]){
        { "LED1_path", NULL, 1 },
        { "LED2_path", NULL, 1 },
        {}
    },
    .blink.on = 1,
    .blink.interval = 50,
    .blink.count = 1,
    .blink.state = 1
};

struct led_event_notification {
    enum nakd_event event;
    struct led_condition *condition;
    int remove_condition;

    struct event_handler *event_handler;
} static _event_notifications[] = {
    { ETHERNET_WAN_PLUGGED, &_led_cable_plugged, 0 },
    { ETHERNET_LAN_PLUGGED, &_led_cable_plugged, 0 },
    { ETHERNET_WAN_LOST, &_led_cable_removed, 0 },
    { ETHERNET_LAN_LOST, &_led_cable_removed, 0 },
    { CONNECTIVITY_LOST, &_connectivity_lost, 0 },
    { CONNECTIVITY_OK, &_connectivity_lost, 1 },
    { NETWORK_TRAFFIC, &_led_traffic, 0 },
    {}
};

static void _event_handler(enum nakd_event event, void *priv) {
    for (struct led_event_notification *notification = _event_notifications;
                                      notification->event; notification++) {
        if (event == notification->event) {
            if (notification->remove_condition)
                nakd_led_condition_remove(notification->condition->name);
            else
                nakd_led_condition_add(notification->condition);
            break;
        }
    }
}

static int _notification_init(void) {
    for (struct led_event_notification *notification = _event_notifications;
                                      notification->event; notification++) {
        notification->event_handler = 
            nakd_event_add_handler(notification->event, _event_handler, NULL);
    }
    return 0;
}

static int _notification_cleanup(void) {
    for (struct led_event_notification *notification = _event_notifications;
                                      notification->event; notification++) {
        if (notification->event_handler != NULL)
            nakd_event_remove_handler(notification->event_handler);
    }
    return 0;
}

static struct nakd_module module_notification = {
    .name = "notification",
    .deps = (const char *[]){ "event", "led", NULL },
    .init = _notification_init,
    .cleanup = _notification_cleanup
};

NAKD_DECLARE_MODULE(module_notification);
