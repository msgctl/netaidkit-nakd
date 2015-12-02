#ifndef CONNECTION_H
#define CONNECTION_H

/* Max size of command sent over domain socket. */
#define MAX_MSG_LEN     4096

int nakd_handle_connection(int sock);

#endif
