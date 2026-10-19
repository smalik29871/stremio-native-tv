/* The P2P engine bundled with the app (tools/torrentd), the fallback for sources that
 * are only a torrent (TorBox has no cached copy, or is down). It runs as its own
 * process on 127.0.0.1:11490, speaks the Stremio streaming server's URLs, keeps pieces
 * in a capped RAM buffer and stops by itself when nothing plays. Used only when no
 * streaming server is set in Settings. */
#ifndef SNTV_TORRENT_H
#define SNTV_TORRENT_H

int  torrent_engine_available(void);   /* the engine is installed next to the app */
/* The engine's URL for a torrent (malloc'ed), or NULL without the engine. */
char *torrent_engine_url(const char *info_hash, int file_idx, const char *trackers);
int  torrent_engine_url_is(const char *url);
/* Start it if needed; wait up to wait_ms for it to answer (0: just start). 1 when it answers. */
int  torrent_engine_start(int wait_ms);
void torrent_engine_drop(const char *url);   /* that torrent can go: frees its RAM now */
void torrent_engine_stop(void);
void torrent_engine_set_cache_mb(int mb);

#endif
