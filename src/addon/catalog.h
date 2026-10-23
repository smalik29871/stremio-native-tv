/* Cuelight's own catalog: films and series from TMDB, served in the addon format
 * (manifest, catalogs, meta) so Home, Search and the title page read it like any
 * installed addon. Its address, cuelight://catalog/manifest.json, never leaves the
 * app: http.c hands each request for it to catalog_answer on a network thread. Its
 * ids are IMDb ids, so stream and subtitle addons find the same titles. */
#ifndef SNTV_CATALOG_H
#define SNTV_CATALOG_H
#include "../http.h"

int catalog_handles(const char *url);
/* Blocking (it asks TMDB): fills `out` as a transfer would. 0 when it answered. */
int catalog_answer(const char *url, HttpResponse *out);

#endif
