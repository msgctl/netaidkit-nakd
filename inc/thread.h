#ifndef NAKD_THREAD_H
#define NAKD_THREAD_H
#include <signal.h>

#define NAKD_THREAD_SHUTDOWN_SIGNAL SIGUSR1

struct nakd_thread;
typedef void (*nakd_thread_routine)(struct nakd_thread *);
typedef void (*nakd_thread_shutdown)(struct nakd_thread *);
struct nakd_thread {
    pthread_t tid;

    nakd_thread_routine routine;
    nakd_thread_shutdown shutdown;
    void *priv;

    int active;
};

int nakd_thread_create_detached(nakd_thread_routine start,
                nakd_thread_shutdown shutdown, void *priv,
                             struct nakd_thread **thread);
int nakd_thread_create_joinable(nakd_thread_routine start,
                nakd_thread_shutdown shutdown, void *priv,
                             struct nakd_thread **thread);
int nakd_thread_kill(struct nakd_thread *thr);
void nakd_thread_killall(void);
struct nakd_thread *nakd_thread_private(void);

#endif
