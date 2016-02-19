#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#include "log.h"

#define CRIT    "CRITICAL"
#define WARNING "WARNING"
#define NOTICE  "NOTICE"
#define INFO    "INFO"
#define DEBUG   "DEBUG"

static int use_syslog = 1;
static int loglevel = DEFAULT_LOG_LEVEL;

static int *syslog_loglevel[] = {
    [L_CRIT] = LOG_CRIT,
    [L_WARNING] = LOG_WARNING,
    [L_NOTICE] = LOG_NOTICE,
    [L_INFO] = LOG_INFO,
    [L_DEBUG] = LOG_DEBUG
};

static const char *loglevel_string[] = {
    [L_CRIT] = CRIT,
    [L_WARNING] = WARNING,
    [L_NOTICE] = NOTICE,
    [L_INFO] = INFO,
    [L_DEBUG] = DEBUG
};

void nakd_set_loglevel(int level) {
    loglevel = level;
}

void nakd_use_syslog(int use) {
    use_syslog = use;
}

void nakd_log_init() {
    openlog("nakd", 0, LOG_DAEMON);
}

void nakd_log_close() {
    closelog();
}

void nakd_log(int priority, const char *format, ...) {
    va_list vl;

    if (priority > loglevel)
        return;

    va_start(vl, format);
    if (use_syslog) {
        vsyslog(syslog_loglevel[priority], format, vl);
    } else {
        fprintf(stderr, "[%s] ", loglevel_string[priority]);
        vfprintf(stderr, format, vl);
        fprintf(stderr, "\n");
    }

    va_end(vl);
}
