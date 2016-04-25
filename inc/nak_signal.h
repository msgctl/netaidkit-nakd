#ifndef NAKD_SIGNAL_H
#define NAKD_SIGNAL_H

int nakd_signal_init(void);
int nakd_signal_cleanup(void);

void nakd_sigwait_loop(void);

#endif
