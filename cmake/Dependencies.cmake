# Third-party dependencies.
#
# Installed on the system (MSYS2 UCRT64 on Windows, the package manager
# elsewhere; see README):
#   Qt 6        - user interface (Widgets), file dialogs, fonts, input; Network only for what
#                 the user asks to download (official plugins, the speech model)
#   FFmpeg      - libavformat / libavcodec / libavfilter / libswscale / libswresample:
#                 reading video and audio, writing MP4 / MOV / WebM / GIF
#   HarfBuzz, ICU, libpng, libjpeg, libwebp, expat, zlib - used by Skia
#   whisper.cpp - speech to text (optional: MSYS2's FFmpeg already depends on it, Homebrew has
#                 "whisper.cpp"; where no package has it, -DBEMOTION_FETCH_WHISPER=ON builds
#                 the same pinned version with bemotion; without it transcription is not offered)
#
# Built once with tools/skia/build-skia.sh (pinned revision):
#   Skia        - 2D rendering of compositions, text shaping, image filters
#
# Fetched at configure time and pinned to exact versions:
#   Qt Advanced Docking System - dockable, auto-hiding panels
#   nlohmann    - JSON for the project file format
#   stb         - image import and PNG writing
#   Lua         - plugin scripting
#   miniaudio   - audio output

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Qt ------------------------------------------------------------------------
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Svg Network Test)
qt_standard_project_setup()

# --- pkg-config packages ---------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavfilter libswscale libswresample libavutil)
pkg_check_modules(SKIA_DEPS REQUIRED IMPORTED_TARGET
    harfbuzz icu-uc icu-i18n libpng libjpeg libwebp libwebpmux libwebpdemux expat zlib)
pkg_check_modules(WHISPER IMPORTED_TARGET whisper)
option(BEMOTION_FETCH_WHISPER "Build whisper.cpp (speech to text) with bemotion when no package provides it" OFF)
if(WHISPER_FOUND)
    set(BEMOTION_WHISPER_TARGET PkgConfig::WHISPER)
    message(STATUS "whisper.cpp ${WHISPER_VERSION}: speech to text on")
elseif(BEMOTION_FETCH_WHISPER)
    # Linked in, so a package carries it with nothing to install. The CPU code is built for any
    # x86-64 from 2013 on (AVX2), not just the build machine; Metal on macOS; Vulkan on request
    # (-DGGML_VULKAN=ON, needs the Vulkan SDK).
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        foreach(flag GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C)
            set(${flag} ON CACHE BOOL "" FORCE)
        endforeach()
    endif()
    foreach(flag WHISPER_BUILD_TESTS WHISPER_BUILD_EXAMPLES WHISPER_BUILD_SERVER WHISPER_SDL2 WHISPER_CURL)
        set(${flag} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(bemotion_shared_libs ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_Declare(whisper
        URL      https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.9.5.tar.gz
        URL_HASH SHA256=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(whisper)
    set(BUILD_SHARED_LIBS ${bemotion_shared_libs})
    set(WHISPER_FOUND TRUE)
    set(BEMOTION_WHISPER_TARGET whisper)
    message(STATUS "whisper.cpp 1.9.5 (built with bemotion): speech to text on")
else()
    message(STATUS "whisper.cpp not found: speech to text off (see -DBEMOTION_FETCH_WHISPER)")
endif()
if(UNIX AND NOT APPLE)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig freetype2)
endif()

# --- Skia (tools/skia/build-skia.sh) ----------------------------------------------
set(SKIA_ROOT "${PROJECT_SOURCE_DIR}/third_party/skia" CACHE PATH "Where tools/skia/build-skia.sh installed Skia")
if(NOT EXISTS "${SKIA_ROOT}/lib/libskia.a")
    message(FATAL_ERROR "Skia not found in ${SKIA_ROOT}. Build it with tools/skia/build-skia.sh "
                        "(or point SKIA_ROOT at an existing build).")
endif()
add_library(skia INTERFACE)
target_include_directories(skia SYSTEM INTERFACE "${SKIA_ROOT}/include")
# The defines Skia was built with that change what its headers declare.
target_compile_definitions(skia INTERFACE
    SK_RELEASE SK_GANESH SK_GL SK_ENABLE_SKSL
    SK_SHAPER_HARFBUZZ_AVAILABLE SK_SHAPER_UNICODE_AVAILABLE SK_UNICODE_AVAILABLE SK_UNICODE_ICU_IMPLEMENTATION
    SK_SHAPER_PRIMITIVE_AVAILABLE)
# Static libraries: the modules first, then Skia itself.
target_link_libraries(skia INTERFACE
    "${SKIA_ROOT}/lib/libskshaper.a"
    "${SKIA_ROOT}/lib/libskunicode_icu.a"
    "${SKIA_ROOT}/lib/libskunicode_core.a"
    "${SKIA_ROOT}/lib/libskia.a"
    PkgConfig::SKIA_DEPS)
if(WIN32)
    target_link_libraries(skia INTERFACE gdi32 user32 ole32 oleaut32 uuid usp10 fontsub dwrite opengl32)
elseif(APPLE)
    target_link_libraries(skia INTERFACE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation"
                                          "-framework AppKit")
else()
    target_link_libraries(skia INTERFACE PkgConfig::FONTCONFIG GL)
endif()

# --- Qt Advanced Docking System ----------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ADS_VERSION 5.1.1 CACHE STRING "" FORCE)
FetchContent_Declare(ads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        v5.1.1
    GIT_SHALLOW    TRUE)

# --- nlohmann/json -----------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# --- stb (image import, PNG writing) ------------------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)

# --- Lua (plugin scripting) ---------------------------------------------------
FetchContent_Declare(lua
    URL      https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# --- miniaudio (audio output) ------------------------------------------------------
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake) # only the header is needed: download without configuring its project

FetchContent_MakeAvailable(ads json stb lua miniaudio)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# Lua ships a Makefile only: build the library (no interpreter/compiler) here.
# It is compiled as C++ so script errors unwind with exceptions (C++ destructors
# run) instead of longjmp; include lua.h & co directly, not lua.hpp.
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(lua PROPERTIES LINKER_LANGUAGE CXX)
