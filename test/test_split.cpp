// Line breaking in the splitter (core/split.cpp): wrapping at a measure,
// justification, and total-fit breaking (engine.h). Text is measured by the
// stub (11 px a character at the default size), so these compare layouts
// with each other and with the measure.

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

const std::string kLong = "\\text{longword longword longword longword longword longword longword longword longword "
                          "longword longword longword}";
const std::string kShort = "\\text{short}";

check::Layout dims(const std::string& tex, float width = 0, bool justify = false, bool optimal = false) {
  Options opt(InputMode::mixed);  // latex_dims() reads a label
  opt.maxWidth = width;
  opt.justify = justify;
  opt.optimalBreak = optimal;
  return check::render(tex, opt);
}

std::string para() {
  std::string out = "\\text{";
  for (int i = 0; i < 4; i++) out += (i ? " " : "") + std::string("The quick brown fox jumps over the lazy dog.");
  return out + "}";
}

// The right edge of each rendered line: the end of its last run (a run is as
// wide as the stub measures its characters).
std::vector<float> lineEdges(const check::Layout& l) {
  std::map<long, float> edge;
  for (const auto& r : check::ink(l)) {
    float width = 0;
    if (r.type == DrawRecord::TEXT) {
      std::size_t characters = 0;
      for (const unsigned char c : r.text) characters += (c & 0xC0) != 0x80 ? 1 : 0;
      width = 0.55f * r.font_size * static_cast<float>(characters);
    } else if (r.type == DrawRecord::FILL_RECT) {
      width = r.width;
    }
    const long row = std::lround(r.y * 10);
    edge[row] = std::max(edge.count(row) ? edge[row] : -1e9f, r.x + width);
  }
  std::vector<float> out;
  for (const auto& kv : edge) out.push_back(kv.second);
  return out;
}

float spread(const std::vector<float>& v) { return check::maximum(v) - check::minimum(v); }

float deviation(const std::vector<float>& v) {
  float mean = 0;
  for (const float x : v) mean += x;
  mean /= static_cast<float>(v.size());
  float sum = 0;
  for (const float x : v) sum += (x - mean) * (x - mean);
  return std::sqrt(sum / static_cast<float>(v.size() - 1));
}

std::vector<float> withoutLast(std::vector<float> v) {
  v.pop_back();
  return v;
}

}  // namespace

TEST(split_content_separated_by_a_row_break_honours_max_width) {
  for (const std::string& tex : {kShort + "\\\\" + kLong, kLong + "\\\\" + kLong}) {
    const auto wide = dims(tex);
    const auto narrow = dims(tex, 200);
    CHECK(narrow.split);
    CHECK(narrow.width < wide.width);
    CHECK(narrow.width <= 200);
    // Wrapping trades width for height.
    CHECK(narrow.height > wide.height);
  }
}

TEST(split_a_decoration_whose_only_content_is_another_still_wraps) {
  std::string words;
  for (int i = 0; i < 4; i++) words += (i ? " " : "") + std::string("one two three four");
  for (const char* outer : {"{\\large ", "\\uline{", "\\textscale{1.5}{"}) {
    for (const char* inner : {"\\uline{", "\\bgcolor{yellow}{", "\\textcolor{red}{"}) {
      const std::string tex = std::string(outer) + inner + words + "}}";
      CHECK(dims(tex, 100).width <= 110);
    }
  }
  // One that fits is untouched.
  CHECK(!dims("{\\large \\uline{a b}}", 100).split);
}

TEST(split_a_single_flowing_paragraph_still_wraps_as_it_always_did) {
  const auto narrow = dims(kLong, 200);
  CHECK(narrow.split);
  CHECK(narrow.width <= 200);
}

TEST(split_content_that_already_fits_is_returned_untouched) {
  for (const char* tex : {"\\text{short}", "\\frac{a}{b}", "\\begin{matrix}a&b\\\\c&d\\end{matrix}",
                          "\\begin{cases}a&b\\\\c&d\\end{cases}", "\\begin{pmatrix}1\\\\2\\end{pmatrix}"}) {
    const auto a = dims(tex);
    const auto b = dims(tex, 200);
    CHECK_EQ(b.width, a.width);
    CHECK_EQ(b.height, a.height);
    CHECK(!b.split);
  }
}

TEST(split_vertical_centring_on_the_math_axis_survives_a_rebuilt_vbox) {
  // MatrixAtom sets the VBox height/depth to centre it on the math axis.
  for (const char* tex : {"\\begin{matrix}a&b\\\\c&d\\end{matrix}", "\\begin{cases}x\\\\y\\end{cases}",
                          "\\begin{pmatrix}1\\\\2\\end{pmatrix}"}) {
    const auto plain = dims(tex);
    const auto limited = dims(tex, 200);
    CHECK_EQ(limited.baseline, plain.baseline);
    CHECK_EQ(limited.depth, plain.depth);
  }
}

TEST(split_splitting_a_wrapped_paragraph_keeps_the_text_on_the_page) {
  // Every drawn record sits inside the reported bounding box.
  const auto l = dims(kShort + "\\\\" + kLong, 200);
  CHECK(l.split);
  CHECK(check::maximum(check::column(check::ink(l), &DrawRecord::x)) <= static_cast<float>(l.width) + 1);
  CHECK(check::minimum(check::column(check::ink(l), &DrawRecord::x)) >= -1);
}

