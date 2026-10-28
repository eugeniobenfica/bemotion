# --- bemotion_core: document model, no UI dependencies -------------------------
add_library(bemotion_core STATIC
    core/Easing.cpp
    core/Editing.cpp
    core/Effects.cpp
    core/Expressions.cpp
    core/Geometry.cpp
    core/History.cpp
    core/Animated.cpp
    core/Arrange.cpp
    core/AudioDsp.cpp
    core/Clipboard.cpp
    core/Data.cpp
    core/Deform.cpp
    core/Describe.cpp
    core/Ops.cpp
    core/Validate.cpp
    core/Language.cpp
    core/Layer.cpp
    core/Lottie.cpp
    core/MeshFile.cpp
    core/Presets.cpp
    core/Project.cpp
    core/Serialization.cpp
    core/Snapping.cpp
    core/Svg.cpp
    core/Tracking.cpp
    core/Transitions.cpp
    core/PluginCatalog.cpp)
target_include_directories(bemotion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bemotion_core PUBLIC nlohmann_json::nlohmann_json)

# The operations and expressions references, built into the program for AI assistants (app/McpServer.cpp).
file(READ ${PROJECT_SOURCE_DIR}/docs/OPERATIONS.md BEMOTION_OPS_REFERENCE)
file(READ ${PROJECT_SOURCE_DIR}/docs/EXPRESSIONS.md BEMOTION_EXPRESSIONS_REFERENCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/docs/OPERATIONS.md ${PROJECT_SOURCE_DIR}/docs/EXPRESSIONS.md)
configure_file(app/OperationsReference.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/OperationsReference.cpp @ONLY)

# --- bemotion: the desktop application -----------------------------------------
add_executable(bemotion WIN32
    main.cpp
    app/Application.cpp
    app/AppIcon.cpp
    app/Automation.cpp
    app/McpServer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/OperationsReference.cpp
    app/DevScript.cpp
    app/Downloads.cpp
    app/EffectLibrary.cpp
    app/Platform.cpp
    app/PluginHost.cpp
    app/ProjectPackage.cpp
    app/PresetLibrary.cpp
    app/Recovery.cpp
    app/Settings.cpp
    app/RecentProjects.cpp
    export/Export.cpp
    export/FfmpegEncoder.cpp
    export/PngSequenceEncoder.cpp
    media/AudioEngine.cpp
    media/FfmpegMedia.cpp
    media/MediaIO.cpp
    media/MediaManager.cpp
    media/Recorder.cpp
    media/Transcriber.cpp
    render/Fonts.cpp
    render/Gradient.cpp
    render/Gpu.cpp
    render/LogoMark.cpp
    render/Mesh3D.cpp
    render/OffscreenRenderer.cpp
    render/PluginCanvas.cpp
    render/SceneRenderer.cpp
    render/Text.cpp
    ui/AppSignals.h
    ui/SkiaQt.h
    ui/WindowChrome.cpp
    ui/EditorContext.cpp
    ui/EditorWindow.cpp
    ui/ExportDialog.cpp
    ui/GpuContext.cpp
    ui/HelpWindow.cpp
    ui/HomeScreen.cpp
    ui/IntroScreen.cpp
    ui/Keymap.cpp
    ui/Language.cpp
    ui/LayerMenus.cpp
    ui/PagedWindow.cpp
    ui/PropertyRow.cpp
    ui/ExpressionEditor.cpp
    ui/ExpressionLibrary.cpp
    ui/PreviewWindow.cpp
    ui/ColorPicker.cpp
    ui/CurvesEditor.cpp
    ui/ExperienceScreen.cpp
    ui/SettingsWindow.cpp
    ui/Tools.cpp
    ui/Widgets.cpp
    ui/panels/EffectsPanel.cpp
    ui/panels/MediaPanel.cpp
    ui/panels/ObjectsPanel.cpp
    ui/panels/ScopesPanel.cpp
    ui/panels/PropertiesPanel.cpp
    ui/panels/TimelineGraph.cpp
    ui/panels/TimelinePanel.cpp
    ui/panels/ToolsPanel.cpp
    ui/panels/AnimationsPanel.cpp
    ui/panels/ColorPanel.cpp
    ui/Tracking.cpp
    ui/Transcribe.cpp
    ui/panels/ViewportPanel.cpp
    ui/panels/ViewportTools.cpp
    ui/panels/ToolSettingsPanel.cpp)
set_target_properties(bemotion PROPERTIES AUTOMOC ON)
target_include_directories(bemotion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bemotion PRIVATE
    bemotion_core
    bestyle
    Qt6::Widgets Qt6::Svg Qt6::Network Qt6::Test
    qtadvanceddocking-qt6
    skia
    PkgConfig::FFMPEG
    stb lua miniaudio)
if(WHISPER_FOUND)
    target_link_libraries(bemotion PRIVATE ${BEMOTION_WHISPER_TARGET})
    target_compile_definitions(bemotion PRIVATE BEMOTION_WHISPER=1)
endif()
target_compile_definitions(bemotion PRIVATE
    BEMOTION_VERSION="${PROJECT_VERSION}"
    BEMOTION_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

if(UNIX AND NOT APPLE)
    target_link_libraries(bemotion PRIVATE ${CMAKE_DL_LIBS} pthread m) # miniaudio loads audio backends at runtime
endif()

if(WIN32)
    # The executable's icon. Its own target: windres chokes on include paths with spaces.
    enable_language(RC)
    add_library(bemotion_resources OBJECT app/bemotion.rc)
    target_sources(bemotion PRIVATE $<TARGET_OBJECTS:bemotion_resources>)
    target_link_libraries(bemotion PRIVATE shell32 ole32 uuid winmm)
endif()

if(MSVC)
    target_compile_options(bemotion PRIVATE /W4 /utf-8)
    target_compile_options(bemotion_core PRIVATE /W4 /utf-8)
    # Portable C functions (fopen, getenv, ...) are fine here.
    target_compile_definitions(bemotion PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(bemotion PRIVATE /ENTRY:mainCRTStartup)
else()
    target_compile_options(bemotion PRIVATE -Wall -Wextra -Wno-attributes) # Skia headers use clang:: attributes
    target_compile_options(bemotion_core PRIVATE -Wall -Wextra)
endif()

# Copy runtime assets (fonts, bundled plugins, ...) next to the executable on
# every build, so edited assets show up even when nothing needs relinking.
add_custom_target(bemotion_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:bemotion>/assets)
add_dependencies(bemotion bemotion_assets)

install(TARGETS bemotion RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets DESTINATION .)

# Developer option: keep a console attached for logs and assertion messages.
option(BEMOTION_CONSOLE "Build bemotion with a console window" OFF)
if(BEMOTION_CONSOLE)
    set_target_properties(bemotion PROPERTIES WIN32_EXECUTABLE OFF)
endif()
