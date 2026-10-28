cmake_minimum_required(VERSION 3.13)
project(stremio_native_tv C)

# SNTV_PLATFORM: linux (desktop preview), webos, tizen, wasm, android (deploy/android runs it),
# windows (tools/windows.sh cross-compiles it with MinGW-w64)
set(SNTV_PLATFORM "linux" CACHE STRING "Target platform: linux | webos | tizen | wasm | android | windows")
option(SNTV_BUILD_TESTS "Build host unit tests" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)

string(TOUPPER "${SNTV_PLATFORM}" SNTV_PLATFORM_UC)
add_compile_definitions(SNTV_PLATFORM_${SNTV_PLATFORM_UC}=1)

# ---- libwebp decoder (vendor/libwebp, BSD): WebP posters on TVs without system support
file(GLOB WEBP_SRC vendor/libwebp/src/dec/*.c vendor/libwebp/src/dsp/*.c vendor/libwebp/src/utils/*.c)
add_library(webpdecoder STATIC ${WEBP_SRC})
target_include_directories(webpdecoder PUBLIC vendor/libwebp)
target_compile_options(webpdecoder PRIVATE -w)   # third-party code: keep our own warnings readable
set_target_properties(webpdecoder PROPERTIES C_STANDARD 99)

# ---- QuickJS-ng (vendor/quickjs, MIT): runs YouTube's challenge solver on the TV
add_library(quickjs STATIC vendor/quickjs/quickjs.c vendor/quickjs/libregexp.c vendor/quickjs/libunicode.c vendor/quickjs/dtoa.c)
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE __STDC_NO_ATOMICS__=1)
target_compile_options(quickjs PRIVATE -w)
set_target_properties(quickjs PROPERTIES C_STANDARD 11)
if(NOT SNTV_PLATFORM STREQUAL "wasm")
  target_link_libraries(quickjs PUBLIC m)
endif()

# ---- core: no SDL video dependency, testable on the host --------------------
add_library(sntv_core STATIC
  vendor/cJSON.c
  vendor/qrcodegen.c
  src/log.c
  src/playback.c
  src/builtin_keys.c src/keyrelay.c
  src/http.c
  src/relay.c
  src/mkvsubs.c
  src/store.c
  src/sha256.c
  src/subs.c
  src/skip.c
  src/quality.c
  src/probe.c
  src/tmdb.c
  src/tracking.c
  src/opensubs.c
  src/srchealth.c
  src/verdict.c
  src/party.c
  src/ai.c
  src/memlevel.c
  src/sysinfo.c
  src/youtube.c
  src/torbox.c
  src/frames.c
  src/tidy.c
  src/ratings.c
  src/music.c
  src/weather.c
  src/depth.c
  src/torrent.c
  src/suntimes.c
  src/newep.c
  src/advisory.c
  src/lang.c
  src/trackmem.c
  src/lists.c
  src/addon/addon.c
  src/addon/catalog.c
  src/addon/stremio_api.c)
target_include_directories(sntv_core PUBLIC src vendor)
target_link_libraries(sntv_core PUBLIC quickjs)
# FFmpeg (tools/ffmpeg.sh: H.264 and HEVC decoders only) for seek previews decoded on the
# TV: shared libraries loaded at run time on the TV, static in the tests. Without it the app
# builds and previews need the home helper.
set(SNTV_FFMPEG_PREFIX "" CACHE PATH "Prefix of the FFmpeg built by tools/ffmpeg.sh")
# The desktop player (src/video/video_ffmpeg.c) on Linux or Windows: the fuller FFmpeg of
# tools/ffmpeg-player.sh, which also serves the seek previews. Without it the desktop build
# simulates playback (video_null.c), as the tests want.
set(SNTV_PLAYER_FFMPEG "" CACHE PATH "Prefix of the FFmpeg built by tools/ffmpeg-player.sh")
if(SNTV_PLAYER_FFMPEG)
  set(SNTV_FFMPEG_PREFIX ${SNTV_PLAYER_FFMPEG})
endif()
if(SNTV_FFMPEG_PREFIX)
  target_compile_definitions(sntv_core PUBLIC SNTV_HAVE_FFMPEG=1)
  target_include_directories(sntv_core PRIVATE ${SNTV_FFMPEG_PREFIX}/include)
  if(SNTV_PLAYER_FFMPEG)
    set(FF ${SNTV_PLAYER_FFMPEG}/lib)
    target_include_directories(sntv_core PUBLIC ${SNTV_PLAYER_FFMPEG}/include)
    target_compile_definitions(sntv_core PUBLIC SNTV_DESKTOP_PLAYER=1)
    if(EXISTS ${FF}/libavformat.dll.a)   # Windows: FFmpeg's DLLs beside the exe (replaceable, as its LGPL asks); dav1d and zlib are inside them
      target_link_libraries(sntv_core PUBLIC ${FF}/libavformat.dll.a ${FF}/libavcodec.dll.a ${FF}/libswscale.dll.a ${FF}/libswresample.dll.a ${FF}/libavutil.dll.a)
    else()
      target_link_libraries(sntv_core PUBLIC ${FF}/libavformat.a ${FF}/libavcodec.a ${FF}/libswscale.a ${FF}/libswresample.a ${FF}/libavutil.a)
    endif()
    if(EXISTS ${FF}/libavformat.dll.a)
    elseif(EXISTS ${FF}/libdav1d.a)   # AV1 in software (the Windows build has it; Linux when the system had it)
      target_link_libraries(sntv_core PUBLIC ${FF}/libdav1d.a)
    elseif(NOT SNTV_PLATFORM STREQUAL "windows" AND EXISTS ${FF}/pkgconfig/libavcodec.pc)
      file(READ ${FF}/pkgconfig/libavcodec.pc SNTV_AVC_PC)
      if(SNTV_AVC_PC MATCHES "dav1d")
        target_link_libraries(sntv_core PUBLIC dav1d)
      endif()
    endif()
    if(SNTV_PLATFORM STREQUAL "windows")
      if(NOT EXISTS ${FF}/libavformat.dll.a)
        target_link_libraries(sntv_core PUBLIC ${FF}/libz.a)
      endif()
      target_link_libraries(sntv_core PUBLIC ws2_32 secur32 bcrypt crypt32 ncrypt ole32 user32 d3d11 dxgi)
    else()
      target_link_libraries(sntv_core PUBLIC ssl crypto z m)
    endif()
  elseif(EXISTS ${SNTV_FFMPEG_PREFIX}/lib/libavcodec.so)   # shared (the TV's): loaded at run time from lib/ beside the app
    target_compile_definitions(sntv_core PUBLIC SNTV_FFMPEG_DLOPEN=1)
    target_link_libraries(sntv_core PUBLIC dl m)
  else()
    target_link_libraries(sntv_core PUBLIC ${SNTV_FFMPEG_PREFIX}/lib/libavcodec.a ${SNTV_FFMPEG_PREFIX}/lib/libavutil.a m)
  endif()
endif()
# Optional: service keys built into the app (CI takes them from repository secrets), so cast
# photos, age ratings, Trakt, SimKL, MDBList and OpenSubtitles work without the viewer entering
# one. They go in scrambled (each byte XORed with a mask that changes along the key), never as
# plain text a `strings` of the package would show; src/builtin_keys.c unscrambles them in memory.
function(sntv_scramble out_var value salt)
  set(bytes "")
  if(NOT value STREQUAL "")
    file(WRITE "${CMAKE_BINARY_DIR}/sntv_key.tmp" "${value}")
    file(READ "${CMAKE_BINARY_DIR}/sntv_key.tmp" hex HEX)
    file(REMOVE "${CMAKE_BINARY_DIR}/sntv_key.tmp")
    string(LENGTH "${hex}" hlen)
    math(EXPR last "${hlen} / 2 - 1")
    foreach(i RANGE 0 ${last})
      math(EXPR at "${i} * 2")
      string(SUBSTRING "${hex}" ${at} 2 h)
      math(EXPR b "(0x${h} ^ ((${salt} + ${i} * 29 + 0xA5) % 256))")
      string(APPEND bytes "${b},")
    endforeach()
  endif()
  set(${out_var} "${bytes}" PARENT_SCOPE)
endfunction()
set(SNTV_KEYS_H "/* generated by CMake from the build's key settings: scrambled, see CMakeLists.txt */\n")
set(salt 17)
foreach(k TMDB_BUILTIN_KEY TRAKT_CLIENT_ID TRAKT_CLIENT_SECRET SIMKL_CLIENT_ID MDBLIST_KEY OPENSUBTITLES_KEY)
  set(SNTV_${k} "" CACHE STRING "Built-in ${k} (CI secret)")
  sntv_scramble(bytes "${SNTV_${k}}" ${salt})
  if(NOT bytes STREQUAL "")
    string(APPEND SNTV_KEYS_H "#define SNTV_HAVE_${k} 1\nstatic const unsigned char sntv_key_${k}[] = { ${bytes} };\nenum { sntv_salt_${k} = ${salt} };\n")
  endif()
  math(EXPR salt "${salt} + 53")
endforeach()
# The key relay's address (relay/README.md): not a secret, so in plain text; relay/url.txt holds
# it once the relay is deployed, and SNTV_KEY_RELAY set here wins over the file.
set(SNTV_KEY_RELAY "" CACHE STRING "Key relay address, e.g. https://cuelight-keys.example.workers.dev")
set(sntv_relay "${SNTV_KEY_RELAY}")
if(sntv_relay STREQUAL "" AND EXISTS "${CMAKE_SOURCE_DIR}/relay/url.txt")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/relay/url.txt")
  file(STRINGS "${CMAKE_SOURCE_DIR}/relay/url.txt" sntv_relay REGEX "^https://[^ \"]+$" LIMIT_COUNT 1)
endif()
string(REGEX REPLACE "/+$" "" sntv_relay "${sntv_relay}")
if(NOT sntv_relay STREQUAL "")
  string(APPEND SNTV_KEYS_H "#define SNTV_KEY_RELAY_URL \"${sntv_relay}\"\n")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/generated/builtin_keys_data.h.new" "${SNTV_KEYS_H}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/generated/builtin_keys_data.h.new" "${CMAKE_BINARY_DIR}/generated/builtin_keys_data.h")
target_include_directories(sntv_core PUBLIC "${CMAKE_BINARY_DIR}/generated")

# ---- platform libraries -----------------------------------------------------
if(SNTV_PLATFORM STREQUAL "wasm")
  # Emscripten: SDL2 + GLES2 come from the toolchain. Networking goes through
  # fetch() (see src/http_wasm.c) because there is no libcurl to dlopen.
  set(SNTV_LINK_LIBS "")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  add_compile_options(-sUSE_SDL=2)
  add_link_options(-sUSE_SDL=2 -sFULL_ES2=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY
                   -sEXPORTED_RUNTIME_METHODS=ccall,cwrap --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets)
elseif(SNTV_PLATFORM STREQUAL "windows")
  # SDL2 and ANGLE (OpenGL ES 2 over Direct3D) prebuilt by MSYS2, fetched by tools/windows.sh;
  # libcurl.dll built here from curl's source with Windows' own TLS (Schannel) and loaded
  # at run time, as libcurl is everywhere.
  set(SNTV_WIN_DEPS "" CACHE PATH "MSYS2 mingw64 tree with SDL2 and ANGLE (tools/windows.sh)")
  set(SNTV_WIN_CURL "" CACHE PATH "curl source tree (tools/windows.sh)")
  target_include_directories(sntv_core PUBLIC ${SNTV_WIN_DEPS}/include/SDL2 ${SNTV_WIN_DEPS}/include)
  target_compile_definitions(sntv_core PUBLIC _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
  target_compile_options(sntv_core PUBLIC -include ${CMAKE_SOURCE_DIR}/src/platform/win_posix.h)
  target_sources(sntv_core PRIVATE src/platform/win_posix.c)
  target_link_libraries(sntv_core PUBLIC ws2_32 iphlpapi shlwapi)
  set(SNTV_LINK_LIBS mingw32 ${SNTV_WIN_DEPS}/lib/libSDL2main.a ${SNTV_WIN_DEPS}/lib/libSDL2.dll.a
      ${SNTV_WIN_DEPS}/lib/libGLESv2.dll.a ws2_32 iphlpapi shlwapi m)
  foreach(o CURL_USE_SCHANNEL CURL_DISABLE_LDAP CURL_ZLIB CURL_WINDOWS_SSPI)
    set(${o} ON CACHE BOOL "" FORCE)
  endforeach()
  foreach(o CURL_USE_OPENSSL BUILD_CURL_EXE BUILD_TESTING BUILD_LIBCURL_DOCS BUILD_MISC_DOCS ENABLE_CURL_MANUAL CURL_USE_LIBPSL
            CURL_USE_LIBSSH2 USE_NGHTTP2 USE_LIBIDN2 CURL_USE_PKGCONFIG BUILD_STATIC_LIBS CURL_BROTLI CURL_ZSTD)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(ZLIB_INCLUDE_DIR ${SNTV_WIN_DEPS}/include CACHE PATH "" FORCE)
  set(ZLIB_LIBRARY ${SNTV_WIN_DEPS}/lib/libz.dll.a CACHE FILEPATH "" FORCE)
  set(BUILD_SHARED_LIBS ON)
  add_subdirectory(${SNTV_WIN_CURL} curl EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS OFF)
elseif(SNTV_PLATFORM STREQUAL "android")
  # SDL2 built from its source (tools/android-deps.sh fetches it), GLES2, log and android
  # from the NDK. libcurl is loaded at run time as everywhere (shipped in the APK).
  set(SNTV_SDL2_SOURCE "${CMAKE_SOURCE_DIR}/deploy/android/third_party/SDL2" CACHE PATH "SDL2 source tree")
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SNTV_SDL2_SOURCE} sdl2 EXCLUDE_FROM_ALL)
  target_link_libraries(sntv_core PUBLIC SDL2::SDL2 log)
  set(SNTV_LINK_LIBS SDL2::SDL2 GLESv2 log android m dl)
  # libcurl.so for the APK: curl with mbedTLS (and the NDK's zlib), both from source.
  # src/http.c loads it at run time, as it loads the TV's own everywhere else.
  set(SNTV_TP ${CMAKE_SOURCE_DIR}/deploy/android/third_party)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  foreach(o ENABLE_PROGRAMS ENABLE_TESTING GEN_FILES MBEDTLS_FATAL_WARNINGS)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(${SNTV_TP}/mbedtls mbedtls EXCLUDE_FROM_ALL)
  set(MBEDTLS_INCLUDE_DIR ${SNTV_TP}/mbedtls/include CACHE PATH "" FORCE)
  set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
  set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
  set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
  foreach(o CURL_USE_MBEDTLS CURL_DISABLE_LDAP CURL_ZLIB)
    set(${o} ON CACHE BOOL "" FORCE)
  endforeach()
  foreach(o CURL_USE_OPENSSL BUILD_CURL_EXE BUILD_TESTING BUILD_LIBCURL_DOCS BUILD_MISC_DOCS ENABLE_CURL_MANUAL CURL_USE_LIBPSL
            CURL_USE_LIBSSH2 USE_NGHTTP2 USE_LIBIDN2 CURL_USE_PKGCONFIG BUILD_STATIC_LIBS CURL_BROTLI CURL_ZSTD)
    set(${o} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(CURL_CA_BUNDLE none CACHE STRING "" FORCE)   # src/http.c hands it Android's roots
  set(CURL_CA_PATH none CACHE STRING "" FORCE)
  set(BUILD_SHARED_LIBS ON)
  add_subdirectory(${SNTV_TP}/curl curl EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS OFF)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED sdl2)
  pkg_check_modules(GLES2 glesv2)
  set(SNTV_LINK_LIBS ${SDL2_LIBRARIES} ${GLES2_LIBRARIES} m dl pthread)
  target_include_directories(sntv_core PUBLIC ${SDL2_INCLUDE_DIRS} ${GLES2_INCLUDE_DIRS})
  target_compile_options(sntv_core PUBLIC ${SDL2_CFLAGS_OTHER})
  if(NOT GLES2_FOUND)
    # The webOS/Tizen sysroots ship libGLESv2 without a .pc file.
    list(APPEND SNTV_LINK_LIBS GLESv2)
  endif()
