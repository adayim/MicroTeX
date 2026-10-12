// Text is drawn as runs: with no width set, a whole phrase is one run (so
// the device orders it and kerns across spaces); with a width set, one run
// per word, since the spaces are where lines break. See
// RowAtom::processTextRun in lib/atom/atom_row.cpp. Right-to-left text is
// reordered per line (FriBidi), so those tests need an engine built with it.

#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

// The text records of a formula, in drawing order.
Strings texts(const std::string& tex, float width = 0, float size = 20) {
  Options opt(InputMode::math, width);
  opt.path = false;
  opt.textSize = size;
  return check::texts(check::render(tex, opt));
}

check::Layout label(const std::string& tex) {
  Options opt(InputMode::mixed);
  opt.path = false;
  return check::render(tex, opt);
}

std::string txt(const std::string& s) { return "\\text{" + s + "}"; }

}  // namespace

TEST(runs_an_unwrapped_phrase_is_a_single_record) {
  CHECK_EQ(texts("\\text{Hello}"), Strings({"Hello"}));
  CHECK_EQ(texts("\\text{Hello, world!}"), Strings({"Hello, world!"}));
  CHECK_EQ(texts("\\text{page 42 of 99}"), Strings({"page 42 of 99"}));
}

TEST(runs_a_wrapped_phrase_keeps_one_run_per_word) {
  CHECK_EQ(texts("\\text{one two three}", 40), Strings({"one", "two", "three"}));
  CHECK_EQ(texts("\\text{ab12cd}", 40), Strings({"ab", "1", "2", "cd"}));
}

TEST(runs_math_is_laid_out_per_glyph_untouched) {
  CHECK_EQ(label("$\\frac{a}{b}$").records.size(), std::size_t(3));
  CHECK_EQ(label("$\\sum_{i=1}^n x_i^2$").records.size(), std::size_t(8));
  CHECK_EQ(label("$\\alpha\\beta\\gamma$").records.size(), std::size_t(3));
}

TEST(runs_a_run_stops_at_anything_that_is_not_plain_text) {
  // A font change, math or a measured skip ends a run; a space does not.
  CHECK_EQ(texts("\\textbf{bold}\\text{plain}"), Strings({"bold", "plain"}));
  CHECK_EQ(texts("\\textsf{ab\\textrm{cd}ef}"), Strings({"ab", "cd", "ef"}));
  CHECK_EQ(texts("\\text{before $x$ after}"), Strings({"before ", " after"}));
  CHECK_EQ(texts("\\text{a \\quad b}").size(), std::size_t(2));
}

TEST(runs_a_p_cell_wraps_even_when_the_formula_as_a_whole_does_not) {
  // A p{} cell is broken to its own width, so it keeps word runs.
  const std::string cell = "\\text{a fairly long cell that ought to wrap inside its column}";
  const auto mk = [&](const std::string& spec) {
    return "\\begin{tabular}{" + spec + "}\\hline " + cell + " & \\text{b}\\\\ \\hline\\end{tabular}";
  };
  Options math(InputMode::math);
  const int free = check::render(mk("ll"), math).width;
  const int fixed = check::render(mk("p{3cm}l"), math).width;
  CHECK(fixed < free / 2);
}

TEST(runs_wrapping_still_happens_and_still_respects_the_measure) {
  std::string prose;
  for (int i = 0; i < 3; i++) prose += (i ? " " : "") + std::string("The quick brown fox jumps over the lazy dog.");
  for (const float mw : {150.0f, 250.0f, 400.0f}) {
    Options opt(InputMode::math, mw);
    const auto d = check::render(txt(prose), opt);
    CHECK(d.width <= mw);
    CHECK(d.height > 18);  // it did wrap
  }
}

#ifdef HAVE_FRIBIDI

