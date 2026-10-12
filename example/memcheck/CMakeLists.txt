add_executable(
    microtex-memcheck
    ../samples/samples.cpp
    mem_check_main.cpp
)

target_include_directories(microtex-memcheck PUBLIC ../samples)

target_link_libraries(
    microtex-memcheck PRIVATE
    microtex
)

# Every sample of res/SAMPLES.tex parses and draws (under the sanitizers when
# the tests are built with them).
if (MICROTEX_TESTS)
    add_test(
        NAME memcheck_samples
        COMMAND microtex-memcheck ${CMAKE_BINARY_DIR}/res ${CMAKE_SOURCE_DIR}/res/SAMPLES.tex
    )
endif ()
