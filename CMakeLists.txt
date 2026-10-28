# bmstyle: bemotion's look (theme, widget style, icons, controls) for any Qt 6 Widgets app.
# Needs Qt6::Widgets and nlohmann_json (themes are JSON); bring them in, then:
#
#     add_subdirectory(libs/bmstyle)
#     target_link_libraries(my_app PRIVATE bmstyle)

add_library(bmstyle STATIC
    include/bmstyle/BmStyle.h
    include/bmstyle/Draw.h
    include/bmstyle/Icons.h
    include/bmstyle/Metrics.h
    include/bmstyle/Style.h
    include/bmstyle/Theme.h
    include/bmstyle/Widgets.h
    src/BmStyle.cpp
    src/Draw.cpp
    src/Icons.cpp
    src/Style.cpp
    src/Theme.cpp
    src/Widgets.cpp)
set_target_properties(bmstyle PROPERTIES AUTOMOC ON)
target_include_directories(bmstyle PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bmstyle PUBLIC Qt6::Widgets PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(bmstyle PUBLIC cxx_std_20)

option(BMSTYLE_BUILD_EXAMPLES "Build bmstyle's gallery example" OFF)
if(BMSTYLE_BUILD_EXAMPLES)
    add_executable(bmstyle_gallery WIN32 examples/gallery.cpp)
    target_link_libraries(bmstyle_gallery PRIVATE bmstyle)
endif()
