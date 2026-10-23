/* A relay on the TV itself, for sources whose addon asks for request headers (a Referer,
 * a cookie): the TV's player can't send them, so it opens http://127.0.0.1:<port>/r/<token>
 * and the relay fetches the source with its headers, handing the bytes on as they come.
 * Only this TV can reach it, the address carries a random token (never the link or a
 * key), and when it can't start the source is opened directly, as before. */
#ifndef SNTV_RELAY_H
#define SNTV_RELAY_H

/* The local address that plays url with these headers ("Name: value" lines), malloc'd;
 * NULL when the relay is off or could not start (open url directly then). */
char *relay_url(const char *url, const char *headers);
int  relay_enabled(void);   /* on unless "relay" is 0 in the store (or SNTV_NO_RELAY is set) */
/* The same for the source that plays, plus a buffer ahead of the TV's player: up to two
 * minutes of the file (est_rate bytes a second, 0 unknown) in memory, within what the TV
 * has free; none when there is too little. One at a time: a new one replaces it. */
char *relay_url_buffered(const char *url, const char *headers, double est_rate);
int  relay_buffer_enabled(void);   /* relay on, and "bufferAhead" not 0 (nor SNTV_NO_BUFFER set) */
void relay_set_duration(double seconds);   /* the video's length: the buffer's minutes from its size */
double relay_buffer_ahead(void);   /* seconds of video loaded past what the player read, -1 none */
void relay_buffer_close(void);     /* playback over: the buffer's memory goes */
#endif
