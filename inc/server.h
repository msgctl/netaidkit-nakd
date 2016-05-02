#ifndef NAKD_SERVER_H
#define NAKD_SERVER_H

void nakd_accept_loop(void);
int nakd_active_connections(void);
void nakd_shutdown_connections(void);

#endif