// --- justification: interword spaces carry TeX's half-space stretch ---------

TEST(split_justified_text_fills_the_measure_exactly) {
  for (const float w : {200.0f, 300.0f, 400.0f}) CHECK_EQ(dims(para(), w, true).width, static_cast<int>(w));
}

TEST(split_justification_evens_the_lines_out) {
  const auto justified = withoutLast(lineEdges(dims(para(), 300, true)));
  const auto ragged = withoutLast(lineEdges(dims(para(), 300, false)));
  CHECK(justified.size() > 2);
  // Justified lines end at nearly the same place; ragged ones do not.
  CHECK(spread(justified) < 0.25f * spread(ragged));
}

TEST(split_the_last_line_stays_ragged) {
  const auto e = lineEdges(dims(para(), 300, true));
  CHECK(e.back() < check::minimum(withoutLast(e)) - 10);
}

TEST(split_justification_does_not_change_how_tall_the_text_is) {
  for (const float w : {200.0f, 300.0f}) {
    const auto ragged = dims(para(), w, false);
    const auto justified = dims(para(), w, true);
    CHECK_EQ(justified.height, ragged.height);
    CHECK_EQ(justified.split, ragged.split);
  }
}

TEST(split_justify_does_nothing_without_max_width) {
  CHECK_EQ(dims(para(), 0, true).width, dims(para(), 0, false).width);
}

TEST(split_a_line_of_one_long_word_is_left_alone) {
  const std::string solid = "\\text{" + std::string(80, 'x') + "}";
  // No space to stretch: it overruns rather than being padded.
  CHECK_EQ(dims(solid, 100, true).width, dims(solid, 100, false).width);
  CHECK(dims(solid, 100, true).width > 100);
}

// --- optimal (total-fit) line breaking ---------------------------------------

TEST(split_optimal_never_overflows_more_than_greedy_does) {
  for (float w = 140; w <= 360; w += 20) {
    const int greedy = dims(para(), w).width;
    const int optimal = dims(para(), w, false, true).width;
    CHECK(static_cast<float>(optimal) <= std::max(static_cast<float>(greedy), w) + 0.51f);
  }
}

TEST(split_optimal_does_not_make_the_paragraph_taller) {
  for (const float w : {180.0f, 220.0f, 260.0f, 300.0f, 340.0f}) {
    CHECK(dims(para(), w, false, true).height <= dims(para(), w).height);
  }
}

TEST(split_optimal_is_at_least_as_even_as_greedy) {
  const auto sd = [](const check::Layout& l) {
    const auto e = withoutLast(lineEdges(l));  // the last line is deliberately short
    return e.size() < 2 ? 0.0f : deviation(e);
  };
  for (const float w : {220.0f, 260.0f, 300.0f, 340.0f}) {
    CHECK(sd(dims(para(), w, false, true)) <= sd(dims(para(), w)) + 1e-4f);
  }
}

TEST(split_optimal_measurably_improves_at_least_one_width) {
  bool better = false;
  for (const float w : {140.0f, 180.0f, 220.0f, 260.0f, 300.0f, 340.0f, 380.0f}) {
    const auto g = withoutLast(lineEdges(dims(para(), w)));
    const auto o = withoutLast(lineEdges(dims(para(), w, false, true)));
    if (g.size() > 1 && o.size() > 1 && deviation(o) < deviation(g) - 1e-3f) better = true;
  }
  CHECK(better);
}

TEST(split_optimal_composes_with_justify) { CHECK_EQ(dims(para(), 300, true, true).width, 300); }

TEST(split_content_with_nothing_to_break_is_unaffected_by_optimal) {
  const std::string solid = "\\text{" + std::string(60, 'x') + "}";
  CHECK_EQ(dims(solid, 100, false, true).width, dims(solid, 100).width);
}

TEST(split_a_gather_or_multline_with_an_empty_line_lays_out_at_a_width) {
  const auto glyphs = [](const std::string& tex) {
    Options opt;
    opt.maxWidth = 300;
    opt.path = false;
    return check::count(check::render(tex, opt), DrawRecord::GLYPH);
  };
  CHECK_EQ(glyphs("\\begin{gather} \\\\ b \\end{gather}"), 1);
  CHECK_EQ(glyphs("\\begin{multline} a \\\\ \\\\ b \\end{multline}"), 2);
}

TEST(split_a_line_does_not_break_inside_a_sub_or_superscript) {
  Options opt(InputMode::mixed, 110);
  const auto r = check::of(check::render("aaa $x_{\\text{p q}}$zzzzzzzz", opt), DrawRecord::TEXT);
  float p = -1, q = -2, aaa = -1, zzz = -2;
  for (const auto& t : r) {
    if (t.text == "p") p = t.y;
    if (t.text == "q") q = t.y;
    if (t.text == "aaa") aaa = t.y;
    if (t.text == "zzzzzzzz") zzz = t.y;
  }
  CHECK_EQ(p, q);
  CHECK(aaa < zzz);
}
