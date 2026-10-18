#ifndef SNTV_LOG_H
#define SNTV_LOG_H
#include <stdarg.h>

/* On webOS, SAM points stdout/stderr at /dev/null, so logs go to a file under
 * the app's writable dir. On desktop they also mirror to stderr. */
void log_init(const char *dir);
void log_msg(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_shutdown(void);

#define LOGI(...) log_msg("I", __VA_ARGS__)
#define LOGW(...) log_msg("W", __VA_ARGS__)
#define LOGE(...) log_msg("E", __VA_ARGS__)

#endif
