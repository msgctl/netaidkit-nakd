#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <linux/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "server.h"
#include "nakd.h"
#include "log.h"

/* Create file containing pid as a string and obtain a write lock for it. */
int writePid(char *pid_path) {
    int fd;
    struct flock pid_lock;
    char pid_str[PID_STR_LEN + 1];

    if ((fd = open(pid_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) == -1)
        nakd_terminate("open()");

    pid_lock.l_type = F_WRLCK;
    pid_lock.l_whence = SEEK_SET;
    pid_lock.l_start = 0;
    pid_lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &pid_lock) == -1)
        return -1;

    if (ftruncate(fd, 0) == -1)
        nakd_terminate("ftruncate()");

    snprintf(pid_str, PID_STR_LEN + 1, "%ld\n", (long) getpid());
    if (write(fd, pid_str, strlen(pid_str)) != strlen(pid_str))
        nakd_terminate("write()");

    return fd;
}

int main(int argc, char *argv[]) {
    int pid_fd;

    nakd_log_init();
    nakd_use_syslog(0);

    /* Check if nakd is already running. */
    if ((pid_fd = writePid(PID_PATH)) == -1)
        nakd_terminate("writePid()");

    /* TODO: CHECK IF CURRENT USER IS ROOT AND IF NAKD USER EXISTS */

    nakd_server_init();
    nakd_accept_loop();
    nakd_server_cleanup();

    nakd_log_close();
    return 0;
}
