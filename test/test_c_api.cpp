// The C API (lib/wrapper, -DHAVE_CWRAPPER=ON), end to end: a font from a file,
// the LaTeX files handed over, a formula parsed, its size and its drawing data.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "wrapper/callback.h"
#include "wrapper/cwrapper.h"

#ifndef MICROTEX_TEX_DIR
#error "MICROTEX_TEX_DIR must name the directory holding prelude.tex and the .sty files"
#endif
#ifndef MICROTEX_TEST_OTF
#error "MICROTEX_TEST_OTF must name the math font file"
#endif

// The host's two measurements (graphic/graphic_recorder.h) are not defined here:
// the library's own estimates (lib/host_defaults.cpp) answer.

namespace {

int failures = 0;

#define CHECK(cond)                                              \
  do {                                                           \
    if (!(cond)) {                                               \
      std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      failures++;                                                \
    }                                                            \
  } while (0)

unsigned int createTextLayout(const char*, FontDesc*) { return 1; }
void textLayoutBounds(unsigned int, TextLayoutBounds* b) { microtex_setTextLayoutBounds(b, 10, 10, 8); }
void releaseTextLayout(unsigned int) {}
bool pathExists(unsigned int) { return false; }

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

int widthOf(const char* tex) {
  RenderPtr render = microtex_parseRender(tex, 0, 20, 6, 0xFF000000u, true, false, 0);
  const int width = render == nullptr ? -1 : microtex_getRenderWidth(render);
  if (render != nullptr) microtex_deleteRender(render);
  return width;
}

}  // namespace

int main() {
  microtex_registerCallbacks(createTextLayout, textLayoutBounds, releaseTextLayout, pathExists);
  FontMetaPtr meta = microtex_init(MICROTEX_TEST_OTF);
  CHECK(meta != nullptr);
  CHECK(microtex_isInited());
  if (meta != nullptr) {
    CHECK(microtex_isMathFont(meta));
    CHECK(std::strlen(microtex_getFontFamily(meta)) > 0);
    microtex_releaseFontMeta(meta);
  }

  // The LaTeX files: pmatrix is the format's, matrix is the engine's own.
  namespace fs = std::filesystem;
  const fs::path dir(MICROTEX_TEX_DIR);
  microtex_setPrelude(readFile(dir / "prelude.tex").c_str());
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".sty") {
      microtex_registerPackage(entry.path().stem().string().c_str(), readFile(entry.path()).c_str());
    }
  }
  const int matrix = widthOf("\\begin{matrix}a\\end{matrix}");
  const int pmatrix = widthOf("\\begin{pmatrix}a\\end{pmatrix}");
  CHECK(matrix > 0);
  CHECK(pmatrix > matrix);

  // A formula draws: the drawing data begin with their own length.
  RenderPtr render = microtex_parseRender("x^2 + \\frac{a}{b}", 0, 20, 6, 0xFF000000u, true, false, 0);
  CHECK(render != nullptr);
  if (render != nullptr) {
    CHECK(microtex_getRenderWidth(render) > 0);
    CHECK(microtex_getRenderHeight(render) > 0);
    DrawingData data = microtex_getDrawingData(render, 0, 0);
    CHECK(data != nullptr);
    if (data != nullptr) {
      unsigned int bytes = 0;
      std::memcpy(&bytes, data, sizeof bytes);
      CHECK(bytes > 4);
      microtex_freeDrawingData(data);
    }
    microtex_deleteRender(render);
  }

  microtex_release();
  std::printf("%s\n", failures == 0 ? "c api: ok" : "c api: FAILED");
  return failures == 0 ? 0 : 1;
}
