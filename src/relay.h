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
#endif
