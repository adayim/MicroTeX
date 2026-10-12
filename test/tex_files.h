// The engine's own commands are LaTeX files (res/tex: prelude.tex and the
// .sty packages), and the engine reads no file: a host hands over their text.
// This is the host for the tests and the fuzzers, as gridmicrotex's .onLoad
// is for R.

#ifndef MICROTEX_TEST_TEX_FILES_H
#define MICROTEX_TEST_TEX_FILES_H

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "front/packages.h"

#ifndef MICROTEX_TEX_DIR
#error "MICROTEX_TEX_DIR must name the directory holding prelude.tex and the .sty files"
#endif

namespace check {

inline std::string readTexFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

inline void registerTexFiles() {
  namespace fs = std::filesystem;
  const fs::path dir(MICROTEX_TEX_DIR);
  microtex::front::setPrelude(readTexFile(dir / "prelude.tex"));
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".sty") {
      microtex::front::registerPackage(entry.path().stem().string(), readTexFile(entry.path()));
    }
  }
}

}  // namespace check

#endif
