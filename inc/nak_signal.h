#ifndef NAKD_SIGNAL_H
#define NAKD_SIGNAL_H

typedef int (*nakd_signal_handler)(int signum);

int nakd_signal_init(void);
int nakd_signal_cleanup(void);

void nakd_add_handler(nakd_signal_handler handler);
void nakd_sigwait_loop(void);

#endif
