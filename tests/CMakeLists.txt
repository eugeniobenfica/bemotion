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
    # Every operation is used by a test (tools/ops-tested.py).
    add_test(NAME operations_tested COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/ops-tested.py)
    # What the renderer draws, against tests/golden (tools/golden.py --update draws them again).
    add_test(NAME golden_images COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/golden.py $<TARGET_FILE:bemotion>
             --out ${CMAKE_CURRENT_BINARY_DIR}/golden)
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
