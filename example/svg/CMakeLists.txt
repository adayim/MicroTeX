add_executable(microtex-svg svg_main.cpp ../samples/samples.cpp)

target_include_directories(microtex-svg PRIVATE ../samples)

# The recorder and the package hooks are internal headers, which a shared
# library hides: this links the static library (the root CMakeLists asks for it).
# FreeType reads the text fonts.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE2 REQUIRED freetype2)
target_include_directories(microtex-svg PRIVATE ${FREETYPE2_INCLUDE_DIRS})
target_link_libraries(microtex-svg PRIVATE microtex ${FREETYPE2_LINK_LIBRARIES})

if (MICROTEX_TESTS)
    add_test(
        NAME svg_example
        COMMAND microtex-svg --res ${CMAKE_BINARY_DIR}/res "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix} \\text{text}"
                ${CMAKE_BINARY_DIR}/svg_example.svg
    )
    # Every sample of res/SAMPLES.tex is drawn, the emoji in colour.
    add_test(
        NAME svg_samples
        COMMAND microtex-svg --res ${CMAKE_BINARY_DIR}/res --width 720
                --fallback ${CMAKE_BINARY_DIR}/res/twemoji/Twemoji.Mozilla.ttf
                --samples ${CMAKE_SOURCE_DIR}/res/SAMPLES.tex --outdir ${CMAKE_BINARY_DIR}/samples-svg
    )
endif ()
