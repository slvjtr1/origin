# libSDL2.so and liborigin.so for the Android TV activity. ORIGIN_ROOT is the repository (passed by app/build.gradle.kts);
# SDL2, curl and mbedTLS sources are in ports/androidtv/deps (fetch-deps.sh).
cmake_minimum_required(VERSION 3.22)
project(origin C CXX)
set(R ${ORIGIN_ROOT})
set(D ${R}/ports/androidtv/deps)
set(TP ${R}/third_party)
# no build-machine folders in the libraries (source names kept for error messages): the repository, the NDK, the home folder
file(TO_CMAKE_PATH "$ENV{USERPROFILE}" ORIGIN_HOME)
if(NOT ORIGIN_HOME)
  file(TO_CMAKE_PATH "$ENV{HOME}" ORIGIN_HOME)
endif()
if(ORIGIN_HOME)
  add_compile_options(-ffile-prefix-map=${ORIGIN_HOME}=/src)
endif()
add_compile_options(-ffile-prefix-map=${ANDROID_NDK}=/ndk -ffile-prefix-map=${R}=/app)

add_subdirectory(${D}/SDL ${CMAKE_BINARY_DIR}/sdl)

set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(${D}/mbedtls ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(MBEDTLS_INCLUDE_DIRS ${D}/mbedtls/include CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
add_subdirectory(${D}/curl ${CMAKE_BINARY_DIR}/curl EXCLUDE_FROM_ALL)

# third-party, as tools/build.sh builds it
file(GLOB FT_SRC ${TP}/freetype/src/base/ftbase.c ${TP}/freetype/src/base/ftinit.c ${TP}/freetype/src/base/ftsystem.c ${TP}/freetype/src/base/ftdebug.c
  ${TP}/freetype/src/base/ftbitmap.c ${TP}/freetype/src/base/ftglyph.c ${TP}/freetype/src/base/ftmm.c ${TP}/freetype/src/base/ftbbox.c
  ${TP}/freetype/src/base/ftsynth.c ${TP}/freetype/src/base/ftstroke.c ${TP}/freetype/src/base/fttype1.c ${TP}/freetype/src/gzip/ftgzip.c
  ${TP}/freetype/src/truetype/truetype.c ${TP}/freetype/src/sfnt/sfnt.c ${TP}/freetype/src/smooth/smooth.c ${TP}/freetype/src/autofit/autofit.c
  ${TP}/freetype/src/psnames/psnames.c ${TP}/freetype/src/pshinter/pshinter.c ${TP}/freetype/src/cff/cff.c ${TP}/freetype/src/psaux/psaux.c)
add_library(tp_ft STATIC ${FT_SRC})
target_compile_definitions(tp_ft PRIVATE FT2_BUILD_LIBRARY "FT_CONFIG_MODULES_H=<ftmodule-sb.h>")
target_include_directories(tp_ft PUBLIC ${TP}/freetype/include PRIVATE ${TP}/config)
target_compile_options(tp_ft PRIVATE -w)

add_library(tp_hb STATIC ${TP}/harfbuzz/src/harfbuzz.cc)
target_compile_definitions(tp_hb PRIVATE HAVE_FREETYPE=1 HAVE_PTHREAD=1)
target_compile_options(tp_hb PRIVATE -w -fno-exceptions -fno-rtti)
target_include_directories(tp_hb PUBLIC ${TP}/harfbuzz/src)
target_link_libraries(tp_hb PUBLIC tp_ft)

file(GLOB FB_SRC ${TP}/fribidi/lib/fribidi*.c)
add_library(tp_fb STATIC ${FB_SRC})
target_compile_definitions(tp_fb PUBLIC HAVE_CONFIG_H)
target_include_directories(tp_fb PUBLIC ${TP}/fribidi/lib ${TP}/config/fribidi)
target_compile_options(tp_fb PRIVATE -w)

file(GLOB WEBP_SRC ${TP}/webp/src/dec/*.c ${TP}/webp/src/dsp/*.c ${TP}/webp/src/utils/*.c)
list(FILTER WEBP_SRC EXCLUDE REGEX "(enc|writer|quant_levels_utils|_sse|_msa|_mips)[^/]*\.c$")
add_library(tp_webp STATIC ${WEBP_SRC})
target_compile_definitions(tp_webp PRIVATE WEBP_REDUCE_CSP)
target_include_directories(tp_webp PUBLIC ${TP}/webp)
target_compile_options(tp_webp PRIVATE -w)

file(GLOB ASS_SRC ${TP}/libass/libass/*.c ${TP}/libass/libass/c/*.c)
add_library(tp_ass STATIC ${ASS_SRC})
target_compile_definitions(tp_ass PRIVATE HAVE_CONFIG_H)
target_include_directories(tp_ass PRIVATE ${TP}/config/libass ${TP}/libass/libass)
target_compile_options(tp_ass PRIVATE -w -std=gnu11)
target_link_libraries(tp_ass PUBLIC tp_hb tp_fb tp_ft)

file(GLOB CRYPTO_SRC ${TP}/crypto/*.c)
add_library(tp_misc STATIC ${TP}/cjson/cJSON.c ${TP}/qrcodegen/qrcodegen.c ${CRYPTO_SRC})
target_include_directories(tp_misc PUBLIC ${TP}/cjson ${TP}/qrcodegen ${TP}/crypto ${TP}/stb)
target_compile_options(tp_misc PRIVATE -w)

# the app: the same sources as webOS's, with this platform folder
file(GLOB APP_SRC ${R}/src/core/*.c ${R}/src/ui/*.c ${R}/src/screens/*.c ${R}/src/platform/android/*.c ${R}/src/main.c)
add_library(origin SHARED ${APP_SRC})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ORIGIN_ROOT}/pkg/appinfo.json") # (a new version: configured again)
file(READ "${ORIGIN_ROOT}/pkg/appinfo.json" ORIGIN_APPINFO)
string(JSON ORIGIN_VERSION GET "${ORIGIN_APPINFO}" version)
target_compile_definitions(origin PRIVATE SB_ANDROID=1 _GNU_SOURCE SB_APP_ID="${ORIGIN_APP_ID}" SB_VERSION="${ORIGIN_VERSION}")
target_compile_options(origin PRIVATE -std=c11 -O2 -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_include_directories(origin PRIVATE ${R}/src ${CMAKE_CURRENT_BINARY_DIR} ${TP}/libass ${D}/SDL/include ${CMAKE_BINARY_DIR}/sdl/include)
# (SDL's headers as <SDL2/SDL.h>, the way the sources include them)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/SDL2)
file(GLOB SDL_HEADERS ${D}/SDL/include/*.h)
file(COPY ${SDL_HEADERS} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/SDL2)
# the built-in keys: sbkeys.h made by app/build.gradle.kts as tools/sbkeys.py makes it (from the private keys file
# outside the repository; none without it), in ORIGIN_KEYS_DIR
target_include_directories(origin PRIVATE ${ORIGIN_KEYS_DIR})
target_link_libraries(origin PRIVATE SDL2 SDL2main tp_ass tp_hb tp_fb tp_ft tp_webp tp_misc libcurl_static GLESv2 EGL android log z m)
