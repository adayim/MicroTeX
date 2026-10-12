cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(
    microtex
    VERSION 1.0.0
    DESCRIPTION "A dynamic, cross-platform, and embeddable LaTeX rendering library"
)

# the API (and ABI) is compatible when MICROTEX_API_VERSION is the same
set(MICROTEX_API_VERSION 1)

if (MSVC) # MSVC Compat
    add_compile_options("/utf-8")
    # make binaries together
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif ()

####################
#  compile option  #
####################
# The engine needs FreeType; FriBidi is optional (-DMICROTEX_BIDI=ON, lib/).
option(HAVE_LOG "If enable log" ON)
if (HAVE_LOG)
    add_definitions(-DHAVE_LOG)
endif ()
option(BUILD_STATIC "If build for static library" OFF)
option(HAVE_CWRAPPER "Build the C API (lib/wrapper)" OFF)

# Examples
option(BUILD_EXAMPLE_SVG "Build microtex-svg, LaTeX to SVG (needs nothing but the engine)" OFF)
option(BUILD_EXAMPLE_MEMCHECK "Build the memory check example" OFF)
option(BUILD_EXAMPLE_WIN32 "Build the Win32 GDI+ example, only work with -DGDI=ON flag" OFF)

# Graphics backends, inherited from upstream MicroTeX; the engine does not need one
option(CAIRO "Build with CAIRO" OFF)
option(QT "Build with Qt" OFF)
option(SKIA "Build with Skia" OFF)
option(GDI "Build with GDI+" OFF)

# Tests
option(MICROTEX_TESTS "Build the tests in test/ (run them with ctest)" OFF)
option(MICROTEX_FUZZ "Build the libFuzzer targets in test/fuzz/ (clang only)" OFF)

# The tests and the examples reach the engine's internals, which a shared
# library hides.
if (BUILD_STATIC OR MICROTEX_TESTS OR MICROTEX_FUZZ OR BUILD_EXAMPLE_SVG OR BUILD_EXAMPLE_MEMCHECK)
    set(_BUILD_STATIC ON)
endif ()

include(MicroTeXInstall.cmake)

add_subdirectory(lib)

if (MICROTEX_TESTS OR MICROTEX_FUZZ)
    enable_testing()
    add_subdirectory(test)
endif ()

file(COPY res DESTINATION .)

#####################
# optional features #
#####################
if (BUILD_EXAMPLE_SVG)
    add_subdirectory(example/svg)
endif ()

if (CAIRO)
    add_subdirectory(platform/cairo)
endif ()

if (QT)
    add_subdirectory(platform/qt)
endif ()

if (SKIA)
    add_subdirectory(platform/skia)
endif ()

if (GDI)
    add_subdirectory(platform/gdi_win)
    if (BUILD_EXAMPLE_WIN32)
        add_subdirectory(example/win32)
    endif ()
endif ()

if (BUILD_EXAMPLE_MEMCHECK)
    add_subdirectory(example/memcheck)
endif ()

################
# installation #
################

# install resources: the fonts, and the LaTeX files the engine's commands are
# written in (res/tex), which the host hands to the engine
install(DIRECTORY res/ DESTINATION share/microtex)