endif()

if(SNTV_PLATFORM STREQUAL "webos")
  # LS2, glib, ACB and the SDL webOS extensions are all dlopen'ed at run time
  # (src/video/video_webos.c), so nothing beyond SDL2 + GLESv2 is linked. That
  # keeps one binary loadable from webOS 3 to current firmware.
  set(SNTV_VIDEO_SRC src/video/video_webos.c)
  set(SNTV_PLATFORM_INCLUDES "")
elseif(SNTV_PLATFORM STREQUAL "tizen")
  pkg_check_modules(TIZEN REQUIRED capi-media-player capi-appfw-application elementary)
  list(APPEND SNTV_LINK_LIBS ${TIZEN_LIBRARIES})
  set(SNTV_VIDEO_SRC src/video/video_tizen.c)
  set(SNTV_PLATFORM_INCLUDES ${TIZEN_INCLUDE_DIRS})
elseif(SNTV_PLATFORM STREQUAL "wasm")
  set(SNTV_VIDEO_SRC src/video/video_wasm.c)
elseif(SNTV_PLATFORM STREQUAL "windows")
  set(SNTV_VIDEO_SRC src/video/video_ffmpeg.c)
elseif(SNTV_PLATFORM STREQUAL "android")
  set(SNTV_VIDEO_SRC src/video/video_android.c)   # ExoPlayer over JNI (CuelightPlayer.java)
  # the bundled files, listed for unpacking from the APK (src/main.c)
  file(GLOB_RECURSE SNTV_ASSET_FILES RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/assets/*)
  set(SNTV_ASSET_INDEX "/* generated by CMake: the files under assets/, unpacked at first start */\nstatic const char *const ASSET_FILES[] = {\n")
  foreach(f ${SNTV_ASSET_FILES})
    string(APPEND SNTV_ASSET_INDEX "    \"${f}\",\n")
  endforeach()
  string(APPEND SNTV_ASSET_INDEX "    NULL,\n};\n")
  file(WRITE ${CMAKE_BINARY_DIR}/generated/assets_index.h "${SNTV_ASSET_INDEX}")
elseif(SNTV_PLAYER_FFMPEG)
  set(SNTV_VIDEO_SRC src/video/video_ffmpeg.c)
else()
  set(SNTV_VIDEO_SRC src/video/video_null.c)
endif()

# ---- app ---------------------------------------------------------------------
file(GLOB SNTV_UI_SRC CONFIGURE_DEPENDS src/ui/*.c)
# What's new: this version's CHANGELOG.md section, built in (tools/whatsnew.cmake)
set(SNTV_WHATSNEW ${CMAKE_BINARY_DIR}/generated/whatsnew.h)
add_custom_command(OUTPUT ${SNTV_WHATSNEW}
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${SNTV_WHATSNEW} -P ${CMAKE_SOURCE_DIR}/tools/whatsnew.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/CHANGELOG.md ${CMAKE_SOURCE_DIR}/src/platform/platform.h ${CMAKE_SOURCE_DIR}/tools/whatsnew.cmake
  COMMENT "What's new from CHANGELOG.md")
set(SNTV_APP_SRC
  src/main.c
  src/app.c
  src/image.c
  src/audio.c
  src/breaks.c
  src/phone.c
  src/perfstat.c
  src/memwatch.c
  ${SNTV_UI_SRC}
  ${SNTV_VIDEO_SRC}
  ${SNTV_WHATSNEW})
if(SNTV_PLATFORM STREQUAL "android")
  add_library(stremio-tv SHARED ${SNTV_APP_SRC})   # libmain.so, what SDL's activity loads
  set_target_properties(stremio-tv PROPERTIES OUTPUT_NAME main)
  add_dependencies(stremio-tv libcurl_shared)       # built and packaged with it, loaded at run time
elseif(SNTV_PLATFORM STREQUAL "windows")
  enable_language(RC)   # the icon and version (deploy/windows/cuelight.rc)
  file(STRINGS src/platform/platform.h SNTV_VER_LINE REGEX "SNTV_APP_VERSION \"")
  string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" SNTV_VER "${SNTV_VER_LINE}")
  set_source_files_properties(deploy/windows/cuelight.rc PROPERTIES COMPILE_DEFINITIONS
      "SNTV_VER_MAJOR=${CMAKE_MATCH_1};SNTV_VER_MINOR=${CMAKE_MATCH_2};SNTV_VER_PATCH=${CMAKE_MATCH_3};SNTV_VER_STRING=\\\"${SNTV_VER}\\\"")
  add_executable(stremio-tv WIN32 ${SNTV_APP_SRC} deploy/windows/cuelight.rc)   # a window, no console
  add_dependencies(stremio-tv libcurl_shared)        # libcurl.dll beside it, loaded at run time
else()
  add_executable(stremio-tv ${SNTV_APP_SRC})
endif()
target_include_directories(stremio-tv PRIVATE ${SNTV_PLATFORM_INCLUDES} ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(stremio-tv PRIVATE sntv_core webpdecoder ${SNTV_LINK_LIBS})

install(TARGETS stremio-tv RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)

# ---- tests -------------------------------------------------------------------
if(SNTV_BUILD_TESTS AND NOT SNTV_PLATFORM STREQUAL "wasm")
  enable_testing()
  add_executable(test_playback tests/test_playback.c)
  target_link_libraries(test_playback PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME playback_progress COMMAND test_playback)
  add_executable(test_log tests/test_log.c)
  target_link_libraries(test_log PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME diagnostic_redaction COMMAND test_log)
  add_executable(test_party tests/test_party.c)
  target_link_libraries(test_party PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME cue_party COMMAND test_party)
  add_executable(test_perfstat tests/test_perfstat.c src/perfstat.c)
  target_link_libraries(test_perfstat PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME playback_diagnostics COMMAND test_perfstat)
  if(SNTV_PLATFORM STREQUAL "linux")
    add_executable(test_webos_load tests/test_webos_load.c)
    target_link_libraries(test_webos_load PRIVATE sntv_core ${SNTV_LINK_LIBS})
    add_test(NAME webos_cancelled_load COMMAND test_webos_load)
  endif()
  if(SNTV_PLAYER_FFMPEG)
    add_executable(test_player_ffmpeg tests/test_player_ffmpeg.c src/audio.c)
    target_link_libraries(test_player_ffmpeg PRIVATE sntv_core ${SNTV_LINK_LIBS})
    add_test(NAME desktop_clock_and_timestamps COMMAND test_player_ffmpeg)
  endif()
  add_executable(test_theme tests/test_theme.c src/ui/theme.c src/ui/gfx.c)
  target_link_libraries(test_theme PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME theme_contracts COMMAND test_theme)
  set_tests_properties(theme_contracts PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  # built-in keys: a known test key through the same scrambling
  sntv_scramble(test_key_bytes "test-key-0123456789abcdef" 17)
  file(WRITE "${CMAKE_BINARY_DIR}/generated_test/builtin_keys_data.h"
       "#define SNTV_HAVE_TMDB_BUILTIN_KEY 1\nstatic const unsigned char sntv_key_TMDB_BUILTIN_KEY[] = { ${test_key_bytes} };\nenum { sntv_salt_TMDB_BUILTIN_KEY = 17 };\n")
  add_executable(test_keys tests/test_keys.c src/builtin_keys.c)
  target_include_directories(test_keys BEFORE PRIVATE "${CMAKE_BINARY_DIR}/generated_test" src)
  target_include_directories(test_keys PRIVATE $<TARGET_PROPERTY:sntv_core,INTERFACE_INCLUDE_DIRECTORIES>)
  target_link_libraries(test_keys PRIVATE ${SNTV_LINK_LIBS})
  add_test(NAME builtin_keys COMMAND test_keys)
  add_executable(test_addon tests/test_addon.c)
  target_link_libraries(test_addon PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_addon PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME addon_protocol COMMAND test_addon)
  add_executable(test_subs tests/test_subs.c)
  target_link_libraries(test_subs PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_subs PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME subtitles COMMAND test_subs)
  add_executable(test_skip tests/test_skip.c)
  target_link_libraries(test_skip PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_skip PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME skip_markers COMMAND test_skip)
  add_executable(test_prefs tests/test_prefs.c)
  target_link_libraries(test_prefs PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME language_and_track_memory COMMAND test_prefs)
  add_executable(test_quality tests/test_quality.c)
  target_link_libraries(test_quality PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME source_quality COMMAND test_quality)
  add_executable(test_sha256 tests/test_sha256.c)
  target_link_libraries(test_sha256 PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME sha256 COMMAND test_sha256)
  add_executable(test_sun tests/test_sun.c)
  target_link_libraries(test_sun PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME sun_times COMMAND test_sun)
  add_executable(test_mem tests/test_mem.c)
  target_link_libraries(test_mem PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME memory_watch COMMAND test_mem)
  add_executable(test_probe tests/test_probe.c)
  target_link_libraries(test_probe PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_probe PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME container_probe COMMAND test_probe)
  add_executable(test_http tests/test_http.c)
  target_link_libraries(test_http PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME http_media_check COMMAND test_http)
  add_executable(test_pack tests/test_pack.c)
  target_link_libraries(test_pack PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME pack_episode_pick COMMAND test_pack)
  add_executable(test_store tests/test_store.c)
  target_link_libraries(test_store PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME settings_survive_power_cuts COMMAND test_store)
  add_executable(test_lists tests/test_lists.c)
  target_link_libraries(test_lists PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME saved_lists COMMAND test_lists)
  add_executable(test_mkvsubs tests/test_mkvsubs.c)
  target_link_libraries(test_mkvsubs PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_mkvsubs PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME mkv_subtitles COMMAND test_mkvsubs)
  add_executable(test_tidy tests/test_tidy.c)
  target_link_libraries(test_tidy PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME data_folder_tidy COMMAND test_tidy)
  add_executable(test_frames tests/test_frames.c)
  target_link_libraries(test_frames PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_frames PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME seek_frames COMMAND test_frames)
  add_executable(test_youtube tests/test_youtube.c)
  target_link_libraries(test_youtube PRIVATE sntv_core ${SNTV_LINK_LIBS})
  add_test(NAME youtube COMMAND test_youtube)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)   # the UI runs, split for CI machines: each part keeps every run with its checks
    add_test(NAME smoke_shards COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tests/check_shards.py ${CMAKE_SOURCE_DIR}/tests/smoke.sh)
  endif()
  set_tests_properties(youtube PROPERTIES ENVIRONMENT "SNTV_YT_SOLVER_DIR=${CMAKE_SOURCE_DIR}/assets/yt")
endif()
