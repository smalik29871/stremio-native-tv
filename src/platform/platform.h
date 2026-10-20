/* Compile-time platform selection. Exactly one SNTV_PLATFORM_* is defined by CMake. */
#ifndef SNTV_PLATFORM_H
#define SNTV_PLATFORM_H

#if !defined(SNTV_PLATFORM_LINUX) && !defined(SNTV_PLATFORM_WEBOS) && \
    !defined(SNTV_PLATFORM_TIZEN) && !defined(SNTV_PLATFORM_WASM)
#define SNTV_PLATFORM_LINUX 1
#endif

#define SNTV_APP_ID      "org.stremio.nativetv"
#define SNTV_APP_NAME    "Stremio"
#define SNTV_APP_VERSION "0.10.23"
#define SNTV_USER_AGENT  "StremioNativeTV/" SNTV_APP_VERSION

/* Logical UI canvas. Everything is laid out in these units and scaled to the
 * real window so 720p webOS 3 sets and 4K panels share one layout. */
#define SNTV_UI_W 1920
#define SNTV_UI_H 1080

/* webOS 3 sets have ~300 MB free. Keep decoded artwork well under that. The size cap is
 * for backdrops and logos (posters are mipmapped at 512): a hero is drawn about 1500 px
 * wide, and 512 px stretched that far looks pixelated. */
#if defined(SNTV_PLATFORM_WEBOS) || defined(SNTV_PLATFORM_TIZEN)
#define SNTV_IMAGE_CACHE_BYTES (48u * 1024u * 1024u)
#define SNTV_IMAGE_MAX_DIM 1280
#else
#define SNTV_IMAGE_CACHE_BYTES (128u * 1024u * 1024u)
#define SNTV_IMAGE_MAX_DIM 1024
#endif

#endif
