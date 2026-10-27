/* Cuelight's key relay (relay/, a Cloudflare Worker): the requests that need the app's own
 * MDBList or OpenSubtitles key go to it without the key, and it adds the key
 * on its side, so the packages need not carry them. A key the viewer entered themselves goes
 * to the service directly, as before. When the relay doesn't answer, or has no key for the
 * service, requests go direct with the key built into the package if there is one, and the
 * relay is tried again ten minutes later. Its address comes from relay/url.txt at build time
 * (SNTV_KEY_RELAY overrides it, for tests). */
#ifndef SNTV_KEYRELAY_H
#define SNTV_KEYRELAY_H

typedef enum { KR_MDBLIST, KR_OPENSUBS, KR_COUNT } KeyRelaySvc;

/* The relay's address for the service ("https://…/mdblist"), the service's own address layout
 * under it; NULL when there is no relay or it failed lately. */
const char *keyrelay_base(KeyRelaySvc s);
/* An answer that came through the relay: 1 when it means the relay can't serve this service
 * (no answer, no key there, too many requests, or the service behind it down), and then the
 * service goes direct for ten minutes. */
int keyrelay_failed(KeyRelaySvc s, long status);

#endif
