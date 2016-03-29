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

static int syslog_loglevel[] = {
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

void _nakd_log(int priority, const char *format, const char *func,
                                const char *file, int line, ...) {
    va_list vl;
    char _fmt[256];

    if (priority > loglevel)
        return;

    va_start(vl, format);
    if (use_syslog) {
        snprintf(_fmt, sizeof(_fmt), "[%s:%d, %s] %s", file, line, func,
                                                                format);

        vsyslog(syslog_loglevel[priority], _fmt, vl);
    } else {
        snprintf(_fmt, sizeof(_fmt), "[%s] [%s:%d, %s] %s\n",
            loglevel_string[priority], file, line, func, format);

        vfprintf(stderr, _fmt, vl);
    }

    va_end(vl);
}

void _nakd_assert(int stmt, const char *stmt_str, const char *func,
                                                        int line) {
    if (stmt)
        return;

    nakd_terminate("nakd: assertion (%s) failed in %s:%d\n", stmt_str, func,
                                                                      line);
}
