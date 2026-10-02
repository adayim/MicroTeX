# The tests reach the engine's internals, so they link the static library
# (the top-level CMakeLists.txt builds it static when MICROTEX_TESTS or
# MICROTEX_FUZZ is on). test/run.sh builds and runs everything here.

if (MICROTEX_TESTS)
    add_executable(front_tests main.cpp test_lexer.cpp test_expander.cpp test_parser.cpp test_tblr.cpp test_siunitx.cpp)
    target_link_libraries(front_tests PRIVATE microtex)
    add_test(NAME front COMMAND front_tests)
endif ()

# libFuzzer targets: clang only, with the library built with
# -fsanitize=fuzzer-no-link (test/run.sh fuzz does both).
if (MICROTEX_FUZZ)
    foreach (target front_fuzz engine_fuzz)
        add_executable(${target} fuzz/${target}.cpp)
        target_link_libraries(${target} PRIVATE microtex)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    endforeach ()
endif ()
