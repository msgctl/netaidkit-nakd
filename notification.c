#include <stddef.h>
#include "notification.h"
#include "led.h"
#include "event.h"
#include "log.h"

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
    .blink.count = 4
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
    .blink.count = 4
};

static void _event_handler(enum nakd_event event, void *priv) {
    struct led_condition *notification;
    if (event == ETHERNET_WAN_PLUGGED ||
        event == ETHERNET_LAN_PLUGGED) {
        notification = &_led_cable_plugged;
    } else if (event == ETHERNET_WAN_LOST ||
               event == ETHERNET_LAN_LOST) {
        notification = &_led_cable_removed;
    }

    nakd_led_condition_add(notification);
    nakd_log(L_DEBUG, "Pushing LED notification: %s",
                                 notification->name);
}

int nakd_notification_init(void) {
    nakd_event_add_handler(ETHERNET_WAN_PLUGGED, _event_handler, NULL);
    nakd_event_add_handler(ETHERNET_WAN_LOST, _event_handler, NULL);
    
    nakd_event_add_handler(ETHERNET_LAN_PLUGGED, _event_handler, NULL);
    nakd_event_add_handler(ETHERNET_LAN_LOST, _event_handler, NULL);
}

int nakd_notification_cleanup(void) {
    /* noop */
}
