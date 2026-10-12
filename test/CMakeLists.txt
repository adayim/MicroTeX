# The tests reach the engine's internals, so they link the static library
# (the top-level CMakeLists.txt builds it static when MICROTEX_TESTS or
# MICROTEX_FUZZ is on). test/run.sh builds and runs everything here.

if (MICROTEX_TESTS)
    add_executable(front_tests main.cpp test_lexer.cpp test_expander.cpp test_parser.cpp test_tblr.cpp test_siunitx.cpp test_mhchem.cpp test_engine.cpp test_equivalence.cpp test_packages.cpp test_diagnostics.cpp test_labels.cpp test_lists.cpp test_document.cpp test_math.cpp test_document_mode.cpp test_tables.cpp test_split.cpp test_diagrams.cpp test_text.cpp test_definitions.cpp test_runs.cpp)
    target_link_libraries(front_tests PRIVATE microtex)
    target_compile_definitions(front_tests PRIVATE MICROTEX_TEX_DIR="${CMAKE_SOURCE_DIR}/res/tex"
        MICROTEX_TEST_OTF="${CMAKE_SOURCE_DIR}/res/firamath/FiraMath-Regular.otf"
        MICROTEX_TEST_FIXTURES="${CMAKE_SOURCE_DIR}/test/fixtures")
    add_test(NAME front COMMAND front_tests)
endif ()

# libFuzzer targets: clang only, with the library built with
# -fsanitize=fuzzer-no-link (test/run.sh fuzz does both).
if (MICROTEX_FUZZ)
    foreach (target front_fuzz engine_fuzz)
        add_executable(${target} fuzz/${target}.cpp)
        target_link_libraries(${target} PRIVATE microtex)
        target_compile_definitions(${target} PRIVATE MICROTEX_TEX_DIR="${CMAKE_SOURCE_DIR}/res/tex")
        target_link_options(${target} PRIVATE -fsanitize=fuzzer)
    endforeach ()
endif ()
