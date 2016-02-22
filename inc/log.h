#ifndef LOG_H
#define LOG_H
#define DEFAULT_LOG_LEVEL L_DEBUG

#define nakd_log(priority, format, args...) \
    _nakd_log(priority, format, __func__, __FILE__, __LINE__, ##args)

enum {
    L_CRIT,
    L_WARNING,
    L_NOTICE,
    L_INFO,
    L_DEBUG
};

void nakd_set_loglevel(int level);
void nakd_use_syslog(int use);
void nakd_log_init();
void nakd_log_close();
void _nakd_log(int priority, const char *format, const char *func,
                                 const char *file, int line, ...);

#endif
