#ifndef SERVER_H
#define SERVER_H

void nakd_server_init(void);
void nakd_server_cleanup(void);

void nakd_accept_loop(void);
int nakd_active_connections(void);
void nakd_shutdown_connections(void);

#endif
