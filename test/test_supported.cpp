// The list of what the package draws (gridmicrotex's inst/supported/examples.tsv,
// the document build.R sets): each row is a claim, checked here. An "ok" row
// is drawn with no warning, and LaTeX sets it too (the .tex compiles with
// pdflatex); "nolatex" is drawn with no warning and has no LaTeX equivalent;
// a "no" row is one the engine does not draw, and still warns of it -- when
// one is supported, its row moves up.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

#ifndef MICROTEX_TEST_FIXTURES
#error "MICROTEX_TEST_FIXTURES must name the directory holding the fixtures"
#endif

namespace {

struct Example {
  std::string section, kind, source;
};

std::vector<Example> examples() {
  std::ifstream in(std::filesystem::path(MICROTEX_TEST_FIXTURES) / "supported" / "examples.tsv", std::ios::binary);
  std::vector<Example> out;
  std::string line;
  bool header = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (header) {
      header = false;
      continue;
    }
    const auto a = line.find('\t');
    const auto b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    out.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1), line.substr(b + 1)});
  }
  return out;
}

}  // namespace

TEST(supported_every_example_listed_as_supported_is_drawn_with_no_warning) {
  int checked = 0;
  for (const auto& ex : examples()) {
    if (ex.kind != "ok" && ex.kind != "nolatex") continue;
    checked++;
    const auto l = check::render(ex.source);
    for (const auto& w : l.warnings) check::fail(__FILE__, __LINE__, ex.source + " -> " + w);
  }
  CHECK(checked > 900);
}

TEST(supported_an_example_listed_as_not_supported_still_warns) {
  // None is listed now: every function of KaTeX's two lists is drawn.
  for (const auto& ex : examples()) {
    if (ex.kind != "no") continue;
    if (check::render(ex.source).warnings.empty()) check::fail(__FILE__, __LINE__, ex.source + " is drawn now");
  }
}
