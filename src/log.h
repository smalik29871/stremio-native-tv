#ifndef SNTV_LOG_H
#define SNTV_LOG_H
#include <stdarg.h>
#include <stddef.h>

/* On webOS, SAM points stdout/stderr at /dev/null, so logs go to a file under
 * the app's writable dir (app.log, previous run in app.prev.log). The last
 * LOG_RING_LINES lines are also kept in memory for the on-screen Diagnostics
 * screen. On desktop everything mirrors to stderr too. */
void log_init(const char *dir);
void log_msg(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_shutdown(void);

#define LOGI(...) log_msg("I", __VA_ARGS__)
#define LOGW(...) log_msg("W", __VA_ARGS__)
#define LOGE(...) log_msg("E", __VA_ARGS__)

enum { LOG_RING_LINES = 600, LOG_LINE_MAX = 320 };

typedef struct LogLine {
    char level;                 /* 'I', 'W' or 'E' */
    char text[LOG_LINE_MAX];    /* "HH:MM:SS message" */
} LogLine;

/* Copies up to `max` most recent lines, oldest first. Returns the count. */
int  log_snapshot(LogLine *out, int max);
/* Bumps whenever a line is added (lets a screen redraw only on change). */
unsigned log_version(void);
/* Path of the log file of the previous run ("" if none). */
const char *log_previous_path(void);
/* The log file's descriptor (-1 if none): lets a crash handler write its last line. */
int log_fd(void);

/* One-line description of the device: webOS release (or uname), user id. */
void log_system_info(char *out, size_t n);

#endif
