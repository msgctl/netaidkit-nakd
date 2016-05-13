#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "nak_signal.h"
#include "timer.h"
#include "log.h"
#include "misc.h"
#include "nak_signal.h"
#include "module.h"

#define TIMER_SIGNAL SIGALRM
#define MAX_TIMERS 16

static struct nakd_timer _timers[MAX_TIMERS];
static pthread_mutex_t _timers_mutex;

static struct nakd_timer *__get_timer_slot(void) {
    struct nakd_timer *timer = _timers;

    for (; timer < ARRAY_END(_timers) && timer->active; timer++);
    if (timer >= ARRAY_END(_timers))
        return NULL;
    return timer;
}

static int _timer_handler(siginfo_t *siginfo) {
    if (siginfo->si_signo != TIMER_SIGNAL)
        return 1;

    pthread_mutex_lock(&_timers_mutex);
    struct nakd_timer *timer = siginfo->si_value.sival_ptr;
    /* in case the timer was removed, but a signal is still pending */
    if (timer->active) {
        timer->handler(siginfo, timer);
    }
    pthread_mutex_unlock(&_timers_mutex);
    return 0; /* handled */
}

struct nakd_timer *nakd_timer_add(int interval_ms, nakd_timer_handler handler,
                                                                 void *priv) {
    pthread_mutex_lock(&_timers_mutex);

    struct nakd_timer *timer = __get_timer_slot();
    if (timer == NULL)
        nakd_terminate("Out of timer slots");

    timer->handler = handler;
    timer->priv = priv;
    timer->active = 1;

    struct sigevent sev = {
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo = TIMER_SIGNAL,
        .sigev_value.sival_ptr = timer
    };

    if (timer_create(CLOCK_REALTIME, &sev, &timer->id) == -1)
        nakd_terminate("Couldn't create a timer. (%s)", strerror(errno));

    struct itimerspec its;
    memset(&its, 0, sizeof(struct itimerspec));

    its.it_value.tv_sec = interval_ms / (int)(1e3);
    its.it_value.tv_nsec = interval_ms % (int)(1e3) * (int)(1e6);
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timer->id, 0, &its, NULL) == -1)
        nakd_terminate("Couldn't set timer parameters. (%s)", strerror(errno));

unlock:
    pthread_mutex_unlock(&_timers_mutex);
    return timer;
}

void __nakd_timer_remove(struct nakd_timer *timer) {
    if (!timer->active) {
        nakd_log(L_WARNING, "Tried to remove nonexistent timer.");
        return;
    }

    timer_delete(timer->id);
    timer->active = 0;
}

void nakd_timer_remove(struct nakd_timer *timer) {
    pthread_mutex_lock(&_timers_mutex);
    __nakd_timer_remove(timer);
    pthread_mutex_unlock(&_timers_mutex);
}

static void _timer_remove_all(void) {
    pthread_mutex_lock(&_timers_mutex);
    for (struct nakd_timer *timer = _timers; timer < ARRAY_END(_timers);
                                                              timer++) {
        if (timer->active)
            __nakd_timer_remove(timer);
    }
    pthread_mutex_unlock(&_timers_mutex);
}

static int _timer_init(void) {
    pthread_mutex_init(&_timers_mutex, NULL);
    nakd_signal_add_handler(_timer_handler);
    return 0;
}

static int _timer_cleanup(void) {
    _timer_remove_all();
    pthread_mutex_destroy(&_timers_mutex);
    return 0;
}

static struct nakd_module module_timer = {
    .name = "timer",
    .deps = (const char *[]){ "signal", NULL },
    .init = _timer_init,
    .cleanup = _timer_cleanup
};

NAKD_DECLARE_MODULE(module_timer);
