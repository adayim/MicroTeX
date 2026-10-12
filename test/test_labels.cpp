// Mixed mode: prose, math in `$...$` and its kin, and a line end in the prose
// as a line break (a plot label). R's latex_wrap() is tested in R.

#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

Options typeface(InputMode mode = InputMode::mixed) {
  Options opt(mode);
  opt.path = false;
  return opt;
}

check::Layout rec(const std::string& tex, InputMode mode = InputMode::mixed) {
  return check::render(tex, typeface(mode));
}

Records textsOf(const std::string& tex) { return check::of(rec(tex), DrawRecord::TEXT); }

check::Strings strings(const Records& rs) {
  check::Strings out;
  for (const auto& r : rs) out.push_back(r.text);
  return out;
}

std::vector<float> diffs(const std::vector<float>& v) {
  std::vector<float> out;
  for (std::size_t i = 1; i < v.size(); i++) out.push_back(v[i] - v[i - 1]);
  return out;
}

bool near(const std::vector<float>& a, const std::vector<float>& b, float tolerance = 1e-4f) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); i++) {
    if (std::fabs(a[i] - b[i]) > tolerance) return false;
  }
  return true;
}

}  // namespace

TEST(labels_a_tabulars_cells_are_text_in_a_label_as_in_latex) {
  CHECK_EQ(strings(textsOf("x\n\\begin{tabular}{l}p q\\end{tabular}")), check::Strings({"x", "p q"}));
}

TEST(labels_a_span_of_math_is_a_list_of_its_own_as_in_tex) {
  // A sign opening the span is not binary, and \over takes only the span.
  const auto xOf = [](const std::string& tex) {
    return check::column(check::of(rec(tex), DrawRecord::GLYPH), &DrawRecord::x);
  };
  CHECK(near(diffs(xOf("The value $-x$ here")), diffs(xOf("$-x$"))));
  const auto ratio = check::of(rec("Ratio: $a \\over b$ end"), DrawRecord::TEXT);
  CHECK_EQ(check::column(ratio, &DrawRecord::font_size), std::vector<float>({20, 20}));
  // And a line end in the prose ends \over's denominator there.
  CHECK_EQ(strings(textsOf("a \\over b\nnext line")), check::Strings({"a ", "next line"}));
}

TEST(labels_a_line_end_in_the_prose_breaks_the_line_even_after_a_command) {
  const auto r = textsOf("Price \\euro\nnext");
  CHECK_EQ(r.size(), std::size_t(2));
  CHECK_EQ(r[1].text, std::string("next"));
  CHECK(r[1].y > r[0].y);
  // A comment takes its line end with it, as in TeX.
  CHECK_EQ(strings(textsOf("a% note\nb")), check::Strings({"ab"}));
  // In math a line end is a space, as in TeX.
  CHECK_EQ(check::distinct(check::column(check::ink(rec("$a\nb$")), &DrawRecord::y)), std::size_t(1));
}

TEST(labels_a_break_inside_a_group_ends_the_line_and_the_group_goes_on) {
  const auto r = textsOf("\\textbf{one\ntwo} three");
  CHECK_EQ(strings(r), check::Strings({"one", "two", " three"}));
  CHECK_EQ(r[0].font_style, r[1].font_style);
  CHECK(r[2].font_style != r[0].font_style);
  CHECK_EQ(check::distinct(check::column(r, &DrawRecord::y)), std::size_t(2));
  // \color runs to the end of its group, across the break, as in TeX.
  const auto c = textsOf("{\\color{red} a\nb} c");
  CHECK_EQ(c.size(), std::size_t(3));
  CHECK_EQ(c[0].col, check::kRed);
  CHECK_EQ(c[1].col, check::kRed);
  CHECK_EQ(c[2].col, 0xFF000000u);
}

TEST(labels_breaks_at_either_end_are_none_and_a_run_of_them_is_one) {
  const auto rows = [](const std::string& tex) {
    return check::distinct(check::column(textsOf(tex), &DrawRecord::y));
  };
  CHECK_EQ(rows("\n\nTitle\n\n"), std::size_t(1));
  CHECK_EQ(rows("a\n\n\nb"), std::size_t(2));
  CHECK_EQ(rows("a \\\\ \\\\ b"), std::size_t(2));
  // A line with nothing drawn on it is no line.
  const auto one = check::render("$x$", Options(InputMode::mixed));
  const auto defined = check::render("\\newcommand{\\foo}{x}\n$\\foo$", Options(InputMode::mixed));
  CHECK_EQ(defined.height, one.height);
}

TEST(labels_a_row_gap_adds_its_space_below_the_line_in_both_modes) {
  for (const InputMode mode : {InputMode::mixed, InputMode::math}) {
    const auto ys = [&](const std::string& tex) {
      return check::column(check::ink(rec(tex, mode)), &DrawRecord::y);
    };
    const auto plain = ys("a\\\\b");
    const auto gap = ys("a\\\\[10pt]b");
    CHECK_EQ(plain.size(), std::size_t(2));
    CHECK_EQ(gap.size(), std::size_t(2));
    CHECK(std::fabs((gap[1] - gap[0]) - (plain[1] - plain[0]) - 9.96) < 0.01);
  }
}

TEST(labels_problems_are_reported_where_they_are_in_the_label) {
  const Options mixed(InputMode::mixed);
  CHECK(check::warns(check::render("a $x$ \\nosuch b", mixed), "1:7: unknown command \\nosuch"));
  CHECK(check::warns(check::render("Revenue ($)", mixed), "1:10: missing $ inserted"));
  CHECK(check::warns(check::render("\\textit{a $b$ c}\n\\bad", mixed), "2:1: unknown command \\bad"));
}

TEST(labels_underscore_caret_hash_and_ampersand_in_prose_are_drawn_and_warned_about) {
  const Options mixed(InputMode::mixed);
  const auto caret = check::render("x^2", mixed);
  CHECK(check::warns(caret, "1:2: ^ outside math is drawn as a character"));
  CHECK_EQ(check::texts(caret), check::Strings({"x^2"}));
  CHECK(check::warns(check::render("a_b", mixed), "1:2: _ outside math"));
  CHECK(check::warns(check::render("# Title", mixed), "1:1: macro parameter #"));
  // Escaped, they are characters of the text, with nothing to warn about.
  const auto escaped = check::render("a \\& b \\% c \\$ d \\# e \\_ f", mixed);
  CHECK(escaped.warnings.empty());
  CHECK_EQ(check::texts(escaped), check::Strings({"a & b % c $ d # e _ f"}));
}

TEST(labels_a_tie_at_the_start_of_a_line_is_a_space_that_stays) {
  CHECK_EQ(strings(textsOf("a\n~b")), check::Strings({"a", " b"}));
}

TEST(labels_a_number_or_a_dimension_takes_one_optional_space_as_in_tex) {
  // The space ends the dimension and goes with it; a second one is a space.
  const auto word = textsOf("\\kern100bp word");
  CHECK_EQ(word.size(), std::size_t(1));
  CHECK_EQ(word[0].text, std::string("word"));
  CHECK_EQ(word[0].x, 100.0f);
  CHECK_EQ(strings(textsOf("\\kern100bp\\ word")), check::Strings({" word"}));
  // In a label, a line end there still breaks the line.
  const auto lines = textsOf("a\\kern10bp\nb");
  CHECK_EQ(strings(lines), check::Strings({"a", "b"}));
  CHECK(lines[0].y != lines[1].y);
}
