#ifndef NAKD_WORKQUEUE_H
#define NAKD_WORKQUEUE_H
#include <pthread.h>
#include <time.h>
#include "thread.h"

#define NAKD_DEFAULT_WQ_THREADS 8
#define NAKD_DEFAULT_TIMEOUT 5 /* seconds */

typedef void (*nakd_work_func)(void *priv);

struct work {
    nakd_work_func impl;
    void *priv;   

    /* debug */
    const char *desc;
    time_t start_time;

    struct work *next;
};

struct workqueue {
    pthread_mutex_t lock; 
    struct work *work;
    int size;

    struct nakd_thread **threads;
    int threadcount;
    pthread_cond_t cv;
    int shutdown;

    /* seconds */
    int timeout;
};

void nakd_workqueue_create(struct workqueue **wq, int threads, int timeout);
void nakd_workqueue_destroy(struct workqueue **wq);
void nakd_workqueue_add(struct workqueue *wq, struct work *work);

extern struct workqueue *nakd_wq;

#endif
