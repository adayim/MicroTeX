# ![logo](readme/tex_logo.svg)

It is a dynamic, cross-platform, and embeddable LaTeX rendering library. Its main purpose is to display mathematical formulas written in LaTeX, and it sets the text, tables, lists and displays around them too. The engine needs no graphics library, it records what it draws, and graphics backends for Cairo, Qt, Skia and Windows GDI+ are included for applications that want to draw directly.

This is a fork of [NanoMichael/MicroTeX](https://github.com/NanoMichael/MicroTeX) by Nano Michael, from its `openmath` branch. The layout engine, the symbol tables and the graphics interface are his work, and the credit for them is his. The fork is the engine of [gridmicrotex](https://github.com/adayim/gridmicrotex), which renders LaTeX in R graphics, and `lib/` is kept in step with the copy that package ships.

![integral](readme/example_integral.svg)

![matrix and cases](readme/example_matrix.svg)

![aligned](readme/example_aligned.svg)

![diagram and chemistry](readme/example_diagram.svg)

These were drawn by [`example/svg`](example/svg/svg_main.cpp), with Latin Modern Math.

# What this fork changes

- **A new LaTeX front end.** A lexer, macro expander, parser and lowering that read LaTeX as TeX does: catcodes and spaces, groups, `\def`, `\newcommand`, `\let`, `\newif`, conditionals, xparse's `\NewDocumentCommand`, `\usepackage`. Malformed input is drawn with a warning at its line and column instead of failing.
- **Commands as LaTeX files.** What is not written in C++ is LaTeX, in [`res/tex`](res/tex): `prelude.tex` and packages such as amsmath, amssymb, amsthm, mathtools, braket, physics, commath, tensor, slashed, booktabs, makecell and graphicx, and `katex` for the names only KaTeX has. The engine reads no file, the host hands over their text, so a host can add packages of its own. Built in: subsets of siunitx and mhchem, `tikz-cd` and amscd diagrams, tabularray tables, `bm` and `cancel`.
- **Three input modes** (`InputMode`): *math*, a formula; *mixed*, a label of text with `$math$` in it; *document*, a document body with paragraphs, headings, lists, tables, theorems, floats and equation numbers (`\label`, `\ref`, `subequations`, `numcases`).
- **Fonts straight from OpenType files.** A math font is read from its `.otf` with FreeType and its MATH table, outlines included. There are no CLM files and no FontForge. Text fonts can be added and named in the document with `\setmainfont` and friends. Fira Math, Latin Modern Math, TeX Gyre DejaVu Math and XITS are in [`res`](res).
- **A recording backend.** `Graphics2D_Recorder` ([`lib/graphic/graphic_recorder.h`](lib/graphic/graphic_recorder.h)) keeps every draw operation as data: glyph outlines or glyph ids, lines, rectangles, text runs, named marks and image boxes. Any vector output can be made from it; `example/svg` is one.
- **Hooks for the host**, for images (`\includegraphics`) and fonts named in a document ([`lib/front/hooks.h`](lib/front/hooks.h)).
- **Right-to-left text**, reordered with FriBidi when it is built in.
- **TeX's own layout rules** where TeX has one, checked against pdfLaTeX output: the row spacing of matrices and alignments, text style in arrays, equation numbers.
- **Safer and more portable.** The engine is tested under AddressSanitizer and UBSan and fuzzed with libFuzzer on every push.

# Status

| Part | State |
|------|-------|
| `lib/` (the engine) | Built and tested by CI with g++ and clang++: the tests under AddressSanitizer and UBSan, the strict warnings, and both fuzzers. |
| `example/svg` | Builds and runs; made the pictures above. |
| `lib/wrapper` (the C API) | Builds with `-DHAVE_CWRAPPER=ON`; a test of it runs in CI. |
| `example/memcheck` | Builds and runs. |
| `platform/gdi_win`, `example/win32` | Compile-checked with MinGW, not run. |
| `platform/cairo`, `platform/qt`, `platform/skia` | Inherited from upstream and written against its interface, which has only grown. Not built here. |

Removed, because they no longer built: the Flutter and WebAssembly ports and the GTK, gtkmm and Qt demo programs (they loaded fonts as CLM data, a format the engine no longer reads), the GTK widget (it uses the old `tex` namespace), `build.zig` (its list of sources was out of date), and the CLM tools and data.

# Build

You need a C++17 compiler, CMake 3.16 and FreeType (found by `pkg-config`). FriBidi is optional.

```sh
cmake -S . -B build -DBUILD_EXAMPLE_SVG=ON
cmake --build build -j
./build/example/svg/microtex-svg --display '\int_0^1 \frac{x^2}{2}\,dx' out.svg
```

`microtex-svg` takes `--res`, `--font`, `--size`, `--width`, `--mode math|mixed|document`, `--display` and `--background`; run it with no arguments for the list. It finds the fonts and the LaTeX files in `./res`, which CMake copies next to it.

| Option | Default | |
|--------|---------|---|
| `MICROTEX_BIDI` | OFF | Right-to-left reordering with FriBidi. |
| `BUILD_STATIC` | OFF | A static library. The examples and the tests link it, because the recorder and the package hooks are internal headers that a shared library hides. |
| `HAVE_CWRAPPER` | OFF | The C API in `lib/wrapper`. |
| `HAVE_LOG` | ON | Print the parse result and the box tree while running. |
| `GLYPH_RENDER_TYPE` | 0 | 0 glyph outlines and typefaces, 1 outlines only, 2 typefaces only. |
| `BUILD_EXAMPLE_SVG`, `BUILD_EXAMPLE_MEMCHECK` | OFF | The examples. `memcheck` parses a samples file such as [`res/SAMPLES.tex`](res/SAMPLES.tex) on a graphics context that draws nothing, for valgrind. |
| `CAIRO`, `QT`, `SKIA`, `GDI`, `BUILD_EXAMPLE_WIN32` | OFF | The graphics backends and the Windows example. |
| `MICROTEX_TESTS`, `MICROTEX_FUZZ` | OFF | The tests and the fuzzers ([`test/`](test/README.md)). |

[`lib/`](lib/meson.build) and the Cairo, Qt and GDI+ backends also have Meson manifests; the CMake build is the one that is tested.

# How to use

A host does four things before it can draw:

1. **Names a platform**, a `PlatformFactory` that creates fonts and text layouts (`example/svg` has a small one).
2. **Defines two measurements** the recorder asks for, `microtex::measure_cached_text_width()` and `microtex::measure_glyph_advance()` (declared at the end of `graphic_recorder.h`). A flipped draw is all that needs them, so an estimate will do. A shared library leaves them undefined, and the program that links it must define them.
3. **Hands over the LaTeX files** of `res/tex`.
4. **Starts the engine** with a math font file.

```c++
#include "front/packages.h"
#include "graphic/graphic_recorder.h"
#include "microtex.h"

using namespace microtex;

PlatformFactory::registerFactory("mine", std::make_unique<MyPlatform>());
PlatformFactory::activate("mine");

front::setPrelude(prelude_tex_text);                 // res/tex/prelude.tex
front::registerPackage("amsmath", amsmath_sty_text); // each res/tex/*.sty, by name

MicroTeX::init(FontSrcOtf("res/lm-math/latinmodern-math.otf"));

std::unique_ptr<Render> render(MicroTeX::parse(
    R"(\int_0^1 \frac{x^2}{2}\,dx)",  // LaTeX, in UTF-8
    0,                                // wrap lines at this width, 0 for no limit
    20,                               // text size, in pixels
    20 / 3.f,                         // space between lines, in pixels
    0xFF000000u));                    // colour, as ARGB

Graphics2D_Recorder g2;
render->draw(g2, 0, 0);
for (const DrawRecord& d : g2.records()) {
  // d.type is PATH, LINE, RECT, FILL_RECT, TEXT, MARK, IMAGE ...
}

MicroTeX::release();
```

`parse` takes more arguments, among them the `InputMode` and the style (`OverrideTeXStyle`, to set a formula in display style). Problems found while reading the input are in `front::lastDiagnostics()`, each with its line and column.

To draw without the recorder, implement `Graphics2D`, `Font` and `TextLayout` of [`lib/graphic/graphic.h`](lib/graphic/graphic.h), as the backends in `platform/` do.

## The C API

With `-DHAVE_CWRAPPER=ON` the engine has a C interface for other languages ([`lib/wrapper/cwrapper.h`](lib/wrapper/cwrapper.h)): `microtex_init(font file)`, `microtex_setPrelude()` and `microtex_registerPackage()` for the LaTeX files, `microtex_parseRender()` and `microtex_getDrawingData()`, which returns the draw commands as a byte stream. Text layouts are done by callbacks of the host.

## Fonts

`MicroTeX::init()` takes one math font, a `.otf` with a MATH table; `MicroTeX::addFont()` adds more, math or text. The fonts in `res` keep their own licences, which sit in their folders.

# Credits

MicroTeX is by [Nano Michael](https://github.com/NanoMichael) and its contributors. The changes in this fork are by [Alim Dayim](https://github.com/adayim).

# License

This project is under the [MIT](https://opensource.org/licenses/MIT) license, Copyright (c) 2020 Nano Michael; see [LICENSE](LICENSE). The fonts under `res` are under their own licenses.
