/* Forced into every file of the Windows build (CMakeLists.txt): the few POSIX calls the app
 * makes that MinGW spells otherwise. Sockets and loading libraries are in compat.h. */
#ifndef SNTV_WIN_POSIX_H
#define SNTV_WIN_POSIX_H
#define _CRT_RAND_S   /* rand_s, before stdlib.h */
#include <sys/stat.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define mkdir(path, mode) _mkdir(path)
static inline int setenv(const char *k, const char *v, int overwrite) { if (!overwrite && getenv(k)) return 0; return _putenv_s(k, v); }
static inline int unsetenv(const char *k) { return _putenv_s(k, ""); }
#include <time.h>
static inline struct tm *localtime_r(const time_t *t, struct tm *out) { return localtime_s(out, t) ? NULL : out; }
static inline struct tm *gmtime_r(const time_t *t, struct tm *out) { return gmtime_s(out, t) ? NULL : out; }
#define timegm _mkgmtime
static inline char *strndup(const char *s, size_t n) { size_t l = strnlen(s, n); char *d = (char *)malloc(l + 1); if (d) { memcpy(d, s, l); d[l] = 0; } return d; }
#define fsync _commit
#define lstat stat
static inline unsigned getuid(void) { return 1000; }   /* no root on Windows */
/* a drive's free space (win_posix.c) */
struct statvfs { unsigned long long f_bsize, f_frsize, f_blocks, f_bfree, f_bavail; };
int sntv_statvfs(const char *path, struct statvfs *out);
#define statvfs(path, out) sntv_statvfs(path, out)
/* rename that replaces what is there, as POSIX's does (the app saves by writing aside, then renaming) */
int sntv_rename(const char *from, const char *to);
#define rename(from, to) sntv_rename(from, to)
#endif
