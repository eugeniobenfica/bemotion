add_executable(bemotion_core_tests core_tests.cpp)
target_link_libraries(bemotion_core_tests PRIVATE bemotion_core)
target_compile_definitions(bemotion_core_tests PRIVATE BEMOTION_SOURCE_DIR="${PROJECT_SOURCE_DIR}") # the docs it checks
add_test(NAME core_tests COMMAND bemotion_core_tests)

# The official plugin catalog matches the plugin folders (tools/plugin-index.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME plugin_catalog COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/plugin-index.py --check)
    # Every language file has every interface text, with the same placeholders (tools/languages.py).
    add_test(NAME languages COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/languages.py --check)
endif()

add_executable(bemotion_recording_tests recording_tests.cpp)
target_include_directories(bemotion_recording_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME recording_tests COMMAND bemotion_recording_tests)

# The layers (docs/ARCHITECTURE.md §2): each links whole, with only the layers below it,
# so a call into a layer above (an upward include) is an undefined symbol and fails the build.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    foreach(layer render media ui)
        add_executable(bemotion_layer_${layer} layer_check.cpp)
        target_link_libraries(bemotion_layer_${layer} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,bemotion_${layer}>")
    endforeach()
endif()
