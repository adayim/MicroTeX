// subequations, and the cases package's numcases and subnumcases: numbered
// cases, with a number to each row at the right margin (engine.h).

#include <string>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

check::Layout rec(const std::string& tex, float width = 300) {
  Options opt(InputMode::document, width);
  opt.path = false;
  return check::render(tex, opt);
}

Strings linesOf(const std::string& tex, float width = 300) { return check::lines(rec(tex, width)); }

bool quiet(const std::string& tex) { return rec(tex).warnings.empty(); }

float yOf(const check::Layout& l, const std::string& text) {
  for (const auto& r : check::of(l, DrawRecord::TEXT)) {
    if (r.text == text) return r.y;
  }
  return -1e9f;
}

}  // namespace

TEST(subequations_number_the_rows_of_its_displays_with_letters) {
  const std::string tex =
    R"(\begin{subequations}\begin{align} a &= b \\ c &= d \end{align}\end{subequations})";
  CHECK_EQ(linesOf(tex), Strings({"(1a)", "(1b)"}));
  // One equation of its own takes a letter too, and the count goes on after it.
  const std::string two = R"(\begin{equation} x \end{equation})" "\n\n"
                          R"(\begin{subequations}\begin{equation} y \end{equation})" "\n\n"
                          R"(\begin{align} z &= 1 \\ w &= 2 \end{align}\end{subequations})" "\n\n"
                          R"(\begin{equation} u \end{equation})";
  CHECK_EQ(linesOf(two), Strings({"(1)", "(2a)", "(2b)", "(2c)", "(3)"}));
}

TEST(subequations_leaves_its_displays_where_they_would_be_without_it) {
  // It is no paragraph of its own: no indent, and a display under the one before it.
  const std::string inner = R"(\begin{equation} y \end{equation}\begin{align} z &= 1 \\ w &= 2 \end{align})";
  const auto plain = rec(inner);
  const auto sub = rec(R"(\begin{subequations})" + inner + R"(\end{subequations})");
  const auto a = check::of(plain, DrawRecord::GLYPH), b = check::of(sub, DrawRecord::GLYPH);
  CHECK_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
    CHECK(std::fabs(a[i].x - b[i].x) < 0.01 && std::fabs(a[i].y - b[i].y) < 0.01);
  }
  CHECK_EQ(plain.width, sub.width);
  CHECK_EQ(plain.height, sub.height);
}

TEST(subequations_a_tag_or_notag_does_not_use_up_a_letter) {
  const std::string tex = R"(\begin{subequations}\begin{align} a &= b \notag \\ c &= d \\ e &= f \tag{*} \\ g &= h \end{align}\end{subequations})";
  CHECK_EQ(linesOf(tex), Strings({"(1a)", "(*)", "(1b)"}));
}

TEST(subequations_a_label_after_its_start_names_the_whole_and_one_in_a_row_that_row) {
  const std::string tex =
    R"(\begin{subequations}\label{all}\begin{align} a &= b \label{first} \\ c &= d \label{second} \end{align}\end{subequations})"
    "\n\n" R"(See \ref{all}, \ref{first} and \eqref{second}.)";
  CHECK(quiet(tex));
  const auto lines = linesOf(tex);
  CHECK_EQ(lines.size(), std::size_t(3));
  CHECK_EQ(lines[2], std::string("See1,1aand(1b)."));
}

TEST(subequations_letters_go_on_past_z) {
  Options opt(InputMode::mixed);
  std::string rows;
  for (int i = 0; i < 28; i++) rows += "x &= y \\\\ ";
  const auto l = check::render(R"(\begin{subequations}\begin{align} )" + rows + R"(\end{align}\end{subequations})", opt);
  const auto texts = check::texts(l);
  CHECK(!texts.empty());
  CHECK_EQ(texts.front(), std::string("(1a)"));
  CHECK_EQ(texts[25], std::string("(1z)"));
  CHECK_EQ(texts[26], std::string("(1aa)"));
}

