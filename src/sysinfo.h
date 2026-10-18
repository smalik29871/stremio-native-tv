/* What the TV says about itself. Newer firmwares hide /etc/starfish-release from apps,
 * so the webOS release is looked up in the places that stay readable. */
#pragma once
#include <stddef.h>

/* "webOS TV Release 9.2.0" (or the kernel when no source answers) and the major
 * version (0 when unknown). `source` names the file it came from, for the log. */
int sysinfo_webos_release(char *out, size_t n, const char **source);

/* Video decoders the system offers to apps (FFmpeg's libavcodec, libde265, OpenH264,
 * GStreamer), for seek previews decoded on the TV. Loads and unloads the libraries,
 * so it runs on a background thread; sysinfo_decoders() is NULL until it finished. */
void sysinfo_probe_decoders(void);
const char *sysinfo_decoders(void);
