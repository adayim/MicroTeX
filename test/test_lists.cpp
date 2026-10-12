// itemize and enumerate: a marker column and a column of text, set as LaTeX
// sets them. Numbers come from the stub text measurer (engine.h), so these
// look at rows and columns, not widths.

#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

Options typeface(InputMode mode = InputMode::mixed, float width = 0) {
  Options opt(mode, width);
  opt.path = false;
  return opt;
}

check::Layout items(const std::string& tex, InputMode mode = InputMode::mixed, float width = 0) {
  return check::render(tex, typeface(mode, width));
}

std::vector<float> xs(const check::Layout& l) { return check::column(check::ink(l), &DrawRecord::x); }
std::vector<float> ys(const check::Layout& l) { return check::column(check::ink(l), &DrawRecord::y); }

// The y of the text record that is exactly `word`.
float rowOf(const check::Layout& l, const std::string& word) {
  for (const auto& r : check::of(l, DrawRecord::TEXT)) {
    if (r.text == word) return r.y;
  }
  return -1e9f;
}

}  // namespace

TEST(lists_itemize_puts_each_item_on_its_own_row_marker_then_content) {
  for (int n = 1; n <= 3; n++) {
    std::string tex = "\\begin{itemize}";
    for (int i = 0; i < n; i++) tex += std::string(i ? " " : "") + "\\item " + char('a' + i);
    tex += "\\end{itemize}";
    const auto l = items(tex);
    CHECK_EQ(check::distinct(ys(l)), std::size_t(n));  // one row per item
    CHECK_EQ(l.records.size(), std::size_t(2 * n));    // a marker and its letter
  }
  // Markers share a column and every letter starts to the right of it.
  const auto columns = check::uniqueSorted(xs(items("\\begin{itemize}\\item a \\item b \\item c\\end{itemize}")));
  CHECK_EQ(columns.size(), std::size_t(2));
  CHECK(columns[1] > columns[0]);
}

TEST(lists_math_inside_an_item_is_still_set_as_math) {
  // A radical rule, and a superscript smaller than its base. In a label an
  // item is text, so its math is between $...$.
  for (const InputMode mode : {InputMode::mixed, InputMode::math}) {
    const std::string src =
      mode == InputMode::math ? R"(\begin{enumerate}\item x^2 \item \sqrt{y}\end{enumerate})"
                              : R"(\begin{enumerate}\item $x^2$ \item $\sqrt{y}$\end{enumerate})";
    check::Options path(mode);
    CHECK(check::count(check::render(src, path), DrawRecord::LINE) > 0);
    const auto l = items(src, mode);
    std::vector<float> sizes;
    for (const auto& r : check::ink(l)) {
      if (r.type == DrawRecord::GLYPH || r.type == DrawRecord::TEXT) sizes.push_back(r.font_size);
    }
    CHECK_EQ(check::distinct(sizes), std::size_t(2));
    CHECK(check::minimum(sizes) < check::maximum(sizes));
  }
}

TEST(lists_an_item_is_text_as_in_latex_when_the_list_is_met_in_text) {
  const auto l = items("\\begin{itemize}\n\\item First point\n\\item Second\n\\end{itemize}");
  // One run of text per item, the spaces at either end dropped.
  CHECK_EQ(check::texts(l), check::Strings({"First point", "Second"}));
  const auto doc = items("\\begin{itemize}\\item First point\\end{itemize}", InputMode::document);
  CHECK_EQ(doc.records.size(), std::size_t(2));
  CHECK_EQ(doc.records[1].text, std::string("First point"));
  // In math, an item is math: letters one glyph each.
  CHECK_EQ(check::count(items("\\begin{itemize}\\item ab\\end{itemize}", InputMode::math), DrawRecord::TEXT), 0);
  // A problem in an item is reported at the list.
  CHECK(check::warns(items("\\begin{itemize}\\item x^2\\end{itemize}"), "1:1: \\begin{itemize}: ^ outside math"));
}

