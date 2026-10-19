/* Source health: which servers (and addons) fail on this TV and network, and at what
 * time of day. Kept in the settings store as "srcHealth": {"host:<h>" or "addon:<name>":
 * {"ok":[4], "fail":[4]}} counted in four six-hour bands; old counts halve as new ones
 * come, so a server that recovers climbs back. */
#ifndef SNTV_SRCHEALTH_H
#define SNTV_SRCHEALTH_H

enum { HEALTH_OK, HEALTH_FAIL, HEALTH_STALL };

/* What happened to a source: it played a good while, it would not play, or it stopped mid-way. */
void srchealth_record(const char *url, const char *addon, int what);
/* 0 when nothing bad is known, up to 900 for a server that keeps failing at this time of day. */
int  srchealth_penalty(const char *url, const char *addon);
/* "fails often in the evening" or NULL, for the source list. */
const char *srchealth_note(const char *url, const char *addon);
#endif
