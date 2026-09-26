// Runs every TEST linked into the executable; exits 1 if any check failed.
// `front_tests NAME...` runs only the tests whose names contain one of NAMEs.

#include <exception>
#include <iostream>
#include <string>

#include "check.h"
#include "utils/types.h"

// The recorder leaves two measurements to the embedder (the end of
// graphic/graphic_recorder.h). Nothing here draws, so they are never called;
// they are defined so the library links.
namespace microtex {

float measure_cached_text_width(const std::string&, int, float) { return 0; }

float measure_glyph_advance(const std::string&, u16, float) { return 0; }

}  // namespace microtex

void check::fail(const char* file, int line, const std::string& what) {
  failures()++;
  std::cerr << file << ":" << line << ": " << what << "\n";
}

int main(int argc, char** argv) {
  int run = 0;
  for (const auto& c : check::cases()) {
    bool wanted = argc < 2;
    for (int i = 1; i < argc; i++) wanted = wanted || check::contains(c.name, argv[i]);
    if (!wanted) continue;
    run++;
    const int before = check::failures();
    try {
      c.fn();
    } catch (const std::exception& e) {
      check::fail(c.name, 0, std::string("unexpected exception: ") + e.what());
    }
    if (check::failures() > before) std::cerr << "FAILED: " << c.name << "\n\n";
  }
  std::cout << run << " tests, " << check::failures() << " failed checks\n";
  return check::failures() == 0 ? 0 : 1;
}
