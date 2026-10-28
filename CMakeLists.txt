# bestyle: bemotion's look (theme, widget style, icons, controls) for any Qt 6 Widgets app.
# Needs Qt6::Widgets and nlohmann_json (themes are JSON); bring them in, then:
#
#     add_subdirectory(libs/bestyle)
#     target_link_libraries(my_app PRIVATE bestyle)

add_library(bestyle STATIC
    include/bestyle/BeStyle.h
    include/bestyle/Draw.h
    include/bestyle/Icons.h
    include/bestyle/Metrics.h
    include/bestyle/Style.h
    include/bestyle/Theme.h
    include/bestyle/Widgets.h
    src/BeStyle.cpp
    src/Draw.cpp
    src/Icons.cpp
    src/Style.cpp
    src/Theme.cpp
    src/Widgets.cpp)
set_target_properties(bestyle PROPERTIES AUTOMOC ON)
target_include_directories(bestyle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bestyle PUBLIC Qt6::Widgets PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(bestyle PUBLIC cxx_std_20)

option(BESTYLE_BUILD_EXAMPLES "Build bestyle's gallery example" OFF)
if(BESTYLE_BUILD_EXAMPLES)
    add_executable(bestyle_gallery WIN32 examples/gallery.cpp)
    target_link_libraries(bestyle_gallery PRIVATE bestyle)
endif()
