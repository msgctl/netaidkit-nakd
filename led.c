#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "led.h"
#include "log.h"
#include "misc.h"
#include "timer.h"
#include "config.h"
#include "module.h"

#define MAX_CONDITIONS 16
#define UPDATE_INTERVAL 33 /* ms */

static pthread_mutex_t _led_mutex;
static struct led_condition _led_conditions[MAX_CONDITIONS];
static struct led_state *_last_states = NULL;
static struct led_condition *_current_condition = NULL;
static struct nakd_timer *_led_timer;

static timer_t _blink_timer;

static struct led_condition *__get_condition_slot(void) {
    struct led_condition *cond = _led_conditions;

    for (; cond < ARRAY_END(_led_conditions) && cond->active; cond++);
    if (cond >= ARRAY_END(_led_conditions))
        return NULL;
    return cond;
}

static int _led_condition_active(const char *name) {
    for (struct led_condition *cond = _led_conditions;
          cond < ARRAY_END(_led_conditions); cond++) {
        if (!cond->active)
            continue;
        if (!strcmp(name, cond->name))
            return 1;
    }
    return 0;
}

void nakd_led_condition_add(struct led_condition *cond) {
    pthread_mutex_lock(&_led_mutex);
    if (_led_condition_active(cond->name))
        goto unlock;

    struct led_condition *_cond = __get_condition_slot();
    if (_cond == NULL) {
        nakd_log(L_CRIT, "Out of LED condition slots.");
        goto unlock;
    }
    *_cond = *cond;
    _cond->active = 1;

unlock:
    pthread_mutex_unlock(&_led_mutex);
}

static void _led_condition_remove(struct led_condition *cond) {
    nakd_log(L_DEBUG, "Removing LED condition: %s", cond->name);
    cond->active = 0;
}

void nakd_led_condition_remove(const char *name) {
    pthread_mutex_lock(&_led_mutex);
    for (struct led_condition *cond = _led_conditions;
          cond < ARRAY_END(_led_conditions); cond++) {
        if (!cond->active)
            continue;
        if (!strcmp(name, cond->name))
            _led_condition_remove(cond);
    }
    pthread_mutex_unlock(&_led_mutex);
}

static struct led_condition *__choose_condition(void) {
    struct led_condition *cond = NULL;

    for (struct led_condition *iter = _led_conditions;
          iter < ARRAY_END(_led_conditions); iter++) {
        if (!iter->active || iter == _current_condition)
            continue;

        if (cond == NULL || iter->priority > cond->priority)
            cond = iter; 
    }
    return cond;
}

static void __set_state(struct led_state *state, int active) {
    if (!state->_led_fs_path) {
        if (nakd_config_key(state->led_config_key, &state->_led_fs_path)) {
            nakd_log(L_WARNING, "Couldn't retrieve LED path from nakd "
                                                     "configuration.");
            return;
        }
    }

    if (access(state->_led_fs_path, W_OK)) {
        nakd_log(L_WARNING, "Couldn't access LED chardev at %s",
                                           state->_led_fs_path);
        return;
    }

    FILE *led_fp = fopen(state->_led_fs_path, "w");
    if (led_fp == NULL) {
        nakd_log(L_WARNING, "Couldn't open chardev at %s for writing",
                                                 state->_led_fs_path);
        return;
    }

    fputs(state->active && active ? "1\n" : "0\n", led_fp);
    fclose(led_fp);
}

static void __set_states(struct led_state *states, int active) {
    _last_states = states;
    for (; states->led_config_key; states++)
        __set_state(states, active);
}

static void __update_condition(void) {
    if (_current_condition == NULL)
        return;

    if (_current_condition->blink.on) {
        if (!_current_condition->blink.count) {
            _led_condition_remove(_current_condition);
            return;
        }

        struct itimerspec timer_state;
        timer_gettime(_blink_timer, &timer_state);
        if (!timer_state.it_value.tv_nsec) {
            __set_states(_current_condition->states == NULL ?
                   _last_states : _current_condition->states,
                            _current_condition->blink.state);
            _current_condition->blink.state = !_current_condition->blink.state;
            if (_current_condition->blink.count > 0)
               _current_condition->blink.count--;

            timer_state.it_value.tv_sec =
                _current_condition->blink.interval * (int)(1e6) / (int)(1e9);
            timer_state.it_value.tv_nsec =
                (_current_condition->blink.interval * (int)(1e6)) % (int)(1e9);
            timer_settime(_blink_timer, 0, &timer_state, NULL);
        }
    } else {
        __set_states(_current_condition->states, 1);
    }
}

static void __swap_condition(struct led_condition *next) {
    nakd_log(L_DEBUG, "Next LED condition: %s", next->name);
    _current_condition = next;
}

static void _led_sighandler(siginfo_t *timer_info, struct nakd_timer *timer) {
    pthread_mutex_lock(&_led_mutex);
    struct led_condition *next = __choose_condition();
    if (next != NULL) {
        if (_current_condition == NULL || !_current_condition->active ||
                        next->priority > _current_condition->priority) {
            __swap_condition(next);
        }
    }

    if (_current_condition->active)
        __update_condition();
    pthread_mutex_unlock(&_led_mutex);
}

static struct led_condition _default = {
    .name = "default",
    .priority = LED_PRIORITY_DEFAULT,
    .states = (struct led_state[]){
        { "LED1_path", NULL, 1 },
        { "LED2_path", NULL, 1 },
        {}
    },
    .blink.on = 1,
    .blink.interval = 100,
    .blink.count = -1, /*infinite */
};

static int _led_init(void) {
    pthread_mutex_init(&_led_mutex, NULL);
    _led_timer = nakd_timer_add(UPDATE_INTERVAL, _led_sighandler, NULL);
    nakd_assert(_led_timer != NULL);

    struct sigevent sev = {
        .sigev_notify = SIGEV_NONE
    };
    if (timer_create(CLOCK_REALTIME, &sev, &_blink_timer) == -1)
        nakd_terminate("Couldn't create a timer. (%s)", strerror(errno));

    nakd_led_condition_add(&_default);
}

static int _led_cleanup(void) {
    timer_delete(_blink_timer);
    nakd_timer_remove(_led_timer);
    pthread_mutex_destroy(&_led_mutex);
}

static struct nakd_module module_led = {
    .name = "led",
    .deps = (const char *[]){ "config", "thread", "timer", NULL },
    .init = _led_init,
    .cleanup = _led_cleanup
};

NAKD_DECLARE_MODULE(module_led);
