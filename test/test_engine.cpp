// The engine from input to draw records (engine.h): findings of the fuzzers
// that are not the front end's alone.

#include "check.h"
#include "engine.h"
#include "front/expander.h"

using microtex::InputMode;

namespace {

check::Options atWidth(float w, InputMode mode = InputMode::math) {
  check::Options opt;
  opt.mode = mode;
  opt.maxWidth = w;
  return opt;
}

}  // namespace

// Found by engine_fuzz: a display with nothing in it but its \tag was a
// gather of no row, and setting it at a measure read the row that was not there.
TEST(engine_a_display_holding_only_a_tag_is_drawn_with_its_number) {
  for (const char* tex : {"\\[\\tag{1}\\]", "\\[\\tag{1}\\label{k}\\]"}) {
    const auto l = check::render(tex, atWidth(131));
    CHECK(l.warnings.empty());
    CHECK(!l.records.empty());
  }
}

// define_macro()'s macros reach what the input writes, not the engine's own
// formulas: \sec was read with the macros defined at its first use and kept
// so for the session.
TEST(engine_a_persistent_macro_does_not_reach_the_engines_own_formulas) {
  const auto plain = check::render(R"(\sec x)");
  microtex::front::setPersistentMacro("mathrm", "\\mathsf");
  const auto with = check::render(R"(\sec x)");
  microtex::front::clearPersistentMacros();
  CHECK(check::sameInk(plain, with));
}

// \limits on a predefined formula: this use of it only.
TEST(engine_limits_on_a_predefined_formula_changes_that_use_only) {
  const auto before = check::render(R"(\displaystyle \sin_{q} x)");
  check::render(R"(\displaystyle \sin\limits_{q} y)");
  const auto after = check::render(R"(\displaystyle \sin_{q} z)");
  CHECK_EQ(after.height, before.height);
  CHECK_EQ(after.depth, before.depth);
  // And it did change that use: the limits sit above and below.
  const auto limits = check::render(R"(\displaystyle \sin\limits_{q} y)");
  CHECK(limits.height + limits.depth > before.height + before.depth);
}

// The harness itself: layouts that differ are told apart, and ink is drawn.
TEST(engine_the_harness_tells_different_layouts_apart) {
  const auto a = check::render("a");
  const auto b = check::render("b");
  CHECK(a.records.size() > 0);
  CHECK(!check::sameInk(a, b));
  CHECK(check::sameInk(a, check::render("a")));
  CHECK(!check::sameInk(check::render("x^2"), check::render("x_2")));
  CHECK(!check::sameInk(check::render(R"(\frac12)"), check::render(R"(\frac13)")));
  // A warning is reported at its line and column.
  const auto w = check::render("a\n\\foo b");
  CHECK_EQ(w.warnings.size(), std::size_t(1));
  CHECK(check::contains(w.warnings[0], "2:1: unknown command"));
}