TEST(lists_the_optional_label_changes_the_marker) {
  const auto rightEdge = [](const std::string& label) {
    return check::maximum(xs(items("\\begin{enumerate}" + label + "\\item a \\item b\\end{enumerate}")));
  };
  // "1-" is wider than the default "1.", so it pushes the text right.
  CHECK(rightEdge("[\\arabic*-]") > rightEdge(""));
  // Each documented counter still yields two rows with a marker on each.
  for (const char* label : {"[\\alph*)]", "[\\Alph*.]", "[\\roman*)]", "[\\Roman*.]"}) {
    const auto l = items(std::string("\\begin{enumerate}") + label + "\\item a \\item b\\end{enumerate}");
    CHECK_EQ(check::distinct(ys(l)), std::size_t(2));
    CHECK(l.records.size() > 2);  // markers as well as the letters
  }
  // A roman counter reaches "ii", which is two glyphs where "b" is one.
  CHECK(items("\\begin{enumerate}[\\roman*)]\\item a \\item b\\end{enumerate}").records.size() >
        items("\\begin{enumerate}[\\alph*)]\\item a \\item b\\end{enumerate}").records.size());
  // An itemize marker is customisable too.
  const auto star = items("\\begin{itemize}[\\star]\\item a \\item b\\end{itemize}");
  const auto plain = items("\\begin{itemize}\\item a \\item b\\end{itemize}");
  CHECK(check::maximum(xs(star)) > check::maximum(xs(plain)));
}

TEST(lists_a_nested_list_is_indented_past_its_parent) {
  const auto l = items("\\begin{itemize}\\item a \\item \\begin{enumerate}\\item p \\item q\\end{enumerate}\\end{itemize}");
  // The leftmost x of each row.
  std::vector<float> left;
  const auto rows = check::uniqueSorted(ys(l));
  for (const double row : rows) {
    float least = 1e9f;
    for (const auto& r : check::ink(l)) {
      if (std::llround(static_cast<double>(r.y) * 100) == std::llround(row * 100)) least = std::min(least, r.x);
    }
    left.push_back(least);
  }
  CHECK_EQ(left.size(), std::size_t(4));  // two outer, two inner
  // The outer bullets share the left margin; the inner rows start further in.
  int atMargin = 0;
  for (const float x : left) atMargin += x == check::minimum(left) ? 1 : 0;
  CHECK_EQ(atMargin, 2);
  CHECK(check::maximum(left) > check::minimum(left));
}

TEST(lists_a_nested_list_after_an_items_text_goes_below_it_as_in_latex) {
  const auto r = items("\\begin{itemize}\\item outer \\begin{itemize}\\item inner\\end{itemize}\\end{itemize}");
  CHECK(rowOf(r, "inner") > rowOf(r, "outer"));
  // Level with the outer item's text, not its marker.
  float outerX = 0;
  for (const auto& t : check::of(r, DrawRecord::TEXT)) {
    if (t.text == "outer") outerX = t.x;
  }
  float leftBelow = 1e9f;
  for (const auto& t : check::ink(r)) {
    if (t.y > rowOf(r, "outer") + 1) leftBelow = std::min(leftBelow, t.x);
  }
  CHECK(leftBelow >= outerX);
  // One that opens the item shares its line, as LaTeX sets it.
  const auto opening = items("\\begin{itemize}\\item \\begin{itemize}\\item inner\\end{itemize}\\end{itemize}");
  const auto glyphY = check::column(check::of(opening, DrawRecord::GLYPH), &DrawRecord::y);
  CHECK(check::maximum(glyphY) - check::minimum(glyphY) < 5);
  // A `}` that closes nothing cannot end the item's text early.
  CHECK_EQ(check::texts(items("\\begin{itemize}\\item {a}} b\\end{itemize}")), check::Strings({"a b"}));
}

TEST(lists_an_empty_list_draws_nothing) {
  const auto l = check::render("\\begin{itemize}\\end{itemize}", Options(InputMode::mixed));
  CHECK_EQ(l.records.size(), std::size_t(0));
}