TEST(numcases_numbers_each_case_at_the_right_margin) {
  const std::string tex = R"(\begin{numcases}{|x|=} x, & for $x \geq 0$ \\ -x, & for $x < 0$ \end{numcases})";
  CHECK(quiet(tex));
  const auto l = rec(tex);
  const auto lines = check::lines(l);
  // Each row: its explanation, then its number.
  CHECK_EQ(lines.size(), std::size_t(2));
  if (lines.size() == 2) {
    CHECK(lines[0].find("(1)") != std::string::npos && lines[0].find("for") != std::string::npos);
    CHECK(lines[1].find("(2)") != std::string::npos && lines[1].find("for") != std::string::npos);
  }
  // The numbers are flush right, one under the other, each level with its case.
  float rightmost = 0;
  for (const auto& t : check::of(l, DrawRecord::TEXT)) {
    if (t.text == "(1)" || t.text == "(2)") rightmost = std::max(rightmost, t.x + 3 * 11.0f);
  }
  CHECK(std::fabs(rightmost - 300) < 1);
  CHECK(yOf(l, "(2)") > yOf(l, "(1)"));
  // ... on the baseline of its case's explanation, the first row's and the second's.
  const auto fors = check::of(l, DrawRecord::TEXT);
  std::vector<float> explained;
  for (const auto& r : fors) {
    if (r.text == "for") explained.push_back(r.y);
  }
  CHECK_EQ(explained.size(), std::size_t(2));
  if (explained.size() == 2) {
    CHECK(std::fabs(explained[0] - yOf(l, "(1)")) < 0.5);
    CHECK(std::fabs(explained[1] - yOf(l, "(2)")) < 0.5);
  }
}

TEST(numcases_a_case_is_in_display_style_and_the_explanation_is_text) {
  const auto l = rec(R"(\begin{numcases}{f(x)=} \frac{1}{x}, & if $x \neq 0$ \\ 0 & otherwise \end{numcases})");
  // The fraction's numerator is at the size of the line, as in a display.
  const auto glyphs = check::of(l, DrawRecord::GLYPH);
  CHECK(!glyphs.empty());
  float biggest = 0;
  for (const auto& g : glyphs) biggest = std::max(biggest, g.font_size);
  CHECK(biggest >= 20);
  CHECK(check::texts(l).size() >= 4);
}

TEST(numcases_nonumber_and_label_name_a_case) {
  const std::string tex =
    R"(\begin{numcases}{y=} a, & x \\ b, & z \nonumber \\ c & w \label{third} \end{numcases})"
    "\n\n" R"(Case \ref{third}.)";
  CHECK(quiet(tex));
  const auto lines = linesOf(tex);
  // Two numbers, and the third case is the second number.
  CHECK_EQ(lines.back(), std::string("Case2."));
  Strings numbers;
  for (const auto& t : check::texts(rec(tex))) {
    if (!t.empty() && t[0] == '(') numbers.push_back(t);
  }
  CHECK_EQ(numbers, Strings({"(1)", "(2)"}));
}

TEST(subnumcases_numbers_the_cases_with_letters_and_the_next_equation_goes_on) {
  const std::string tex =
    R"(\begin{subnumcases}{w=} 0 & if $a$ \\ 1 & if $b$ \end{subnumcases})" "\n\n" R"(\begin{equation} z \end{equation})";
  Strings numbers;
  for (const auto& t : check::texts(rec(tex))) {
    if (!t.empty() && t[0] == '(') numbers.push_back(t);
  }
  CHECK_EQ(numbers, Strings({"(1a)", "(1b)", "(2)"}));
}

TEST(numcases_draws_its_left_side_before_the_brace) {
  const auto with = rec(R"(\begin{numcases}{|x|=} a & b \\ c & d \end{numcases})");
  const auto without = rec(R"(\begin{numcases}{} a & b \\ c & d \end{numcases})");
  CHECK(with.warnings.empty());
  // |, x, | and =.
  CHECK_EQ(check::of(with, DrawRecord::GLYPH).size(), check::of(without, DrawRecord::GLYPH).size() + 4);
  // The left side is the leftmost thing drawn.
  float leftmost = 1e9f, braceX = 1e9f;
  for (const auto& r : with.records) leftmost = std::min(leftmost, r.x);
  for (const auto& r : without.records) braceX = std::min(braceX, r.x);
  CHECK(leftmost < braceX);
}

TEST(numcases_an_empty_left_side_and_no_width) {
  const auto l = rec(R"(\begin{numcases}{} 1 & a \\ 2 & b \end{numcases})", 0);
  CHECK(l.warnings.empty());
  Strings numbers;
  for (const auto& t : check::texts(l)) {
    if (!t.empty() && t[0] == '(') numbers.push_back(t);
  }
  CHECK_EQ(numbers, Strings({"(1)", "(2)"}));
}