namespace {

// The words of each line read from the right, across the lines in order.
Strings rightToLeft(const std::string& tex, float width = 110, float size = 14, InputMode mode = InputMode::math) {
  Options opt(mode, width);
  opt.path = false;
  opt.textSize = size;
  const auto runs = check::of(check::render(tex, opt), DrawRecord::TEXT);
  Strings out;
  for (const double row : check::uniqueSorted(check::column(runs, &DrawRecord::y), 1)) {
    Records line;
    for (const auto& r : runs) {
      if (std::fabs(std::round(r.y * 10) / 10 - row) < 0.01) line.push_back(r);
    }
    std::sort(line.begin(), line.end(), [](const auto& a, const auto& b) { return a.x > b.x; });
    for (const auto& r : line) out.push_back(r.text);
  }
  return out;
}

Strings concat(Strings a, const Strings& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

const Strings kArabic = {"مرحبا", "بك", "في", "العالم"};

}  // namespace

TEST(runs_an_unwrapped_right_to_left_phrase_comes_out_in_the_right_order) {
  // One run, so the device orders it.
  const std::string ar = std::string("مرحبا") + " " + "بك";
  CHECK_EQ(texts(txt(ar)), Strings({ar}));
  // Mixed scripts too -- the device resolves the whole line at once.
  const std::string mixed = std::string("مرحبا") + " abc " + "بك";
  CHECK_EQ(texts(txt(mixed)), Strings({mixed}));
}

TEST(runs_a_wrapped_right_to_left_paragraph_is_reordered_per_line) {
  // Reading each line right to left gives the source order back.
  Strings src;
  std::string ar;
  for (int i = 0; i < 3; i++) {
    for (const auto& w : kArabic) {
      src.push_back(w);
      ar += (ar.empty() ? "" : " ") + w;
    }
  }
  Options opt(InputMode::math, 120);
  opt.path = false;
  opt.textSize = 16;
  const auto d = check::of(check::render(txt(ar), opt), DrawRecord::TEXT);
  CHECK(check::distinct(check::column(d, &DrawRecord::y), 1) > 1);  // it really wrapped
  CHECK_EQ(rightToLeft(txt(ar), 120, 16), src);
}

TEST(runs_right_to_left_text_is_ordered_across_font_groups_not_only_within_one) {
  // See RowAtom::collectBidiText.
  const auto& a = kArabic;
  // Two plain groups, and an emphasised group between two plain ones.
  CHECK_EQ(rightToLeft(txt(a[0] + " " + a[1] + " ") + txt(a[2] + " " + a[3])), a);
  CHECK_EQ(rightToLeft(txt(a[0] + " " + a[1] + " ") + "\\textbf{" + txt(a[2]) + "}" + txt(" " + a[3])), a);
  // A colour and a named family are wrappers of their own.
  CHECK_EQ(rightToLeft(txt(a[0] + " ") + "\\textcolor{red}{" + txt(a[1]) + "}" + txt(" " + a[2])), Strings(a.begin(), a.begin() + 3));
  CHECK_EQ(rightToLeft(txt(a[0] + " ") + "\\gmfontfamily{serif}{" + txt(a[1]) + "}" + txt(" " + a[2])),
           Strings(a.begin(), a.begin() + 3));
  // One group, and emphasis nested inside a group.
  CHECK_EQ(rightToLeft(txt(a[0] + " " + a[1] + " " + a[2] + " " + a[3])), a);
  CHECK_EQ(rightToLeft(txt(a[0] + " \\textbf{" + a[1] + "} " + a[2])), Strings(a.begin(), a.begin() + 3));
}

TEST(runs_a_left_to_right_run_inside_right_to_left_keeps_its_own_direction) {
  const std::string a1 = "مرحبا", a2 = "بك";
  // A Latin word in its own group sits between the two Arabic words.
  CHECK_EQ(rightToLeft(txt(a1 + " ") + "\\textbf{" + txt("Latin") + "}" + txt(" " + a2)), Strings({a1, "Latin", a2}));
  // Each digit is its own record; read from the right they come back
  // reversed, i.e. drawn left to right.
  const auto got = rightToLeft(txt(a1 + " ") + txt("2026") + txt(" " + a2));
  CHECK(got.size() >= 6);
  CHECK_EQ(got.front(), a1);  // Arabic still rightmost
  CHECK_EQ(got.back(), a2);
  Strings digits(got.begin() + 1, got.end() - 1);
  std::reverse(digits.begin(), digits.end());
  CHECK_EQ(digits, Strings({"2", "0", "2", "6"}));
}

TEST(runs_a_left_to_right_group_that_breaks_across_lines_stays_in_the_flow) {
  // A broken piece of an all-Latin group takes its neighbour's level.
  const Strings u = {"خۇش", "كەلدىڭىز", "بۇ", "دۇنياغا"};
  const Strings latin = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta"};
  std::string joined;
  for (const auto& w : latin) joined += (joined.empty() ? "" : " ") + w;
  const std::string tex = txt(u[0] + " " + u[1] + " ") + "\\textbf{" + txt(joined) + "}" + txt(" " + u[2] + " " + u[3]);
  Options opt(InputMode::math, 130);
  opt.path = false;
  opt.textSize = 14;
  const auto d = check::of(check::render(tex, opt), DrawRecord::TEXT);
  const auto rows = check::uniqueSorted(check::column(d, &DrawRecord::y), 1);
  CHECK(rows.size() > 1);  // it really wrapped
  // Right-to-left text reads from the right, Latin from the left.
  Strings gotU, gotLatin;
  for (const double row : rows) {
    Records line;
    for (const auto& r : d) {
      if (std::fabs(std::round(r.y * 10) / 10 - row) < 0.01) line.push_back(r);
    }
    auto byXDesc = line, byX = line;
    std::sort(byXDesc.begin(), byXDesc.end(), [](const auto& a, const auto& b) { return a.x > b.x; });
    std::sort(byX.begin(), byX.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    for (const auto& r : byXDesc) {
      if (std::find(u.begin(), u.end(), r.text) != u.end()) gotU.push_back(r.text);
    }
    for (const auto& r : byX) {
      if (std::find(latin.begin(), latin.end(), r.text) != latin.end()) gotLatin.push_back(r.text);
    }
  }
  CHECK_EQ(gotU, u);
  CHECK_EQ(gotLatin, latin);
}

TEST(runs_an_explicit_right_to_left_mark_counts_as_right_to_left) {
  // U+200F has no script of its own.
  const std::string rlm = "‏";
  const Strings words = {"(a)", "(b)", "(c)"};
  const auto orderOf = [&](const std::string& tex) {
    Options opt(InputMode::math, 100);
    opt.path = false;
    opt.textSize = 14;
    auto d = check::of(check::render(tex, opt), DrawRecord::TEXT);
    std::sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.x > b.x; });  // right to left
    Strings out;
    for (const auto& r : d) out.push_back(r.text);
    return out;
  };
  // (A measure the three words fit in, so each is a run of its own on one line.)
  // With the mark the line reads right to left; without it, it does not.
  const auto marked = orderOf(txt(rlm + "(a) (b) (c)"));
  CHECK_EQ(marked.size(), std::size_t(3));
  CHECK(marked[0].find('a') != std::string::npos);
  CHECK_EQ(orderOf(txt("(a) (b) (c)"))[0], std::string("(c)"));
}

