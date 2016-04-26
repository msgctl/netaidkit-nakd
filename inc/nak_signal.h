#ifndef NAKD_SIGNAL_H
#define NAKD_SIGNAL_H
#include <signal.h>

typedef int (*nakd_signal_handler)(siginfo_t *info);

int nakd_signal_init(void);
int nakd_signal_cleanup(void);

void nakd_signal_add_handler(nakd_signal_handler handler);
void nakd_sigwait_loop(void);

#endif
