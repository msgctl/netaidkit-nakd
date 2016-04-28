#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <linux/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "server.h"
#include "log.h"
#include "ubus.h"
#include "thread.h"
#include "nak_signal.h"
#include "config.h"
#include "timer.h"
#include "led.h"
#include "event.h"
#include "netintf.h"
#include "nak_uci.h"

#define PID_PATH "/run/nakd/nakd.pid"

/* Create file containing pid as a string and obtain a write lock for it. */
static int _write_pid(char *pid_path) {
    int fd;
    struct flock pid_lock;
    char pid_str[64];

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

    snprintf(pid_str, sizeof pid_str, "%ld\n", (long) getpid());
    if (write(fd, pid_str, strlen(pid_str)) != strlen(pid_str))
        nakd_terminate("write()");

    return fd;
}

int main(int argc, char *argv[]) {
    int pid_fd;

    nakd_log_init();
    nakd_use_syslog(0);

    /* Check if nakd is already running. */
    if ((pid_fd = _write_pid(PID_PATH)) == -1)
        nakd_terminate("writePid()");

    /* TODO: CHECK IF CURRENT USER IS ROOT AND IF NAKD USER EXISTS */

    nakd_signal_init();
    nakd_uci_init();
    nakd_config_init();
    nakd_thread_init();
    nakd_event_init();
    nakd_timer_init();
    nakd_led_init();
    nakd_ubus_init();
    nakd_netintf_init();
    nakd_server_init();

    nakd_sigwait_loop();

    nakd_server_cleanup();
    nakd_netintf_cleanup();
    nakd_ubus_free();
    nakd_led_cleanup();
    nakd_timer_cleanup();
    nakd_event_cleanup();
    nakd_thread_cleanup();
    nakd_config_cleanup();
    nakd_uci_cleanup();
    nakd_signal_cleanup();

    nakd_log_close();
    return 0;
}
