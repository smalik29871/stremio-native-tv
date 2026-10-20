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

/* ---- torrents from addons, through TorBox instead of peers ----
 * A source that is only a torrent (info hash, no link) plays from TorBox's copy when
 * TorBox has it cached: no P2P, nobody sees this TV's address. Its link is stored as
 * "torbox://hash/<info hash>/<file index>" and opened when the player gets to it. */
struct Stream;
/* On the network thread, after an addon's streams were parsed: asks TorBox which of the
 * torrents it has cached and marks them (tags.cached = 1). */
void torbox_mark_streams(struct Stream *s, int n);
int  torbox_hash_cached(const char *info_hash);      /* seen cached by the check above */
int  torbox_is_hash_link(const char *s);
char *torbox_hash_link(const char *info_hash, int file_idx);   /* malloc'd */
/* Adds the torrent to your TorBox (if it is not there) and gives the link for its file;
 * url is NULL with the reason when TorBox does not have it ready. */
typedef void (*TorboxOpenCb)(const char *url, const char *error, void *user);
void torbox_open_hash(const char *link, TorboxOpenCb cb, void *user);
/* Starts TorBox downloading a torrent this TV is playing over P2P, so it can switch back. */
void torbox_queue_hash(const char *info_hash);
#endif
