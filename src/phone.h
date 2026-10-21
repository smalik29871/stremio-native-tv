/* Type on your phone: a tiny web page served by the TV on the home network. When a
 * text field is open on the TV it shows a QR code; the phone opens the page, the
 * viewer pastes the key or URL there, and it lands in the field. Nothing leaves
 * the network and the page only accepts text while a field is open. */
#ifndef SNTV_PHONE_H
#define SNTV_PHONE_H
#include <stddef.h>
void phone_start(void);                          /* listens on 11471 (or the next free port) */
void phone_stop(void);
int  phone_url(char *out, size_t n);             /* "http://192.168.1.20:11471/" or 0 when not listening */
/* The field the page offers; NULL title closes it. */
void phone_prompt(const char *title, const char *hint, const char *current);
/* 1 when the phone sent text for the open field (copied into out). */
int  phone_take(char *out, size_t n);
/* Also accept an uploaded recording for `slot` ("start" or "end") on the open field's page.
 * The upload lands in <data>/sounds/<slot>.<ext> and phone_take() returns "sntv-sound://<slot>.<ext>". */
void phone_accept_file(const char *slot);
/* http://127.0.0.1:<port>/<rel> on this TV, or 0 when the server is not running. */
int phone_local_url(const char *rel, char *out, size_t n);
/* "sntv-sound://x.mp3" -> a URL the media pipeline can open; other URLs are copied unchanged. */
const char *phone_resolve_sound(const char *url, char *out, size_t n);
/* Settings backup: while json is set the phone page offers it for download and takes
 * a backup back; NULL closes both. phone_take_restore() returns a sent backup (malloc'd). */
void phone_offer_backup(const char *json);
char *phone_take_restore(void);
/* Diagnostics: offers `text` at a link with a fresh random key (written to url, 1 when
 * the server is up) while the QR is open; NULL takes it down. */
int phone_offer_log(const char *text, char *url, size_t n);
#endif
