#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "thread.h"
#include "log.h"
#include "misc.h"

#define THREAD_STACK_SIZE 65536
#define MAX_THREADS 64

static int _unit_initialized;
static struct nakd_thread _threads[MAX_THREADS];

static pthread_key_t _tls_data;

static pthread_mutex_t _threads_mutex;

static pthread_mutex_t _shutdown_mutex;
static pthread_cond_t _shutdown_cv;

/* doubly-prefixed functions aren't thread-safe */
static struct nakd_thread *__get_thread_slot(void) {
    struct nakd_thread *thr = _threads;

    for (; thr < ARRAY_END(_threads) && thr->active; thr++);
    if (thr >= ARRAY_END(_threads))
        return NULL;
    return thr;
}

static int __active_threads(void) {
    int ret = 0;
    for (struct nakd_thread *thr = _threads;
                  thr < ARRAY_END(_threads);
                                    thr++) {
        ret += thr->active ? 1 : 0;
    }
    return ret;
}

int nakd_active_threads() {
    pthread_mutex_lock(&_threads_mutex);
    int n = __active_threads();
    pthread_mutex_unlock(&_threads_mutex);
    return n;
}

static void _cleanup_thread(void *priv) {
    struct nakd_thread *thr = (struct nakd_thread *)(priv);

    nakd_assert(_unit_initialized);

    nakd_log(L_DEBUG, "Cleaning up thread %d.", thr->tid);
    pthread_mutex_lock(&_threads_mutex);
    thr->active = 0;
    pthread_mutex_unlock(&_threads_mutex);

    /* see: _wait_for_completion() */
    pthread_mutex_lock(&_shutdown_mutex);
    pthread_cond_signal(&_shutdown_cv);
    pthread_mutex_unlock(&_shutdown_mutex);
}

static void _shutdown_sighandler(int signum) {
    nakd_assert(signum == NAKD_THREAD_SHUTDOWN_SIGNAL);

    struct nakd_thread *thr =
        (struct nakd_thread *)(pthread_getspecific(_tls_data)); 

    nakd_assert(thr->shutdown != NULL);
    thr->shutdown(thr);
}

static void _setup_shutdown_sighandler(void) {
    struct sigaction shutdown_action = {
        .sa_handler = _shutdown_sighandler,
        .sa_flags = 0
    };
    sigemptyset(&shutdown_action.sa_mask);

    if (sigaction(NAKD_THREAD_SHUTDOWN_SIGNAL, &shutdown_action, NULL))
        nakd_terminate("sigaction(): %s", strerror(errno));
}

static void *_thread_setup(void *priv) {
    sigset_t cleanup;
    sigemptyset(&cleanup);
    sigaddset(&cleanup, NAKD_THREAD_SHUTDOWN_SIGNAL);
    nakd_assert(!pthread_sigmask(SIG_UNBLOCK, &cleanup, NULL));

    struct nakd_thread *thr = (struct nakd_thread *)(priv);
    pthread_cleanup_push(_cleanup_thread, (void *)(thr));
    pthread_setspecific(_tls_data, (void *)(thr));

    thr->routine(thr);
    pthread_cleanup_pop(1);
    return NULL;
}

static int _thread_create(nakd_thread_routine start,
          nakd_thread_shutdown shutdown, void *priv,
                                pthread_attr_t attr,
                     struct nakd_thread **uthrptr) {
    pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);

    pthread_mutex_lock(&_threads_mutex);
    struct nakd_thread *thr = __get_thread_slot();
    if (thr == NULL)
        return 1;

    thr->routine = start;
    thr->shutdown = shutdown;
    thr->priv = priv;

    if (pthread_create(&thr->tid, &attr, _thread_setup, (void *)(thr)))
        return 1;

    thr->active = 1;

    if (uthrptr != NULL)
        *uthrptr = thr;

    pthread_mutex_unlock(&_threads_mutex);
    return 0;
}

int nakd_thread_create_detached(nakd_thread_routine start,
                 nakd_thread_shutdown cleanup, void *priv,
                           struct nakd_thread **uthrptr) {
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    return _thread_create(start, cleanup, priv, attr, uthrptr);
}

int nakd_thread_create_joinable(nakd_thread_routine start,
                 nakd_thread_shutdown cleanup, void *priv,
                           struct nakd_thread **uthrptr) {
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    return _thread_create(start, cleanup, priv, attr, uthrptr);
}

static int __thread_kill(struct nakd_thread *thr) {
    if (!thr->active) {
        nakd_log(L_NOTICE, "Tried to shutdown inactive thread.");
        return 1;
    }

    int s = pthread_kill(thr->tid, NAKD_THREAD_SHUTDOWN_SIGNAL);
    if (s < 0) {
        if (s == -ESRCH) {
            nakd_log(L_NOTICE, "Tried to shutdown nonexistent thread %d.", thr->tid);
            thr->active = 0;
            return 1;
        }
        nakd_terminate("pthread_kill(): %s", strerror(s));
    }

    pthread_attr_t attr;
    pthread_getattr_np(thr->tid, &attr);
    
    int detached;
    pthread_attr_getdetachstate(&attr, &detached);

    if (detached == PTHREAD_CREATE_JOINABLE) { 
        nakd_log(L_DEBUG, "Sent %s to joinable thread %d.",
          strsignal(NAKD_THREAD_SHUTDOWN_SIGNAL), thr->tid);
        nakd_log(L_DEBUG, "Waiting for thread %d to clean up.", thr->tid);
        s = pthread_join(thr->tid, NULL);
        if (s < 0)
            nakd_terminate(L_CRIT, "pthread_join(): %s", strerror(s));
    } else if (detached == PTHREAD_CREATE_DETACHED) {
        nakd_log(L_DEBUG, "Sent %s to detached thread %d.",
          strsignal(NAKD_THREAD_SHUTDOWN_SIGNAL), thr->tid);
    }

    return 0;
}

int nakd_thread_kill(struct nakd_thread *thr) {
    pthread_mutex_lock(&_threads_mutex);
    int s = __thread_kill(thr);
    pthread_mutex_unlock(&_threads_mutex);
    return s;
}

void nakd_thread_killall(void) {
    pthread_mutex_lock(&_threads_mutex);
    for (struct nakd_thread *thr = _threads;
                  thr < ARRAY_END(_threads);
                                    thr++) {
        if (thr->active)
            __thread_kill(thr);
    }
    pthread_mutex_unlock(&_threads_mutex);
}

int nakd_thread_init(void) {
    if (!_unit_initialized) {
        pthread_key_create(&_tls_data, NULL);
        pthread_mutex_init(&_threads_mutex, NULL);
        pthread_mutex_init(&_shutdown_mutex, NULL);
        pthread_cond_init(&_shutdown_cv, NULL);
        _unit_initialized = 1;
    }

    _setup_shutdown_sighandler();

    return 0;
}

static void _wait_for_completion(void) {
    int threads;

    pthread_mutex_lock(&_shutdown_mutex);
    while (threads = __active_threads()) {
        nakd_log(L_INFO, "Shutting down threads, %d remaining", threads);
        pthread_cond_wait(&_shutdown_cv, &_shutdown_mutex);
    }
    pthread_mutex_unlock(&_shutdown_mutex);
}

int nakd_thread_cleanup(void) {
    if (_unit_initialized) {
        nakd_thread_killall();
        _wait_for_completion();

        pthread_mutex_destroy(&_threads_mutex);
        pthread_cond_destroy(&_shutdown_cv);
        pthread_mutex_destroy(&_shutdown_mutex);
        _unit_initialized = 0;
    }
    return 0;
}
