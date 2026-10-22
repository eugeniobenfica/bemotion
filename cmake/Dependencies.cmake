# Third-party dependencies, fetched at configure time and pinned to exact versions.
#
#   GLFW        - window, input and OpenGL context
#   Dear ImGui  - immediate mode UI (docking branch)
#   nlohmann    - JSON for the project file format
#   stb         - PNG writing for image-sequence export

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- GLFW --------------------------------------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# --- nlohmann/json -----------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# --- Dear ImGui --------------------------------------------------------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.4-docking
    GIT_SHALLOW    TRUE)

# --- stb (image writing for PNG export) --------------------------------------
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)

# --- Lua (plugin scripting) ---------------------------------------------------
FetchContent_Declare(lua
    URL      https://www.lua.org/ftp/lua-5.4.7.tar.gz
    URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(glfw json imgui stb lua)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

# On X11 the GLFW backend calls Xlib directly (e.g. for IME/window hints).
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(imgui PUBLIC X11::X11)
endif()

# Lua ships a Makefile only: build the library (no interpreter/compiler) here.
# It is compiled as C++ so script errors unwind with exceptions (C++ destructors
# run) instead of longjmp; include lua.h & co directly, not lua.hpp.
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua SYSTEM PUBLIC ${lua_SOURCE_DIR}/src)
set_target_properties(lua PROPERTIES LINKER_LANGUAGE CXX)
