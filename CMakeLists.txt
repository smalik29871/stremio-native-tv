cmake_minimum_required(VERSION 3.13)
project(stremio_native_tv C)

# SNTV_PLATFORM: linux (desktop preview), webos, tizen, wasm
set(SNTV_PLATFORM "linux" CACHE STRING "Target platform: linux | webos | tizen | wasm")
option(SNTV_BUILD_TESTS "Build host unit tests" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing)

string(TOUPPER "${SNTV_PLATFORM}" SNTV_PLATFORM_UC)
add_compile_definitions(SNTV_PLATFORM_${SNTV_PLATFORM_UC}=1)

# ---- core: no SDL video dependency, testable on the host --------------------
add_library(sntv_core STATIC
  vendor/cJSON.c
  src/log.c
  src/http.c
  src/store.c
  src/addon/addon.c
  src/addon/stremio_api.c)
target_include_directories(sntv_core PUBLIC src vendor)

# ---- platform libraries -----------------------------------------------------
if(SNTV_PLATFORM STREQUAL "wasm")
  # Emscripten: SDL2 + GLES2 come from the toolchain. Networking goes through
  # fetch() (see src/http_wasm.c) because there is no libcurl to dlopen.
  set(SNTV_LINK_LIBS "")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")
  add_compile_options(-sUSE_SDL=2)
  add_link_options(-sUSE_SDL=2 -sFULL_ES2=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY
                   -sEXPORTED_RUNTIME_METHODS=ccall,cwrap --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets)
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
  pkg_check_modules(LS2 REQUIRED luna-service2 glib-2.0)
  list(APPEND SNTV_LINK_LIBS ${LS2_LIBRARIES})
  set(SNTV_VIDEO_SRC src/video/video_webos.c)
  set(SNTV_PLATFORM_INCLUDES ${LS2_INCLUDE_DIRS})
elseif(SNTV_PLATFORM STREQUAL "tizen")
  pkg_check_modules(TIZEN REQUIRED capi-media-player capi-appfw-application elementary)
  list(APPEND SNTV_LINK_LIBS ${TIZEN_LIBRARIES})
  set(SNTV_VIDEO_SRC src/video/video_tizen.c)
  set(SNTV_PLATFORM_INCLUDES ${TIZEN_INCLUDE_DIRS})
elseif(SNTV_PLATFORM STREQUAL "wasm")
  set(SNTV_VIDEO_SRC src/video/video_wasm.c)
else()
  set(SNTV_VIDEO_SRC src/video/video_null.c)
endif()

# ---- app ---------------------------------------------------------------------
file(GLOB SNTV_UI_SRC CONFIGURE_DEPENDS src/ui/*.c)
add_executable(stremio-tv
  src/main.c
  src/app.c
  src/image.c
  ${SNTV_UI_SRC}
  ${SNTV_VIDEO_SRC})
target_include_directories(stremio-tv PRIVATE ${SNTV_PLATFORM_INCLUDES})
target_link_libraries(stremio-tv PRIVATE sntv_core ${SNTV_LINK_LIBS})

install(TARGETS stremio-tv RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)

# ---- tests -------------------------------------------------------------------
if(SNTV_BUILD_TESTS AND NOT SNTV_PLATFORM STREQUAL "wasm")
  enable_testing()
  add_executable(test_addon tests/test_addon.c)
  target_link_libraries(test_addon PRIVATE sntv_core ${SNTV_LINK_LIBS})
  target_compile_definitions(test_addon PRIVATE SNTV_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME addon_protocol COMMAND test_addon)
endif()
