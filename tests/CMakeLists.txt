add_executable(bemotion_core_tests core_tests.cpp animation_tests.cpp timeline_tests.cpp operations_tests.cpp
    expression_tests.cpp layer_tests.cpp file_tests.cpp)
target_link_libraries(bemotion_core_tests PRIVATE bemotion_core)
target_compile_definitions(bemotion_core_tests PRIVATE BEMOTION_SOURCE_DIR="${PROJECT_SOURCE_DIR}") # the docs and files it checks
add_test(NAME core_tests COMMAND bemotion_core_tests)

# Mutated inputs for the core's readers (tests/corpus): a few seconds here, longer in CI.
add_executable(bemotion_fuzz fuzz.cpp)
target_link_libraries(bemotion_fuzz PRIVATE bemotion_core)
target_compile_definitions(bemotion_fuzz PRIVATE BEMOTION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME fuzz COMMAND bemotion_fuzz --seconds 5)

# The official plugin catalog matches the plugin folders (tools/plugin-index.py).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME plugin_catalog COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/plugin-index.py --check)
    # Every language file has every interface text, with the same placeholders (tools/languages.py).
    add_test(NAME languages COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/languages.py --check)
    # What the renderer draws, against tests/golden (tools/golden.py --update draws them again).
    add_test(NAME golden_images COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/golden.py $<TARGET_FILE:bemotion>
             --out ${CMAKE_CURRENT_BINARY_DIR}/golden)
endif()

add_executable(bemotion_recording_tests recording_tests.cpp)
target_include_directories(bemotion_recording_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME recording_tests COMMAND bemotion_recording_tests)
