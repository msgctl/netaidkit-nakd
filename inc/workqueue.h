#ifndef NAKD_WORKQUEUE_H
#define NAKD_WORKQUEUE_H
#include <pthread.h>
#include <time.h>
#include <setjmp.h>
#include "thread.h"

#define NAKD_DEFAULT_WQ_THREADS 8

typedef void (*nakd_work_func)(void *priv);

enum work_status {
    WORK_QUEUED,
    WORK_PROCESSING,
    WORK_DONE,
    WORK_CANCELED
};

struct work_desc {
    nakd_work_func impl;
    /* Called when entry has timed out or was canceled. */
    nakd_work_func canceled;
    void *priv;   
    const char *name;
    /*
     * Effect:
     * Allocated struct work won't be freed and nakd_workqueue_add will return
     * only after either impl() returns or canceled() is called due to timeout.
     */
    int synchronous;
    int timeout; /* seconds */
};

struct work {
    struct work_desc desc;

    time_t start_time;
    sigjmp_buf canceled_jmpbuf;
    pthread_cond_t completed_cv;
    enum work_status status;

    struct work *next;
};

struct workqueue {
    pthread_mutex_t lock; 
    struct work *work;

    struct nakd_thread **threads;
    int threadcount;
    pthread_cond_t cv;
    int shutdown;
};

void nakd_workqueue_create(struct workqueue **wq, int threads);
void nakd_workqueue_destroy(struct workqueue **wq);
struct work *nakd_alloc_work(const struct work_desc *desc);
void nakd_free_work(struct work *work);
void nakd_workqueue_add(struct workqueue *wq, struct work *work);
int nakd_work_pending(struct workqueue *wq, const char *name);

extern struct workqueue *nakd_wq;

#endif
