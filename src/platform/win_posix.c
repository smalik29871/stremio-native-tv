/* The Windows build's stand-ins for POSIX calls (win_posix.h), kept out of the header so
 * windows.h stays out of every file. */
#include <windows.h>
#include "win_posix.h"

int sntv_rename(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
}

int sntv_statvfs(const char *path, struct statvfs *out)
{
    ULARGE_INTEGER avail, total;
    if (!GetDiskFreeSpaceExA(path, &avail, &total, NULL)) return -1;
    out->f_frsize = out->f_bsize = 4096;
    out->f_bavail = out->f_bfree = (unsigned long long)avail.QuadPart / 4096;
    out->f_blocks = (unsigned long long)total.QuadPart / 4096;
    return 0;
}
