add_executable(microtex-svg svg_main.cpp)

# The recorder and the package hooks are internal headers, which a shared
# library hides: this links the static library (the root CMakeLists asks for it).
target_link_libraries(microtex-svg PRIVATE microtex)

if (MICROTEX_TESTS)
    add_test(
        NAME svg_example
        COMMAND microtex-svg --res ${CMAKE_BINARY_DIR}/res "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}"
                ${CMAKE_BINARY_DIR}/svg_example.svg
    )
endif ()
