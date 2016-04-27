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

struct led_condition {
    char *name;
    int priority;
    struct led_state *states;
    struct led_blink blink;

    int active;
};

void nakd_led_init(void);
void nakd_led_cleanup(void);

void nakd_led_condition_add(struct led_condition *cond);
void nakd_led_condition_remove(const char *name);

#endif
