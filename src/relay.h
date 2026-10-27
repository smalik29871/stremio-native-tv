/* A relay on the TV itself, for sources whose addon asks for request headers (a Referer,
 * a cookie): the TV's player can't send them, so it opens http://127.0.0.1:<port>/r/<token>
 * and the relay fetches the source with its headers, handing the bytes on as they come.
 * Only this TV can reach it, the address carries a random token (never the link or a
 * key), and when it can't start the source is opened directly, as before. */
#ifndef SNTV_RELAY_H
#define SNTV_RELAY_H
#include <stddef.h>
#include <stdint.h>

/* The local address that plays url with these headers ("Name: value" lines), malloc'd;
 * NULL when the relay is off or could not start (open url directly then). */
char *relay_url(const char *url, const char *headers);
int  relay_enabled(void);   /* on unless "relay" is 0 in the store (or SNTV_NO_RELAY is set) */
/* The same for the source that plays, plus a buffer ahead of the TV's player: up to two
 * minutes of the file (est_rate bytes a second, 0 unknown) in memory, as much as the TV can
 * spare from moment to moment; none when there is too little. One at a time: a new one
 * replaces it. */
char *relay_url_buffered(const char *url, const char *headers, double est_rate);
void relay_prime(const char *url, const char *headers, const void *data, size_t len, uint64_t total, const char *type);
int  relay_buffer_enabled(void);   /* relay on, and "bufferAhead" not 0 (nor SNTV_NO_BUFFER set) */
void relay_set_duration(double seconds);   /* the video's length: the buffer's minutes from its size */
double relay_buffer_ahead(void);   /* seconds of video loaded past what the player read, -1 none */
void relay_buffer_close(void);     /* playback over: the buffer's memory goes */
/* What the buffer holds now, [lo, hi) of the file; and a copy of part of it (0: not in). */
int  relay_buffer_span(uint64_t *lo, uint64_t *hi);
int  relay_buffer_copy(uint64_t at, void *out, size_t n);
/* The same, only when url is the file the buffer holds (the bytes then need no network);
 * total: the file's size, 0 unknown. */
int  relay_buffer_read(const char *url, uint64_t at, void *out, size_t n, uint64_t *total);
void relay_buffer_alternates(const char *const *urls, const char *const *headers, int n);   /* links that may lead to the same file: if the source stops partway, the buffer carries on from one that proves identical */
int  relay_buffer_failing(void);   /* the source stopped sending partway and no identical copy took over: what is in plays on */
int  relay_buffer_source_failed(void);
double relay_buffer_waiting_s(void);   /* seconds since the player first asked while not a byte has come, -1 otherwise */   /* the buffered source itself refused or kept failing (not the relay) */
#endif
