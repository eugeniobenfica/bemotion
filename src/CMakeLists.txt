# --- bemotion_core: document model, no UI dependencies -------------------------
add_library(bemotion_core STATIC
    core/Easing.cpp
    core/Effects.cpp
    core/Geometry.cpp
    core/History.cpp
    core/Layer.cpp
    core/Presets.cpp
    core/Project.cpp
    core/Serialization.cpp
    core/Snapping.cpp)
target_include_directories(bemotion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bemotion_core PUBLIC nlohmann_json::nlohmann_json)

# --- bemotion: the desktop application -----------------------------------------
add_executable(bemotion WIN32
    main.cpp
    app/Application.cpp
    app/AppIcon.cpp
    app/DevScript.cpp
    app/EffectLibrary.cpp
    app/Platform.cpp
    app/PresetLibrary.cpp
    app/RecentProjects.cpp
    export/Export.cpp
    export/FfmpegEncoder.cpp
    export/MediaFoundationEncoder.cpp
    export/PngSequenceEncoder.cpp
    render/EffectRenderer.cpp
    render/Fonts.cpp
    render/GL.cpp
    render/OffscreenRenderer.cpp
    render/SceneRenderer.cpp
    ui/EditorContext.cpp
    ui/EditorScreen.cpp
    ui/ExportDialog.cpp
    ui/HomeScreen.cpp
    ui/Theme.cpp
    ui/Widgets.cpp
    ui/panels/ObjectsPanel.cpp
    ui/panels/PropertiesPanel.cpp
    ui/panels/TimelinePanel.cpp
    ui/panels/ViewportPanel.cpp)
target_include_directories(bemotion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bemotion PRIVATE bemotion_core imgui stb)
target_compile_definitions(bemotion PRIVATE
    BEMOTION_VERSION="${PROJECT_VERSION}"
    BEMOTION_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

if(WIN32)
    target_link_libraries(bemotion PRIVATE comdlg32 shell32 ole32 uuid winmm mfplat mfreadwrite mfuuid)
endif()

if(MSVC)
    target_compile_options(bemotion PRIVATE /W4 /utf-8)
    target_compile_options(bemotion_core PRIVATE /W4 /utf-8)
    # Portable C functions (fopen, getenv, ...) are fine here.
    target_compile_definitions(bemotion PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(bemotion PRIVATE /ENTRY:mainCRTStartup)
else()
    target_compile_options(bemotion PRIVATE -Wall -Wextra)
    target_compile_options(bemotion_core PRIVATE -Wall -Wextra)
endif()

if(MINGW)
    # Ship a self-contained executable (no libstdc++/libgcc DLLs needed).
    target_link_options(bemotion PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Copy runtime assets (fonts, ...) next to the executable.
add_custom_command(TARGET bemotion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:bemotion>/assets)

install(TARGETS bemotion RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets DESTINATION .)

# Developer option: keep a console attached for logs and assertion messages.
option(BEMOTION_CONSOLE "Build bemotion with a console window" OFF)
if(BEMOTION_CONSOLE)
    set_target_properties(bemotion PROPERTIES WIN32_EXECUTABLE OFF)
endif()
