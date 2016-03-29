#ifndef LOG_H
#define LOG_H
#define DEFAULT_LOG_LEVEL L_DEBUG

enum {
    L_CRIT,
    L_WARNING,
    L_NOTICE,
    L_INFO,
    L_DEBUG
};

#define nakd_log(priority, format, args...) \
    _nakd_log((priority), (format), __func__, __FILE__, __LINE__, ##args)

void _nakd_log(int priority, const char *format, const char *func,
                                 const char *file, int line, ...);

#define nakd_terminate(format, args...) \
    { nakd_log(L_CRIT, (format), ##args); fflush(stdout); fflush(stderr); \
                                                                 exit(1); }

#define nakd_assert(stmt) _nakd_assert((stmt), #stmt, __PRETTY_FUNCTION__, __LINE__)

void _nakd_assert(int stmt, const char *stmt_str, const char *func, int line);

#define nakd_log_execution_point() \
    nakd_log(L_DEBUG, "")

void nakd_set_loglevel(int level);
void nakd_use_syslog(int use);
void nakd_log_init();
void nakd_log_close();

#endif
