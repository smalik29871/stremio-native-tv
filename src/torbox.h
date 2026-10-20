/* Your own videos kept on TorBox: listed with your API key, and played through TorBox's
 * permanent link (requestdl with redirect=true), which always forwards to a fresh copy.
 * A pick is stored as "torbox://<torrents|usenet|webdl>/<item id>/<file id>/<file name>"
 * so the key itself is never part of a saved link. */
#ifndef SNTV_TORBOX_H
#define SNTV_TORBOX_H
#include <stddef.h>

int  torbox_enabled(void);                       /* a key is set */
int  torbox_is_link(const char *s);              /* "torbox://..." */
/* The URL the player opens for a stored pick, or NULL (no key, or not a TorBox pick). */
const char *torbox_resolve(const char *stored, char *out, size_t n);
/* The file's name from a stored pick, for showing it. */
const char *torbox_name(const char *stored, char *out, size_t n);

typedef struct { char link[600]; char name[256]; double size_mb; } TorboxFile;
/* Every video file in your TorBox (torrents, usenet, web downloads), on the main thread;
 * files is NULL with a reason when nothing could be read. */
typedef void (*TorboxListCb)(const TorboxFile *files, int n, const char *error, void *user);
void torbox_list_videos(TorboxListCb cb, void *user);
#endif
