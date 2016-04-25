#include <signal.h>
#include <string.h>
#include "nak_signal.h"
#include "log.h"
#include "thread.h"

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

static void _sighandler(int signum) {
    switch (signum) {
        default: {
            nakd_log(L_INFO, "%s caught, terminating.", strsignal(signum));
            _shutdown = 1;
        }
    }
}

void nakd_sigwait_loop(void) {
    sigset_t set = _sigset(_sigwait);

    while (!_shutdown) {
        int signal;
        if (!sigwait(&set, &signal)) {
            _sighandler(signal);
        }
    }
}

int nakd_signal_init(void) {
    _set_default_sigmask();
    return 0;
}

int nakd_signal_cleanup(void) {
    /* noop */
}
