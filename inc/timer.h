#ifndef NAKD_TIMER_H
#define NAKD_TIMER_H

struct nakd_timer;
typedef void (*nakd_timer_handler)(siginfo_t *timer_info,
                               struct nakd_timer *timer);
struct nakd_timer {
    timer_t id;
    nakd_timer_handler handler;
    void *priv;

    int active;
};

void nakd_timer_init(void);
void nakd_timer_cleanup(void);

struct nakd_timer *nakd_timer_add(int interval_ms, nakd_timer_handler handler,
                                                                  void *priv);
void __nakd_timer_remove(struct nakd_timer *timer);
void nakd_timer_remove(struct nakd_timer *timer);

#endif
