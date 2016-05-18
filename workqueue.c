#include <stdlib.h>
#include <time.h>
#include <setjmp.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include "workqueue.h"
#include "thread.h"
#include "misc.h"
#include "log.h"
#include "module.h"
#include "timer.h"

#define WQ_CANCEL_SIGNAL SIGCONT

#define TIMEOUT_CHECK_INTERVAL 2500 /* ms */

/* provide a default, daemon-wide workqueue */
struct workqueue *nakd_wq;

struct worker_thread_priv {
    struct workqueue *wq;
    struct work *current;
};

static pthread_mutex_t _status_lock;
static struct nakd_timer *_timeout_timer;

static void __cancel_work(struct nakd_thread *thread) {
    nakd_assert(pthread_kill(thread->tid, WQ_CANCEL_SIGNAL) >= 0);
}

static void __check_timeout(void) {
    int now = time(NULL);

    /* TODO */
    struct workqueue *wq = nakd_wq;

    pthread_mutex_lock(&_status_lock);
    for (struct nakd_thread **thr = wq->threads;
            thr < wq->threads + wq->threadcount;
                                        thr++) {
        struct worker_thread_priv *priv = (*thr)->priv;
        if (priv->current != NULL && priv->current->desc.timeout) {
            int processing_time = now - priv->current->start_time;

            if (processing_time > priv->current->desc.timeout / 2) {
                nakd_log(L_WARNING, "workqueue: \"%s\" is taking too much"
                      " time: %ds", priv->current->desc.name, processing_time);
                if (processing_time > priv->current->desc.timeout) {
                    nakd_log(L_WARNING, "workqueue: canceling \"%s\".",
                                                  priv->current->desc.name);
                    __cancel_work(*thr);
                }
            }
        }
    }
    pthread_mutex_unlock(&_status_lock);
}

static void _timeout_sighandler(siginfo_t *timer_info,
                           struct nakd_timer *timer) {
    __check_timeout();
}

struct work *nakd_alloc_work(const struct work_desc *desc) {
    struct work *work = calloc(1, sizeof(struct work));
    nakd_assert(work != NULL);

    pthread_cond_init(&work->completed_cv, NULL);
    work->desc = *desc;
    return work;
}

static struct work *__add_work(struct workqueue *wq, struct work *new) {
    struct work **work = &wq->work;
    while (*work != NULL)
        work = &(*work)->next;

    *work = new;
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

void nakd_free_work(struct work *work) {
    pthread_cond_destroy(&work->completed_cv);
    free(work);
}

static void __free_queue(struct workqueue *wq) {
    struct work *work = wq->work;
    while (work != NULL) {
        struct work *next = work->next;
        nakd_free_work(work);
        work = next;
    }
}

static void _cancel_sighandler(int signum) {
    nakd_assert(signum == WQ_CANCEL_SIGNAL);

    struct nakd_thread *thread = nakd_thread_private();
    struct worker_thread_priv *priv = thread->priv;
    struct work *current = priv->current;
    if (current->desc.canceled != NULL)
        current->desc.canceled(current->desc.priv);
    siglongjmp(current->canceled_jmpbuf, 1);
}

static void _setup_cancel_sighandler(void) {
    struct sigaction cancel_action = {
        .sa_handler = _cancel_sighandler,
        .sa_flags = 0
    };
    sigemptyset(&cancel_action.sa_mask);

    if (sigaction(WQ_CANCEL_SIGNAL, &cancel_action, NULL))
        nakd_terminate("sigaction(): %s", strerror(errno));
}

static void _unblock_cancel_signal(void) {
    sigset_t cancel;
    sigemptyset(&cancel);
    sigaddset(&cancel, WQ_CANCEL_SIGNAL);
    nakd_assert(!pthread_sigmask(SIG_UNBLOCK, &cancel, NULL));
}

static void _workqueue_loop(struct nakd_thread *thr) {
    struct worker_thread_priv *priv = thr->priv;
    struct workqueue *wq = priv->wq;

    _unblock_cancel_signal();

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

        if (work->desc.name != NULL)
            nakd_log(L_DEBUG, "workqueue: processing \"%s\"", work->desc.name);

        pthread_mutex_lock(&_status_lock);
        work->start_time = time(NULL);
        priv->current = work;
        pthread_mutex_unlock(&_status_lock);

        if (!sigsetjmp(work->canceled_jmpbuf, 1)) {
            work->status = WORK_PROCESSING;
            work->desc.impl(work->desc.priv);
            work->status = WORK_DONE;
        } else {
            work->status = WORK_CANCELED;
        }

        time_t now = time(NULL);
        if (work->desc.name != NULL)
            nakd_log(L_DEBUG, "workqueue: finished \"%s\", took %ds",
                                 work->desc.name, now - work->start_time);

        pthread_cond_broadcast(&work->completed_cv);

        pthread_mutex_lock(&_status_lock);
        priv->current = NULL;
        pthread_mutex_unlock(&_status_lock);
        if (!work->desc.synchronous)
            nakd_free_work(work);
    }
    pthread_mutex_unlock(&wq->lock);
}

static void _workqueue_shutdown_cb(struct nakd_thread *thr) {
    struct workqueue *wq = thr->priv;
    wq->shutdown = 1;
}

void nakd_workqueue_create(struct workqueue **wq, int threadcount) {
    *wq = calloc(1, sizeof(struct workqueue));

    pthread_mutex_init(&(*wq)->lock, NULL);
    pthread_cond_init(&(*wq)->cv, NULL);

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
    struct work *new = __add_work(wq, work);
    new->status = WORK_QUEUED;
    pthread_cond_signal(&wq->cv);
    if (!work->desc.synchronous) {
        pthread_mutex_unlock(&wq->lock);
    } else {
        pthread_cond_wait(&work->completed_cv, &wq->lock);
        pthread_mutex_unlock(&wq->lock);
    }
}

int nakd_work_pending(struct workqueue *wq, const char *name) {
    int s = 0;
    pthread_mutex_lock(&wq->lock);

    /* check queue */
    struct work *work = wq->work;
    while (work != NULL) {
        if (!strcmp(name, work->desc.name)) {
            s = 1;
            goto unlock_wq;
        }
        work = work->next;
    }

    /* check already dequeued */
    pthread_mutex_lock(&_status_lock);
    for (struct nakd_thread **thr = wq->threads;
            thr < wq->threads + wq->threadcount;
                                        thr++) {
        struct worker_thread_priv *priv = (*thr)->priv;
        if (priv->current == NULL)
            continue;

        if (!strcmp(name, priv->current->desc.name)) {
            s = 1;
            goto unlock_workers;
        }
    }

unlock_workers:
    pthread_mutex_unlock(&_status_lock);
unlock_wq:
    pthread_mutex_unlock(&wq->lock);
    return s;
}

static int _workqueue_init(void) {
    pthread_mutex_init(&_status_lock, NULL);
    /* cancel wq entries on timeout */
    _setup_cancel_sighandler();
    nakd_workqueue_create(&nakd_wq, NAKD_DEFAULT_WQ_THREADS);
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
