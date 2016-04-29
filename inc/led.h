#ifndef NAKD_LED_H
#define NAKD_LED_H
#include <sys/time.h>

struct led_state {
    char *led_config_key;
    char *_led_fs_path;
    int active;
};

struct led_blink {
    int on;
    int interval;
    int count;
    int state;
};

enum led_priority {
    LED_PRIORITY_DEFAULT,

    LED_PRIORITY_MODE,
    LED_PRIORITY_NOTIFICATION,
    LED_PRIORITY_ACTION_NEEDED
};

struct led_condition {
    char *name;
    enum led_priority priority;
    struct led_state *states;
    struct led_blink blink;

    int active;
};

void nakd_led_init(void);
void nakd_led_cleanup(void);

void nakd_led_condition_add(struct led_condition *cond);
void nakd_led_condition_remove(const char *name);

#endif
