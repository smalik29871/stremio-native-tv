/* What POSIX gives and Windows spells otherwise, for the few files that need it (sockets,
 * loading a library, a folder's free space). Elsewhere it is the system's own headers. */
#ifndef SNTV_COMPAT_H
#define SNTV_COMPAT_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <stdio.h>
#undef near   /* old 16-bit keywords windows.h still defines, and the app's names */
#undef far

/* LoadLibrary as dlopen */
#define RTLD_NOW 0
#define RTLD_LAZY 0
#define RTLD_LOCAL 0
static inline void *dlopen(const char *name, int flags) { (void)flags; return (void *)LoadLibraryA(name); }
static inline void *dlsym(void *h, const char *sym) { return (void *)(intptr_t)GetProcAddress((HMODULE)h, sym); }
static inline int dlclose(void *h) { return FreeLibrary((HMODULE)h) ? 0 : -1; }
static inline const char *dlerror(void) { static char b[48]; snprintf(b, sizeof b, "Windows error %lu", GetLastError()); return b; }

/* sockets: closesocket, millisecond timeouts, FIONBIO, WSAPoll */
#define sock_close(fd) closesocket((SOCKET)(fd))
#define SHUT_RDWR SD_BOTH
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
typedef ULONG nfds_t;
#define poll(fds, n, ms) WSAPoll((fds), (n), (ms))
static inline void sock_timeout(int fd, int which, int seconds) { DWORD ms = (DWORD)seconds * 1000; setsockopt((SOCKET)fd, SOL_SOCKET, which, (const char *)&ms, sizeof ms); }
static inline void sock_nonblock(int fd) { u_long on = 1; ioctlsocket((SOCKET)fd, FIONBIO, &on); }
static inline int sock_in_progress(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
/* once, before the first socket */
static inline void sock_startup(void) { static int done; if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = 1; } }

#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#define sock_close(fd) close(fd)
static inline void sock_timeout(int fd, int which, int seconds) { struct timeval tv = { seconds, 0 }; setsockopt(fd, SOL_SOCKET, which, &tv, sizeof tv); }
static inline void sock_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
static inline int sock_in_progress(void) { return errno == EINPROGRESS; }
static inline void sock_startup(void) {}
#endif

#endif
