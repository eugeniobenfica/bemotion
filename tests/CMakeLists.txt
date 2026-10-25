add_executable(bemotion_core_tests core_tests.cpp)
target_link_libraries(bemotion_core_tests PRIVATE bemotion_core)
target_compile_definitions(bemotion_core_tests PRIVATE BEMOTION_SOURCE_DIR="${PROJECT_SOURCE_DIR}") # the docs it checks
add_test(NAME core_tests COMMAND bemotion_core_tests)

# The official plugin catalog matches the plugin folders (tools/plugin-index.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME plugin_catalog COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/plugin-index.py --check)
endif()

add_executable(bemotion_recording_tests recording_tests.cpp)
target_include_directories(bemotion_recording_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME recording_tests COMMAND bemotion_recording_tests)