TEST(lists_a_label_keeps_a_list_whole_whatever_is_inside) {
  const auto l = items(
    "\\begin{enumerate}\\item $x^2$ \\item T: "
    "$\\begin{array}{|c|c|}a&b\\\\c&d\\end{array}$\\end{enumerate}");
  bool seen = false;
  for (const auto& t : check::texts(l)) seen = seen || t == "T: ";
  CHECK(seen);
  CHECK(check::count(l, DrawRecord::LINE) > 0);  // the array's rules
}

TEST(lists_an_item_wraps_at_max_width_hanging_under_its_own_text) {
  // A list is a marker column and an X column.
  std::string longText;
  for (int i = 0; i < 30; i++) longText += (i ? " " : "") + std::string("word");
  for (const InputMode mode : {InputMode::document, InputMode::mixed}) {
    const auto l = items("\\begin{itemize}\\item " + longText + " \\item short\\end{itemize}", mode, 200);
    CHECK(l.width <= 200);
    const auto texts = check::of(l, DrawRecord::TEXT);
    CHECK(check::distinct(check::column(texts, &DrawRecord::y), 0) > 5);
    // Every line of the item starts where its first word does.
    float first = 1e9f;
    std::vector<float> starts;
    for (const auto& t : texts) {
      if (t.text == "word") first = std::min(first, t.x);
    }
    for (const auto& t : texts) {
      if (t.text == "word" && t.x < 60) starts.push_back(t.x);
    }
    CHECK_EQ(check::uniqueSorted(starts), std::vector<double>({std::round(first * 100) / 100}));
  }
  // A nested list wraps too, at its own, deeper indent.
  const auto nested =
    items("\\begin{itemize}\\item a \\begin{itemize}\\item " + longText + "\\end{itemize}\\end{itemize}",
          InputMode::document, 220);
  CHECK(nested.width <= 220);
  const auto texts = check::of(nested, DrawRecord::TEXT);
  float wordLeft = 1e9f;
  for (const auto& t : texts) {
    if (t.text == "word") wordLeft = std::min(wordLeft, t.x);
  }
  CHECK(wordLeft > texts[0].x);  // deeper than the outer item's "a"
}

TEST(lists_an_items_text_may_end_in_a_control_space_before_a_nested_list) {
  const std::string tex =
    "\\begin{enumerate}[\\Roman*.]\\item \\text{A:}\\ \\begin{itemize}\\item x\\end{itemize}\\end{enumerate}";
  const auto l = items(tex, InputMode::math);
  const auto plain = items("\\begin{enumerate}[\\Roman*.]\\item \\text{A:}\\ \\end{enumerate}", InputMode::math);
  // The item's own glyphs, then the nested bullet and x: nothing else.
  CHECK_EQ(l.records.size(), plain.records.size() + 2);
  // No `&` drawn.
  CHECK(check::pathCharacters(check::render(tex, Options(InputMode::math))).find(U'&') == std::u32string::npos);
  float plainText = 1e9f, nestedLeft = 1e9f;
  for (const auto& t : check::of(plain, DrawRecord::TEXT)) plainText = std::min(plainText, t.x);
  for (std::size_t i = plain.records.size(); i < l.records.size(); i++) {
    nestedLeft = std::min(nestedLeft, l.records[i].x);
  }
  CHECK(nestedLeft > plainText);
}

TEST(lists_a_short_list_keeps_its_own_width_and_labels_are_set_right) {
  // A list that fits must not grow to max_width.
  const std::string shortList = "\\begin{itemize}\\item a \\item b\\end{itemize}";
  const auto wide = items(shortList, InputMode::mixed, 400);
  const auto natural = items(shortList, InputMode::mixed, 0);
  CHECK_EQ(wide.width, natural.width);
  CHECK_EQ(wide.height, natural.height);
  CHECK_EQ(wide.depth, natural.depth);
  // Labels of different widths end level, as LaTeX sets them.
  std::string ten = "\\begin{enumerate}";
  for (int i = 1; i <= 10; i++) ten += (i > 1 ? " " : "") + std::string("\\item i") + std::to_string(i);
  ten += "\\end{enumerate}";
  CHECK_EQ(check::distinct(check::column(check::of(items(ten), DrawRecord::TEXT), &DrawRecord::x)), std::size_t(1));
}
