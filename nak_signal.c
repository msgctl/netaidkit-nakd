#include <signal.h>
#include <string.h>
#include "nak_signal.h"
#include "log.h"
#include "thread.h"

struct nakd_signal_handler {
    nakd_signal_handler impl;
    struct nakd_signal_handler *next;
} static *_handlers = NULL;

/* block by default in other threads */
static const int _sigmask[] = {
    SIGINT,
    SIGQUIT,
    SIGHUP,
    SIGTERM,
    SIGALRM,
    NAKD_THREAD_SHUTDOWN_SIGNAL,
    0
};

/* handle these here */
static const int _sigwait[] = {
    SIGINT,
    SIGQUIT,
    SIGHUP,
    SIGTERM,
    SIGALRM,
    0
};

static int _shutdown;

static sigset_t _sigset(const int *signals) {
    sigset_t set;

    sigemptyset(&set);
    for (const int *sig = signals; *sig; sig++)
        sigaddset(&set, *sig);

    return set;
}

static void _set_default_sigmask(void) {
    sigset_t set = _sigset(_sigmask);

    int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (s)
        nakd_terminate("Couldn't set default sigmask");
}

static struct nakd_signal_handler *_alloc_handler() {
    struct nakd_signal_handler *ret =
        malloc(sizeof(struct nakd_signal_handler)); 
    nakd_assert(ret != NULL);

    ret->impl = NULL;
    ret->next = NULL;
    return ret;
}

void nakd_signal_add_handler(nakd_signal_handler impl) {
    struct nakd_signal_handler *last_handler;

    if (_handlers == NULL) {
        last_handler = _handlers = _alloc_handler();
    } else {
        for (last_handler = _handlers; last_handler->next != NULL;
                               last_handler = last_handler->next);

        last_handler = last_handler->next = _alloc_handler();
    }

    last_handler->impl = impl;
} 

static void _free_handlers() {
    struct nakd_signal_handler *handler = _handlers;
    while (handler != NULL) {
        struct nakd_signal_handler *next = handler->next;
        free(handler);
        handler = next;
    }
}

static void _sighandler(siginfo_t *siginfo) {
    int handled = 0;
    for (struct nakd_signal_handler *handler = _handlers;
         handler->next != NULL; handler = handler->next) {
        if (!handler->impl(siginfo))
            handled = 1;
    }

    if (!handled) {
        nakd_log(L_INFO, "%s caught, terminating.",
                     strsignal(siginfo->si_signo));
        _shutdown = 1;
    }
}

void nakd_sigwait_loop(void) {
    sigset_t set = _sigset(_sigwait);

    while (!_shutdown) {
        siginfo_t siginfo;
        if (!sigwaitinfo(&set, &siginfo)) {
            _sighandler(&siginfo);
        }
    }
}

int nakd_signal_init(void) {
    _set_default_sigmask();
    return 0;
}

int nakd_signal_cleanup(void) {
    _free_handlers();
    return 0;
}
