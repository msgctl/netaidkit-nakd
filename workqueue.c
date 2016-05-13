#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "workqueue.h"
#include "thread.h"
#include "misc.h"
#include "log.h"
#include "module.h"
#include "timer.h"

#define TIMEOUT_CHECK_INTERVAL 2500 /* ms */

/* provide a default, daemon-wide workqueue */
struct workqueue *nakd_wq;

struct worker_thread_priv {
    struct workqueue *wq;
    struct work *current;
};

static pthread_mutex_t _status_lock;
struct nakd_timer *_timeout_timer;

static void __check_timeout(void) {
    int now = time(NULL);

    /* TODO */
    struct workqueue *wq = nakd_wq;

    pthread_mutex_lock(&_status_lock);
    for (struct nakd_thread **thr = wq->threads;
            thr < wq->threads + wq->threadcount;
                                        thr++) {
        struct worker_thread_priv *priv = (*thr)->priv;
        if (priv->current != NULL) {
            int processing_time = now - priv->current->start_time;

            if (processing_time > priv->wq->timeout) {
                nakd_log(L_WARNING, "workqueue: \"%s\" is taking too much time: %ds",
                                               priv->current->name, processing_time);
            }
        }
    }
    pthread_mutex_unlock(&_status_lock);
}

static void _timeout_sighandler(siginfo_t *timer_info,
                           struct nakd_timer *timer) {
    __check_timeout();
}

static struct work *__add_work(struct workqueue *wq) {
    struct work **work = &wq->work;
    while (*work != NULL)
        work = &(*work)->next;

    *work = calloc(1, sizeof(struct work));
    return *work;
}

static struct work *__dequeue(struct workqueue *wq) {
    if (wq->work == NULL)
        return NULL;

    struct work *cur = wq->work;
    struct work *next = cur->next;
    wq->work = next;
    return cur;
}

static void __free_work(struct work *work) {
    free(work);
}

static void __free_queue(struct workqueue *wq) {
    struct work *work = wq->work;
    while (work != NULL) {
        struct work *next = work->next;
        __free_work(work);
        work = next;
    }
}

static void _workqueue_loop(struct nakd_thread *thr) {
    struct worker_thread_priv *priv = thr->priv;
    struct workqueue *wq = priv->wq;

    for (;;) {
        pthread_mutex_lock(&wq->lock);
        if (wq->shutdown)
            break;

        if (wq->work == NULL) {
            pthread_cond_wait(&wq->cv, &wq->lock);
            if (wq->shutdown)
                break;
        }

        struct work *work = __dequeue(wq);
        pthread_mutex_unlock(&wq->lock);

        if (work == NULL)
            continue;

        if (work->name != NULL)
            nakd_log(L_DEBUG, "workqueue: processing \"%s\"", work->name);

        pthread_mutex_lock(&_status_lock);
        work->start_time = time(NULL);
        priv->current = work;
        pthread_mutex_unlock(&_status_lock);

        work->impl(work->priv);

        if (work->name != NULL)
            nakd_log(L_DEBUG, "workqueue: finished \"%s\"", work->name);

        pthread_mutex_lock(&_status_lock);
        priv->current = NULL;
        pthread_mutex_unlock(&_status_lock);
        __free_work(work);
    }
    pthread_mutex_unlock(&wq->lock);
}

static void _workqueue_shutdown_cb(struct nakd_thread *thr) {
    struct workqueue *wq = thr->priv;
    wq->shutdown = 1;
}

void nakd_workqueue_create(struct workqueue **wq, int threadcount, int timeout) {
    *wq = calloc(1, sizeof(struct workqueue));

    pthread_mutex_init(&(*wq)->lock, NULL);
    pthread_cond_init(&(*wq)->cv, NULL);

    (*wq)->timeout = timeout;

    (*wq)->threadcount = threadcount;
    (*wq)->threads = calloc(threadcount, sizeof(struct nakd_thread *));

    for (struct nakd_thread **thr = (*wq)->threads;
                thr < (*wq)->threads + threadcount;
                                           thr++) {
        /* TODO implement custom cleanup in thread.c */
        struct worker_thread_priv *priv = calloc(1,
                sizeof(struct worker_thread_priv));
        nakd_assert(priv != NULL);
        priv->wq = *wq;
        priv->current = NULL;

        nakd_assert(!nakd_thread_create_joinable(_workqueue_loop,
                             _workqueue_shutdown_cb, priv, thr));
    }
}

void nakd_workqueue_destroy(struct workqueue **wq) {
    for (struct nakd_thread **thr = (*wq)->threads;
         thr < (*wq)->threads + (*wq)->threadcount;
                                           thr++) {
        nakd_thread_kill(*thr);
    }
    free((*wq)->threads);

    __free_queue(*wq); 

    pthread_mutex_destroy(&(*wq)->lock);
    pthread_cond_destroy(&(*wq)->cv);
    free(*wq), *wq = NULL;
}

void nakd_workqueue_add(struct workqueue *wq, struct work *work) {
    pthread_mutex_lock(&wq->lock);
    struct work *new = __add_work(wq);
    *new = *work;
    pthread_cond_signal(&wq->cv);
    pthread_mutex_unlock(&wq->lock);
}

struct work *nakd_workqueue_lookup(struct workqueue *wq, const char *name) {
    pthread_mutex_lock(&wq->lock);
    struct work *work = wq->work;
    while (work != NULL) {
        if (!strcmp(name, work->name))
            goto unlock;
        work = work->next;
    }

    work = NULL;

unlock:
    pthread_mutex_unlock(&wq->lock);
    return work;
}

static int _workqueue_init(void) {
    pthread_mutex_init(&_status_lock, NULL);
    nakd_workqueue_create(&nakd_wq, NAKD_DEFAULT_WQ_THREADS,
                                      NAKD_DEFAULT_TIMEOUT);
    _timeout_timer = nakd_timer_add(TIMEOUT_CHECK_INTERVAL,
                                _timeout_sighandler, NULL);
    return 0;
}

static int _workqueue_cleanup(void) {
    nakd_timer_remove(_timeout_timer);
    nakd_workqueue_destroy(&nakd_wq);
    pthread_mutex_destroy(&_status_lock);
    return 0;
}

static struct nakd_module module_workqueue = {
    .name = "workqueue",
    .deps = (const char *[]){ "thread", "timer", NULL },
    .init = _workqueue_init,
    .cleanup = _workqueue_cleanup
};

NAKD_DECLARE_MODULE(module_workqueue);