TEST(runs_a_line_ending_at_a_drawn_hyphen_is_still_reordered) {
  const std::string ar = "مرحبا";
  const auto firstLine = [&](const std::string& tex) {
    Options opt(InputMode::math, 130);
    opt.path = false;
    opt.textSize = 14;
    auto d = check::of(check::render(tex, opt), DrawRecord::TEXT);
    const float top = check::minimum(check::column(d, &DrawRecord::y));
    Records line;
    for (const auto& r : d) {
      if (std::fabs(std::round(r.y * 10) / 10 - std::round(top * 10) / 10) < 0.01) line.push_back(r);
    }
    std::sort(line.begin(), line.end(), [](const auto& a, const auto& b) { return a.x > b.x; });
    Strings out;
    for (const auto& r : line) out.push_back(r.text);
    return out;
  };
  const auto hy = firstLine(txt(ar + " inter\\-nation\\-alization بك"));
  CHECK(std::find(hy.begin(), hy.end(), "-") != hy.end());  // the break really was taken
  // The Arabic word is rightmost.
  CHECK_EQ(hy[0], ar);
  CHECK_EQ(firstLine(txt(ar + " internationalization بك"))[0], ar);
}

TEST(runs_right_to_left_runs_are_ordered_without_a_max_width) {
  const std::string heb = "שלום עולם";
  const auto orderOf = [&](const std::string& tex, float width = 0) {
    Options opt(InputMode::math, width);
    opt.path = false;
    opt.textSize = 14;
    auto d = check::of(check::render(tex, opt), DrawRecord::TEXT);
    std::sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    Strings out;
    for (const auto& r : d) out.push_back(r.text);
    return out;
  };
  // The base direction is right-to-left, so the Latin run goes on the left.
  for (const std::pair<std::string, std::string>& wrap :
       std::vector<std::pair<std::string, std::string>>{{"\\textbf{", "}"}, {"\\textcolor{red}{", "}"}, {"\\gmfontfamily{serif}{", "}"}}) {
    const auto got = orderOf(txt(heb + " ") + wrap.first + txt("alpha") + wrap.second);
    CHECK_EQ(got.size(), std::size_t(2));
    CHECK_EQ(got[0], std::string("alpha"));
    CHECK(got[1].rfind("ש", 0) == 0);
  }
  // Setting a width must not change the reading order, only where it breaks.
  CHECK_EQ(orderOf(txt(heb + " ") + "\\textbf{" + txt("alpha") + "}")[0], std::string("alpha"));
  CHECK_EQ(orderOf(txt(heb + " ") + "\\textbf{" + txt("alpha") + "}", 400)[0], std::string("alpha"));
  // The Hebrew is still one record.
  CHECK_EQ(orderOf(txt(heb)).size(), std::size_t(1));
  // A left-to-right formula must be untouched by any of this.
  CHECK_EQ(orderOf(txt("alpha ") + "\\textbf{" + txt("beta") + "}"), Strings({"alpha ", "beta"}));
}

#endif  // HAVE_FRIBIDI
