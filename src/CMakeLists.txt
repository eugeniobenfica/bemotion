# --- mirage_core: document model, no UI dependencies -------------------------
add_library(mirage_core STATIC
    core/Easing.cpp
    core/Geometry.cpp
    core/History.cpp
    core/Layer.cpp
    core/Presets.cpp
    core/Project.cpp
    core/Serialization.cpp)
target_include_directories(mirage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mirage_core PUBLIC nlohmann_json::nlohmann_json)

# --- Mirage: the desktop application -----------------------------------------
add_executable(Mirage WIN32
    main.cpp
    app/Application.cpp
    app/AppIcon.cpp
    app/Platform.cpp
    app/RecentProjects.cpp
    render/Fonts.cpp
    render/SceneRenderer.cpp
    ui/EditorContext.cpp
    ui/EditorScreen.cpp
    ui/HomeScreen.cpp
    ui/Theme.cpp
    ui/Widgets.cpp
    ui/panels/ObjectsPanel.cpp
    ui/panels/PropertiesPanel.cpp
    ui/panels/TimelinePanel.cpp
    ui/panels/ViewportPanel.cpp)
target_include_directories(Mirage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Mirage PRIVATE mirage_core imgui)
target_compile_definitions(Mirage PRIVATE
    MIRAGE_VERSION="${PROJECT_VERSION}"
    MIRAGE_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

if(WIN32)
    target_link_libraries(Mirage PRIVATE comdlg32 shell32 ole32 uuid winmm)
endif()

if(MSVC)
    target_compile_options(Mirage PRIVATE /W4 /utf-8)
    target_compile_options(mirage_core PRIVATE /W4 /utf-8)
    target_link_options(Mirage PRIVATE /ENTRY:mainCRTStartup)
else()
    target_compile_options(Mirage PRIVATE -Wall -Wextra)
    target_compile_options(mirage_core PRIVATE -Wall -Wextra)
endif()

if(MINGW)
    # Ship a self-contained executable (no libstdc++/libgcc DLLs needed).
    target_link_options(Mirage PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# Copy runtime assets (fonts, ...) next to the executable.
add_custom_command(TARGET Mirage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Mirage>/assets)

install(TARGETS Mirage RUNTIME DESTINATION .)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets DESTINATION .)

# Developer option: keep a console attached for logs and assertion messages.
option(MIRAGE_CONSOLE "Build Mirage with a console window" OFF)
if(MIRAGE_CONSOLE)
    set_target_properties(Mirage PROPERTIES WIN32_EXECUTABLE OFF)
endif()
