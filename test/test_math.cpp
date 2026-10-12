// Math commands that amsmath, mathtools, KaTeX and TeX itself have, each
// checked by what it draws against the form it is defined by (engine.h).

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

Options typeface(InputMode mode = InputMode::math) {
  Options opt(mode);
  opt.path = false;
  return opt;
}

check::Layout rec(const std::string& tex, InputMode mode = InputMode::math) {
  return check::render(tex, typeface(mode));
}

bool quiet(const std::string& tex, InputMode mode = InputMode::math) { return rec(tex, mode).warnings.empty(); }

bool warnsAbout(const std::string& tex, const std::string& part, InputMode mode = InputMode::math) {
  return check::warns(rec(tex, mode), part);
}

// The x of every glyph.
std::vector<float> xs(const std::string& tex) {
  return check::column(check::of(rec(tex), DrawRecord::GLYPH), &DrawRecord::x);
}

std::string said(const std::string& tex) {
  std::string out;
  for (const auto& t : check::texts(rec(tex))) out += t;
  return out;
}

bool near(const std::vector<float>& a, const std::vector<float>& b, float tolerance = 1e-4f) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); i++) {
    if (std::fabs(a[i] - b[i]) > tolerance) return false;
  }
  return true;
}

// Every segment of the lines: its length.
std::vector<float> lengths(const Records& lines) {
  std::vector<float> out;
  for (const auto& l : lines) out.push_back(std::hypot(l.x2 - l.x1, l.y2 - l.y1));
  return out;
}

}  // namespace

// Two inputs draw the same kinds of ink in the same places.
#define SAME(a, b)                                                                               \
  do {                                                                                           \
    const auto la_ = rec(a);                                                                     \
    const auto lb_ = rec(b);                                                                     \
    if (!check::samePlaces(la_, lb_)) {                                                          \
      check::fail(__FILE__, __LINE__,                                                            \
                  std::string("not the same places:\n  ") + (a) + "\n  " + (b) + "\n" +          \
                    check::describe(la_) + check::describe(lb_));                                \
    }                                                                                            \
  } while (0)

// The same records exactly, glyphs included, in typeface mode.
#define IDENTICAL(a, b)                                                                          \
  do {                                                                                           \
    const auto la_ = rec(a);                                                                     \
    const auto lb_ = rec(b);                                                                     \
    if (!check::sameInk(la_, lb_)) {                                                             \
      check::fail(__FILE__, __LINE__,                                                            \
                  std::string("not identical:\n  ") + (a) + "\n  " + (b) + "\n" +                \
                    check::describe(la_) + check::describe(lb_));                                \
    }                                                                                            \
  } while (0)

TEST(math_mkern_mskip_mspace_and_hskip_leave_the_space_they_name) {
  const auto quad = xs(R"(a\quad b)");
  for (const char* tex : {R"(a\mkern18mu b)", R"(a\mskip18mu b)", R"(a\mspace{18mu}b)"}) {
    CHECK(quiet(tex));
    CHECK(near(xs(tex), quad));
  }
  CHECK(near(xs(R"(a\hskip1em b)"), xs(R"(a\kern1em b)")));
  CHECK(quiet(R"(a\hskip1em b)"));
  // A negative one takes the second letter back over the first.
  CHECK(xs(R"(a\mkern-6mu b)")[1] < xs(R"(a b)")[1]);
}

TEST(math_dcases_sets_its_cells_in_display_style) {
  const std::string body = R"(\sum_{i=1}^n x_i & y)";
  const auto height = [&](const char* env) {
    return rec(std::string(R"(\begin{)") + env + "}" + body + R"(\end{)" + env + "}").height;
  };
  CHECK(quiet(R"(\begin{dcases})" + body + R"(\end{dcases})"));
  // Limits above and below the sum, not at its side.
  CHECK(height("dcases") > height("cases"));
}

TEST(math_subarray_is_a_script_style_array_as_substack_is) {
  const std::string sub = R"(\sum_{\begin{subarray}{c} i<n \\ j>0 \end{subarray}} x)";
  const std::string stack = R"(\sum_{\substack{ i<n \\ j>0 }} x)";
  CHECK(quiet(sub));
  IDENTICAL(sub, stack);
  // {l}: the rows start at the same place.
  const auto g = check::of(rec(R"(\begin{subarray}{l} abc \\ d \end{subarray})"), DrawRecord::GLYPH);
  CHECK(g.size() >= 4);
  CHECK(std::fabs(g[0].x - g[3].x) < 1e-4f);
}

TEST(math_shortintertext_is_intertext_in_the_same_slot) {
  const auto mk = [](const char* cmd) {
    return std::string(R"(\begin{align} a&=b \)") + cmd + R"({so} c&=d \end{align})";
  };
  CHECK(quiet(mk("shortintertext")));
  IDENTICAL(mk("shortintertext"), mk("intertext"));
}

TEST(math_bgroup_and_egroup_are_the_braces_of_a_group) {
  CHECK(quiet(R"(a\bgroup b\egroup c)"));
  IDENTICAL(R"(a\bgroup b\egroup c)", R"(a{b}c)");
  IDENTICAL(R"(x^\bgroup ab\egroup)", R"(x^{ab})");
}

TEST(math_textsl_and_slshape_are_the_italic_the_font_has) {
  CHECK(quiet(R"(\textsl{ab})"));
  CHECK(quiet(R"(\text{\slshape ab})"));
  IDENTICAL(R"(\textsl{ab})", R"(\textit{ab})");
  IDENTICAL(R"(\text{\slshape ab})", R"(\text{\itshape ab})");
}

TEST(math_moveeqleft_moves_the_first_line_left_by_the_ems_it_is_given) {
  // The distance from the first line's start to where the second line's "=" is.
  const auto gap = [](const char* option) {
    const std::string tex = std::string(R"(\begin{align}\MoveEqLeft)") + option + R"( a+b \\ &= c \end{align})";
    CHECK(quiet(tex));
    const auto x = xs(tex);
    return x.size() == 5 ? x[3] - x[0] : -1.f;
  };
  const float none = gap("[0]"), two = gap(""), four = gap("[4]");
  CHECK(std::fabs(none) < 1e-3f);
  CHECK(two > 1.f);
  // 2em is the default, and the move is in proportion to the number.
  CHECK(std::fabs((four - two) - (two - none)) < 1e-3f);
}

TEST(math_cancelto_strikes_its_base_with_an_arrow_and_sets_the_value_at_the_tip) {
  CHECK(quiet(R"(\cancelto{0}{x})"));
  const auto r = rec(R"(\cancelto{0}{x})");
  const auto plain = rec(R"(\cancel{x})");
  // The shaft and two strokes of the head.
  CHECK_EQ(check::count(r, DrawRecord::LINE), check::count(plain, DrawRecord::LINE) + 2);
  const auto g = check::of(r, DrawRecord::GLYPH);
  CHECK_EQ(g.size(), std::size_t(2));
  // The value is to the right of the base and above it.
  CHECK(g[1].x > g[0].x);
  CHECK(g[1].y < g[0].y);
}

TEST(math_declarepaireddelimiter_defines_the_plain_starred_and_sized_forms) {
  const std::string decl = R"(\DeclarePairedDelimiter{\abs}{\lvert}{\rvert})";
  CHECK(quiet(decl + R"(\abs{x})"));
  IDENTICAL(decl + R"(\abs{x})", R"(\lvert x\rvert)");
  IDENTICAL(decl + R"(\abs*{\frac{a}{b}})", R"(\left\lvert\frac{a}{b}\right\rvert)");
  IDENTICAL(decl + R"(\abs[\big]{x})", R"(\big\lvert x\big\rvert)");
  // An existing command is not replaced.
  CHECK(warnsAbout(R"(\DeclarePairedDelimiter{\frac}{(}{)}x)", "already exists"));
}

TEST(math_leftroot_and_uproot_are_read_and_leave_the_root_as_it_is) {
  const std::string tex = R"(\sqrt[\leftroot{2}\uproot{2}3]{x})";
  CHECK(quiet(tex));
  IDENTICAL(tex, R"(\sqrt[3]{x})");
}

TEST(math_cancel_rises_to_the_right_and_bcancel_falls_as_the_cancel_package_draws_them) {
  const auto line = [](const char* tex) {
    const auto lines = check::of(rec(tex), DrawRecord::LINE);
    CHECK_EQ(lines.size(), std::size_t(1));
    return lines.empty() ? DrawRecord() : lines[0];
  };
  // y runs down the page: the slash starts low and ends high.
  const auto s = line(R"(\cancel{x})");
  CHECK(s.y1 > s.y2);
  CHECK(s.x1 < s.x2);
  const auto b = line(R"(\bcancel{x})");
  CHECK(b.y1 < b.y2);
  CHECK(b.x1 < b.x2);
}

TEST(math_a_columns_displaystyle_sets_its_cells_in_display_style) {
  const auto h = [](const std::string& spec) {
    return rec(R"(\begin{array}{)" + spec + R"(} \sum_{i=1}^n x_i \end{array})").height;
  };
  CHECK(h(R"(>{\displaystyle}l)") > h("l"));
  CHECK_EQ(h(R"(>{\displaystyle}l)"), rec(R"(\begin{array}{l} \displaystyle\sum_{i=1}^n x_i \end{array})").height);
  // \scriptscriptstyle holds "scriptstyle": it must not be read as it. (The
  // widths: an array's rows have a strut, so its heights are alike.)
  const auto w = [](const std::string& spec) {
    return rec(R"(\begin{array}{)" + spec + R"(} \sum_{i=1}^n x_i \end{array})").width;
  };
  CHECK(w(R"(>{\scriptscriptstyle}l)") < w(R"(>{\scriptstyle}l)"));
}

TEST(math_katexs_aliases_draw_what_the_commands_they_stand_for_draw) {
  const std::vector<std::pair<std::string, std::string>> pairs = {
    {R"(\rarr)", R"(\rightarrow)"}, {R"(\lArr)", R"(\Leftarrow)"},
    {R"(\harr)", R"(\leftrightarrow)"}, {R"(\Uarr)", R"(\Uparrow)"},
    {R"(\R)", R"(\mathbb{R})"}, {R"(\N)", R"(\mathbb{N})"}, {R"(\Bbb{Z})", R"(\mathbb{Z})"},
    {R"(\isin)", R"(\in)"}, {R"(\empty)", R"(\emptyset)"}, {R"(\infin)", R"(\infty)"},
    {R"(\clubs)", R"(\clubsuit)"}, {R"(\alef)", R"(\aleph)"}, {R"(\sdot)", R"(\cdot)"},
    {R"(\dotsm)", R"(\cdots)"}, {R"(\argmax_x)", R"(\operatorname*{arg\,max}_x)"},
    {R"(\bigm|)", R"(\mathrel{\big|})"}, {R"(\bra{a})", R"(\left\langle a\right|)"},
    {R"(\set{a\VERT b})", R"(\left\{ a\mathrel{|} b\right\})"}};
  // x, y and glyph of each record, rounded and sorted.
  const auto key = [](const std::string& tex) {
    std::vector<std::tuple<long, long, int>> out;
    for (const auto& r : check::ink(rec(tex))) {
      out.emplace_back(std::lround(r.x * 1000), std::lround(r.y * 1000), r.glyph_id);
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  for (const auto& p : pairs) {
    CHECK(quiet(p.first));
    CHECK(key(p.first) == key(p.second));
  }
}

TEST(math_the_text_symbols_and_letters_katex_has_are_text) {
  CHECK_EQ(said(R"(\text{\textbackslash\textbar\textdollar\textunderscore})"), std::string("\\|$_"));
  CHECK_EQ(said(R"(\text{\AE\oe\ss\o})"), std::string("Æœßø"));
  CHECK_EQ(said(R"(\text{\textendash\textemdash\textdegree})"), std::string("–—°"));
  CHECK(quiet(R"(\text{\textmd{a}\textup{b}})"));
}

TEST(math_a_document_may_define_a_name_katex_has_as_its_own) {
  CHECK(quiet(R"(\newcommand{\R}{\mathbb{R}}\newcommand{\set}[1]{\{#1\}} \R \set{a})"));
  CHECK(quiet(R"(\renewcommand{\N}{n}\N)"));
  // Its own definition is the one used.
  CHECK_EQ(check::count(rec(R"(\newcommand{\bra}[1]{[#1]} \bra{a})"), DrawRecord::LINE), 0);
  // A name LaTeX itself has is still taken.
  CHECK(warnsAbout(R"(\newcommand{\frac}{x})", "already exists"));
}

TEST(math_a_column_specs_colon_and_hdashline_are_dashed_rules) {
  const auto small = [](const check::Layout& l) {
    int n = 0;
    for (const float len : lengths(check::of(l, DrawRecord::LINE))) n += len < 8 ? 1 : 0;
    return n;
  };
  const auto plain = rec(R"(\begin{array}{cc} a & b \\ c & d \end{array})");
  const auto colon = rec(R"(\begin{array}{c:c} a & b \\ c & d \end{array})");
  CHECK(quiet(R"(\begin{array}{c:c} a & b \\ c & d \end{array})"));
  CHECK_EQ(check::count(plain, DrawRecord::LINE), 0);
  CHECK(small(colon) > 2);
  // Between the cells, level with them.
  float leftmost = 1e9f;
  for (const auto& g : check::of(colon, DrawRecord::GLYPH)) leftmost = std::min(leftmost, g.x);
  const auto v = check::of(colon, DrawRecord::LINE);
  for (const auto& l : v) {
    CHECK(std::fabs(l.x1 - l.x2) < 1e-3f);
    CHECK(l.x1 > leftmost);
  }
  const auto dashed = rec(R"(\begin{array}{cc} a & b \\ \hdashline c & d \end{array})");
  const auto solid = rec(R"(\begin{array}{cc} a & b \\ \hline c & d \end{array})");
  CHECK_EQ(check::count(solid, DrawRecord::LINE), 1);
  CHECK(check::count(dashed, DrawRecord::LINE) > 3);
  for (const auto& l : check::of(dashed, DrawRecord::LINE)) CHECK(std::fabs(l.y1 - l.y2) < 1e-3f);
}

// --- What KaTeX has that LaTeX's own commands did not give us ---------------

TEST(math_mathchoice_draws_the_choice_for_the_style_it_is_set_in) {
  SAME(R"(\displaystyle\mathchoice{a}{b}{c}{d})", R"(\displaystyle a)");
  SAME(R"(\textstyle\mathchoice{a}{b}{c}{d})", R"(\textstyle b)");
  SAME(R"(x^{\mathchoice{a}{b}{c}{d}})", R"(x^{c})");
  SAME(R"(x^{y^{\mathchoice{a}{b}{c}{d}}})", R"(x^{y^{d}})");
  CHECK(quiet(R"(\mathchoice{a}{b}{c}{d})"));
}

TEST(math_vcenter_puts_the_middle_of_its_box_on_the_math_axis) {
  // A rule is a line as thick as it is tall. A fraction's bar is on the axis,
  // so the rule's middle is on the bar's.
  const auto l = rec(R"(\frac{a}{b}\vcenter{\rule{1pt}{2em}})");
  const auto lines = check::of(l, DrawRecord::LINE);
  CHECK_EQ(lines.size(), std::size_t(2));
  const auto& thin = lines[0].line_width < lines[1].line_width ? lines[0] : lines[1];
  const auto& thick = lines[0].line_width < lines[1].line_width ? lines[1] : lines[0];
  CHECK(std::fabs(thick.y1 - thin.y1) < 1e-3f);
  // And the box that holds it is as tall as the rule (a render's height is
  // the whole box, its depth what is below the baseline), the baseline the
  // axis below the middle: less than half of it is below.
  const auto alone = rec(R"(\vcenter{\rule{1pt}{2em}})");
  CHECK_EQ(alone.height, 40);
  CHECK(alone.depth < 20);
  CHECK(alone.depth > 0);
  CHECK(quiet(R"(\vcenter{x})"));
}

TEST(math_expandafter_noexpand_edef_xdef_and_futurelet_work_as_texs_do) {
  SAME(R"(\def\f#1{[#1]}\def\g{abc}\expandafter\f\g)", "[a]bc");
  SAME(R"(\def\a{1}\edef\b{\a 2}\def\a{9}\b)", "12");
  SAME(R"(\def\a{1}\edef\b{\noexpand\a 2}\def\a{9}\b)", "92");
  SAME(R"({\def\a{1}\xdef\b{\a 2}}\b)", "12");
  SAME(R"(\futurelet\q\mathrm x\q)", R"(\mathrm{x}x)");
  for (const char* tex : {R"(\expandafter\def\csname)", R"(\def\a{1}\edef\b{\a 2}\b)"}) {
    for (const auto& w : rec(tex).warnings) {
      CHECK(w.find("expandafter") == std::string::npos && w.find("edef") == std::string::npos);
    }
  }
}

TEST(math_the_arrows_with_no_stretching_glyph_are_drawn_as_long_as_their_text) {
  const auto span = [](const std::string& tex) {
    float lo = 1e9f, hi = -1e9f;
    for (const auto& l : check::of(rec(tex), DrawRecord::LINE)) {
      lo = std::min({lo, l.x1, l.x2});
      hi = std::max({hi, l.x1, l.x2});
    }
    return hi - lo;
  };
  for (const char* name : {"xtwoheadrightarrow", "xtwoheadleftarrow", "xlongequal", "xtofrom"}) {
    const std::string shorter = std::string("\\") + name + "{a}";
    const std::string longer = std::string("\\") + name + "{abcdefghijkl}";
    CHECK(quiet(shorter));
    CHECK(span(longer) > span(shorter) + 20);
  }
  // Two heads, and two lines for the equal sign.
  const auto strokes = [](const std::string& tex) { return check::count(rec(tex), DrawRecord::LINE); };
  CHECK(strokes(R"(\xtwoheadrightarrow{abc})") > strokes(R"(\xrightarrow{abc})"));
  CHECK(strokes(R"(\xlongequal{abc})") >= 2);
}

TEST(math_the_enclosures_katex_has_are_drawn_around_their_text) {
  const auto lines = [](const std::string& tex) { return check::of(rec(tex), DrawRecord::LINE); };
  const auto glyphY = [](const std::string& tex) {
    return check::column(check::of(rec(tex), DrawRecord::GLYPH), &DrawRecord::y);
  };
  const auto top = [](const Records& ls) {
    float t = 1e9f;
    for (const auto& l : ls) t = std::min({t, l.y1, l.y2});
    return t;
  };
  const auto bottom = [](const Records& ls) {
    float b = -1e9f;
    for (const auto& l : ls) b = std::max({b, l.y1, l.y2});
    return b;
  };
  // A segment: a bar over (under) with a tick at each end.
  const auto over = lines(R"(\overlinesegment{AB})");
  CHECK_EQ(over.size(), std::size_t(3));
  CHECK(top(over) < check::minimum(glyphY("AB")) - 5);
  const auto under = lines(R"(\underlinesegment{AB})");
  CHECK_EQ(under.size(), std::size_t(3));
  CHECK(bottom(under) > check::maximum(glyphY("AB")));
  // \angl is a bar over and one at the right; \angln is \angl n.
  CHECK_EQ(lines(R"(\angl{n})").size(), std::size_t(2));
  SAME(R"(\angln)", R"({\angl n})");
  // \phase: a slanting stroke and the line under.
  const auto phase = lines(R"(\phase{30^\circ})");
  CHECK_EQ(phase.size(), std::size_t(2));
  bool slanting = false;
  for (const auto& l : phase) slanting = slanting || (std::fabs(l.x1 - l.x2) > 1 && std::fabs(l.y1 - l.y2) > 1);
  CHECK(slanting);
  // A circle round the letter: its strokes close on themselves.
  const auto circle = lines(R"(\textcircled{a})");
  CHECK(circle.size() > 20);
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  for (const auto& l : circle) {
    x0 = std::min({x0, l.x1, l.x2});
    x1 = std::max({x1, l.x1, l.x2});
    y0 = std::min({y0, l.y1, l.y2});
    y1 = std::max({y1, l.y1, l.y2});
  }
  CHECK(std::fabs((x1 - x0) - (y1 - y0)) <= 0.2f * (y1 - y0));
  for (const char* tex : {R"(\text{\textcircled{a}})", R"(\text{\H{o}})", R"(\widecheck{ac})",
                          R"(\overgroup{AB})", R"(\undergroup{AB})"}) {
    CHECK(quiet(tex));
  }
}

TEST(math_ce_reads_formulas_charges_coefficients_states_bonds_and_arrows) {
  SAME(R"(\ce{H2O})", R"(\mathrm{H}_{2}\mathrm{O})");
  SAME(R"(\ce{SO4^2-})", R"(\mathrm{S}\mathrm{O}_{4}^{2-})");
  SAME(R"(\ce{Fe^{3+}})", R"(\mathrm{Fe}^{3+})");
  SAME(R"(\ce{Na+})", R"(\mathrm{Na}^{+})");
  SAME(R"(\ce{2H2 + O2 -> 2H2O})", R"(2\,\mathrm{H}_{2}{}+{}\mathrm{O}_{2}\longrightarrow2\,\mathrm{H}_{2}\mathrm{O})");
  SAME(R"(\ce{A ->[H2O] B})", R"(\mathrm{A}\xrightarrow{\mathrm{H}_{2}\mathrm{O}}\mathrm{B})");
  SAME(R"(\ce{A <=>[a][b] B})", R"(\mathrm{A}\xrightleftharpoons[\mathrm{b}]{\mathrm{a}}\mathrm{B})");
  SAME(R"(\ce{H2O(l)})", R"(\mathrm{H}_{2}\mathrm{O}\mathrm{(l)})");
  SAME(R"(\ce{1/2 H2O})", R"(\frac{1}{2}\,\mathrm{H}_{2}\mathrm{O})");
  SAME(R"(\ce{C6H5-CHO})", R"(\mathrm{C}_{6}\mathrm{H}_{5}{-}\mathrm{C}\mathrm{H}\mathrm{O})");
  SAME(R"(\ce{^{227}_{90}Th})", R"({}^{227}_{90}\mathrm{Th})");
  SAME(R"(\ce{BaSO4 v})", R"(\mathrm{Ba}\mathrm{S}\mathrm{O}_{4}\downarrow)");
  CHECK(quiet(R"(\ce{CO2 + C -> 2 CO})"));
}

TEST(math_pu_sets_a_number_and_a_unit) {
  SAME(R"(\pu{1.5 mol/L})", R"(1.5\,\mathrm{mol}{/}\mathrm{L})");
  SAME(R"(\pu{12 mol L-1})", R"(12\,\mathrm{mol}\,\mathrm{L}^{-1})");
  CHECK(quiet(R"(\pu{123 kJ/mol})"));
}

TEST(math_arraystretch_spaces_the_rows_of_the_array_it_is_set_before) {
  const auto pitch = [](const std::string& tex) {
    const auto ys = check::uniqueSorted(check::column(check::of(rec(tex), DrawRecord::GLYPH), &DrawRecord::y), 3);
    return ys.size() < 2 ? 0.0 : ys[1] - ys[0];
  };
  const double base = pitch(R"(\begin{array}{c} a \\ b \end{array})");
  // Each step of the stretch is a baselineskip (1.2em).
  const double stretched = pitch(R"(\def\arraystretch{2}\begin{array}{c} a \\ b \end{array})");
  const double em = check::of(rec("a"), DrawRecord::GLYPH)[0].font_size;
  CHECK(std::fabs((stretched - base) - 1.2 * em) <= 0.05 * 1.2 * em);
  CHECK(stretched > base);
  // It is read at the array, and ends with the group it was set in.
  CHECK_EQ(pitch(R"({\renewcommand{\arraystretch}{3}}\begin{array}{c} a \\ b \end{array})"), base);
  // Matrices are arrays too.
  CHECK(pitch(R"(\renewcommand{\arraystretch}{2}\begin{pmatrix} a \\ b \end{pmatrix})") >
        pitch(R"(\begin{pmatrix} a \\ b \end{pmatrix})"));
}

TEST(math_global_makes_the_definition_after_it_last_past_its_group) {
  SAME(R"({\global\def\a{1}}\a)", "1");
  SAME(R"({\def\z{9}\global\edef\a{\z}}\a)", "9");
  SAME(R"({\global\let\a=x}\a)", "x");
  SAME(R"({\global\futurelet\q\mathrm x\q}\q)", R"(\mathrm{x}xx)");
  // Without \global the group takes it away again.
  CHECK(warnsAbout(R"({\def\a{1}}\a)", "unknown command"));
  CHECK(quiet(R"({\global\def\a{1}}\a)"));
  // It is for the next assignment only: another command ends it.
  CHECK(warnsAbout(R"(\global\newcommand{\a}{1}{\def\y{2}}\y)", "unknown command \\y", InputMode::mixed));
}

TEST(math_ifstar_ifnextchar_and_firstoftwo_are_names_without_makeatletter_as_in_katex) {
  SAME(R"(\def\f{\@ifstar{S}{N}}\f*)", "S");
  SAME(R"(\def\f{\@ifstar{S}{N}}\f x)", "Nx");
  SAME(R"(\def\f{\@ifnextchar[{Y}{N}}\f[)", "Y[");
  SAME(R"(\def\f{\@ifnextchar[{Y}{N}}\f x)", "Nx");
  SAME(R"(\@firstoftwo{a}{b})", "a");
  SAME(R"(\@secondoftwo{a}{b})", "b");
}

TEST(math_textormath_sixptsize_strut_and_the_text_symbols_are_read) {
  SAME(R"(\TextOrMath{t}{m})", "m");
  SAME(R"(\text{\TextOrMath{t}{m}})", R"(\text{t})");
  const auto size = [](const std::string& tex) { return check::ink(rec(tex))[0].font_size; };
  CHECK(std::fabs(size(R"(\sixptsize x)") / size("x") - 0.6f) < 1e-3f * 0.6f + 1e-3f);
  CHECK(quiet(R"(a\strut b\ordinarycolon)"));
  CHECK(quiet(R"(\text{\textsection\textparagraph\textcopyright\texttrademark\textquotedbl})"));
}

TEST(math_overrightarrow_overleftharpoon_and_overrightharpoon_are_drawn_over_their_text) {
  for (const char* tex : {R"(\Overrightarrow{AB})", R"(\overleftharpoon{AB})", R"(\overrightharpoon{AB})"}) {
    const auto l = rec(tex);
    CHECK(l.warnings.empty());
    const auto lines = check::of(l, DrawRecord::LINE);
    const auto glyphs = check::of(l, DrawRecord::GLYPH);
    const float highest = check::minimum(check::column(glyphs, &DrawRecord::y));
    // Over the letters, and as wide as they are.
    float lo = 1e9f, hi = -1e9f;
    for (const auto& s : lines) {
      CHECK(s.y1 < highest - 5 && s.y2 < highest - 5);
      lo = std::min({lo, s.x1, s.x2});
      hi = std::max({hi, s.x1, s.x2});
    }
    const auto gx = check::column(glyphs, &DrawRecord::x);
    CHECK(hi - lo >= check::maximum(gx) - check::minimum(gx));
  }
  // The double arrow has two shafts.
  const auto longOnes = [](const std::string& tex) {
    int n = 0;
    for (const float len : lengths(check::of(rec(tex), DrawRecord::LINE))) n += len > 5 ? 1 : 0;
    return n;
  };
  CHECK_EQ(longOnes(R"(\Overrightarrow{AB})"), 2);
  CHECK_EQ(longOnes(R"(\overrightharpoon{AB})"), 1);
}

// --- Symbols that drew wrong against LaTeX's own ------------------------------

TEST(math_utilde_sets_a_tilde_under_all_of_its_argument) {
  const auto g = check::of(rec(R"(\utilde{ABCD})"), DrawRecord::GLYPH);
  CHECK_EQ(g.size(), std::size_t(5));
  const float biggest = check::maximum(check::column(g, &DrawRecord::font_size));
  Records letters, tilde;
  for (const auto& x : g) (x.font_size == biggest ? letters : tilde).push_back(x);
  CHECK_EQ(tilde.size(), std::size_t(1));
  // Below the baseline and under the letters, not an accent drawn alone before them.
  CHECK(tilde[0].y > check::maximum(check::column(letters, &DrawRecord::y)) + 3);
  CHECK(tilde[0].x >= check::minimum(check::column(letters, &DrawRecord::x)));
  CHECK(tilde[0].x <= check::maximum(check::column(letters, &DrawRecord::x)));
  CHECK(quiet(R"(\utilde{ABCD})"));
}

TEST(math_a_slash_is_an_ordinary_character_as_in_tex) { SAME("a/b", "a{/}b"); }

TEST(math_copyright_is_a_copyright_sign_and_surd_is_drawn) {
  SAME(R"(\copyright)", R"(\mathord{©})");
  CHECK(!check::sameInk(rec(R"(\copyright)"), rec(R"(\circledR)")));
  CHECK_EQ(check::count(rec(R"(\surd)"), DrawRecord::GLYPH), 1);
}

TEST(math_pod_pmod_and_mod_space_as_amsmath_does) {
  SAME(R"(\textstyle x\pod a)", R"(\textstyle x\mkern8mu(a))");
  SAME(R"(\displaystyle x\pod a)", R"(\displaystyle x\mkern18mu(a))");
  SAME(R"(\textstyle x\pmod a)", R"(\textstyle x\mkern8mu(\mathrm{mod}\mkern6mu a))");
  SAME(R"(\textstyle x\mod a)", R"(\textstyle x\mkern12mu\mathrm{mod}\,\,a)");
}

TEST(math_boxed_sets_its_content_in_display_style) {
  SAME(R"(\textstyle\boxed{\frac ab})", R"(\textstyle\fbox{\displaystyle\frac ab})");
}

TEST(math_the_text_of_xrightarrow_and_kin_is_set_in_script_style) {
  // The font's own script scale: the size of a superscript.
  const auto script = check::of(rec("x^{a}"), DrawRecord::GLYPH);
  CHECK_EQ(script.size(), std::size_t(2));
  const double scale = script[1].font_size / script[0].font_size;
  CHECK(scale < 1);
  for (const char* cmd : {"xrightarrow", "xleftarrow", "xRightarrow", "xhookrightarrow", "xmapsto"}) {
    const auto g = check::of(rec(std::string("\\") + cmd + "{a}"), DrawRecord::GLYPH);
    const auto sorted = check::uniqueSorted(check::column(g, &DrawRecord::font_size), 4);
    CHECK_EQ(sorted.size(), std::size_t(2));
    // The arrow is at the size of the line, its text at a script's.
    CHECK(std::fabs(sorted[0] / sorted[1] - scale) < 1e-3);
  }
  const auto g = check::of(rec(R"(\xrightarrow[b]{a})"), DrawRecord::GLYPH);
  const float biggest = check::maximum(check::column(g, &DrawRecord::font_size));
  int smaller = 0;
  for (const auto& x : g) smaller += x.font_size < biggest ? 1 : 0;
  CHECK_EQ(smaller, 2);
}

TEST(math_big_to_bigg_are_about_1_2_1_8_2_4_and_3_times_the_size) {
  std::vector<double> ratio;
  for (const char* tex : {R"(\big()", R"(\Big()", R"(\bigg()", R"(\Bigg()"}) {
    ratio.push_back(static_cast<double>(rec(tex).height) / 20.0);
  }
  CHECK(std::is_sorted(ratio.begin(), ratio.end(), std::less_equal<double>()));
  // The font's delimiter variants set the sizes: TeX's, give or take a fifth.
  const double want[] = {1.2, 1.8, 2.4, 3};
  double off = 0, total = 0;
  for (int i = 0; i < 4; i++) {
    off += std::fabs(ratio[i] - want[i]);
    total += want[i];
  }
  CHECK(off / total <= 0.2);
}

TEST(math_substack_and_subarray_are_as_wide_as_their_rows_with_no_padding) {
  const auto subX = [](const std::string& tex) {
    const auto g = check::of(rec(tex), DrawRecord::GLYPH);
    const float biggest = check::maximum(check::column(g, &DrawRecord::font_size));
    float least = 1e9f;
    for (const auto& x : g) {
      if (x.font_size < biggest) least = std::min(least, x.x);
    }
    return least;
  };
  const float plain = subX(R"(\textstyle\sum_{i})");
  CHECK(std::fabs(subX(R"(\textstyle\sum_{\substack{i\\j}})") - plain) <= 0.02f * plain + 0.02f);
  CHECK(std::fabs(subX(R"(\textstyle\sum_{\begin{subarray}{c}i\\j\end{subarray}})") - plain) <= 0.02f * plain + 0.02f);
}

TEST(math_ldots_and_cdots_are_three_spaced_dots_as_texs_are) {
  // Three periods and the two thin spaces between them: about 1.1 em.
  for (const char* tex : {R"(\textstyle\ldots)", R"(\textstyle\cdots)"}) {
    CHECK(std::fabs(rec(tex).width / 20.0 - 1.1) <= 0.25 * 1.1);
  }
  CHECK(quiet(R"(\text{a\ldots b}\; x_1,\ldots,x_n)"));
}

TEST(math_a_dimension_ends_where_its_unit_does) {
  SAME(R"(x\mkern8mu(a))", R"(x\mkern8mu{}(a))");
  SAME(R"(x\kern1em[a])", R"(x\kern1em{}[a])");
  CHECK(quiet(R"(x\mkern8mu(a))"));
}

TEST(math_textsc_in_math_is_text_upright_with_its_spaces_and_eqcolon_is_eqqcolon) {
  CHECK_EQ(said(R"(\begin{array}{l}\textsc{Small caps}\end{array})"), std::string("SMALL CAPS"));
  SAME(R"(\Eqcolon)", R"(\Eqqcolon)");
}

TEST(math_models_is_a_bar_joined_to_an_equal_sign_as_latexs) {
  SAME(R"(\models)", R"(\mathrel{\mathrel{\vert}\joinrel=})");
}

// --- siunitx, as a label meets it -------------------------------------------

namespace {

// type, x, y, glyph and text of every record, rounded and sorted by x, then y:
// two spellings of the same thing draw the same set.
std::vector<std::tuple<long, long, int, int, std::string>> sortedKey(const std::string& tex) {
  std::vector<std::tuple<long, long, int, int, std::string>> out;
  for (const auto& r : check::ink(rec(tex))) {
    out.emplace_back(std::lround(r.x * 1000), std::lround(r.y * 1000), static_cast<int>(r.type), r.glyph_id, r.text);
  }
  std::sort(out.begin(), out.end());
  return out;
}

void sameSet(const char* file, int line, const std::string& a, const std::string& b) {
  if (sortedKey(a) != sortedKey(b)) check::fail(file, line, "not the same set of records:\n  " + a + "\n  " + b);
  if (!rec(a).warnings.empty()) check::fail(file, line, "warns: " + a);
}

}  // namespace

#define SAME_SET(a, b) sameSet(__FILE__, __LINE__, (a), (b))

TEST(siunitx_num_sets_digits_in_groups_an_exponent_as_a_power_of_ten_and_an_uncertainty) {
  SAME_SET(R"(\num{1234})", "1234");
  SAME_SET(R"(\num{12345})", R"(12\,345)");
  SAME_SET(R"(\num{1234567.12345})", R"(1\,234\,567.123\,45)");
  SAME_SET(R"(\num{1.5e-4})", R"(1.5\times10^{-4})");
  SAME_SET(R"(\num{2E+3})", R"(2\times10^{3})");
  SAME_SET(R"(\num{-3.2(1)})", R"({-}3.2(1))");
  SAME_SET(R"(\num{1.2+-0.3})", R"(1.2\pm0.3)");
  SAME_SET(R"(\num{1.2+-0.3e3})", R"(\left(1.2\pm0.3\right)\times10^{3})");
  SAME_SET(R"(\num{.5})", "0.5");
  SAME_SET(R"(\num{1,5})", "1{,}5");
  // In text too, as math.
  CHECK(quiet(R"(a number \num{3.5} in a label)"));
}

TEST(siunitx_num_warns_of_what_is_not_a_number_and_says_it_as_it_is) {
  CHECK(warnsAbout(R"(\num{12x4})", "not a number"));
  CHECK(warnsAbout(R"(\num{1e})", "not a number"));
}

TEST(siunitx_units_are_written_with_macros_or_as_text_prefixes_joined_to_their_unit) {
  SAME_SET(R"(\si{\meter\per\second})", R"(\mathrm{m}\,\mathrm{s}^{-1})");
  SAME_SET(R"(\si{\kilo\gram\metre\per\second\squared})", R"(\mathrm{kg}\,\mathrm{m}\,\mathrm{s}^{-2})");
  SAME_SET(R"(\unit{\micro\meter\cubed})", "\\mathrm{\u00b5m}^{3}");
  SAME_SET(R"(\si{\square\metre})", R"(\mathrm{m}^{2})");
  SAME_SET(R"(\si{m.s^{-1}})", R"(\mathrm{m}\,\mathrm{s}^{-1})");
  SAME_SET(R"(\si{kg.m/s^2})", R"(\mathrm{kg}\,\mathrm{m}\,\mathrm{s}^{-2})");
  SAME_SET(R"(\si{\metre\of{eff}})", R"(\mathrm{m}_{\mathrm{eff}})");
  SAME_SET(R"(\si{\ohm})", R"(\mathrm{\Omega})");
  CHECK(warnsAbout(R"(\si{\wibble})", "unknown unit"));
}

TEST(siunitx_si_and_qty_are_a_number_a_thin_space_and_a_unit) {
  SAME_SET(R"(\SI{9.81}{\meter\per\second\squared})", R"(9.81\,\mathrm{m}\,\mathrm{s}^{-2})");
  SAME_SET(R"(\qty{1.5}{\kilo\gram})", R"(1.5\,\mathrm{kg})");
  SAME_SET(R"(\SI{3e8}{m/s})", R"(3\times10^{8}\,\mathrm{m}\,\mathrm{s}^{-1})");
  SAME_SET(R"(\SI[round-mode=places]{12345}{\hertz})", R"(12\,345\,\mathrm{Hz})");
  SAME_SET(R"(\SI{20}{\degreeCelsius})", R"(20\,\mathrm{{}^{\circ}C})");
}

TEST(siunitx_angles_ranges_and_lists) {
  SAME_SET(R"(\ang{12.5})", R"(12.5{}^{\circ})");
  SAME_SET(R"(\ang{1;2;3})", R"(1{}^{\circ}2{}^{\prime}3{}^{\prime\prime})");
  SAME_SET(R"(\ang{;;30})", R"(30{}^{\prime\prime})");
  SAME_SET(R"(\numrange{1}{10})", R"(1\text{ to }10)");
  SAME_SET(R"(\SIrange{1}{5}{\volt})", R"(1\,\mathrm{V}\text{ to }5\,\mathrm{V})");
  SAME_SET(R"(\numlist{1;2;3})", R"(1\text{, }2\text{, and }3)");
  SAME_SET(R"(\numlist{1;2})", R"(1\text{ and }2)");
  SAME_SET(R"(\SIlist{1;2}{\meter})", R"(1\,\mathrm{m}\text{ and }2\,\mathrm{m})");
}

TEST(siunitx_declaresiunit_makes_a_unit_and_a_documents_own_num_or_si_wins) {
  SAME_SET(R"(\DeclareSIUnit{\foo}{bar}\si{\foo\per\second})", R"(\mathrm{bar}\,\mathrm{s}^{-1})");
  SAME_SET(R"(\newcommand{\num}[1]{[#1]} \num{12345})", "[12345]");
  SAME_SET(R"(\renewcommand{\si}[1]{(#1)} \si{x})", "(x)");
  // \sisetup and options are read and leave nothing.
  SAME_SET(R"(\sisetup{per-mode=symbol} \num{12})", "12");
}

// --- the rows of an alignment, as TeX sets them ---------------------------------

namespace {

// The baselines of the rows of `tex`, top to bottom: the distinct y of its
// glyphs. The text is 20 px (Options::textSize), so an em is 20.
std::vector<float> baselines(const std::string& tex, Options opt = typeface()) {
  auto ys = check::column(check::of(check::render(tex, opt), DrawRecord::GLYPH), &DrawRecord::y);
  std::sort(ys.begin(), ys.end());
  std::vector<float> rows;
  for (const float y : ys) {
    if (rows.empty() || y - rows.back() > 0.05f) rows.push_back(y);
  }
  return rows;
}

// The pitch of the two rows of `env` set with `rows` (a body that has two).
float pitch(const std::string& env, const std::string& rows, Options opt = typeface(),
            const std::string& args = "") {
  const auto b = baselines("\\begin{" + env + "}" + args + rows + "\\end{" + env + "}", opt);
  return b.size() == 2 ? b[1] - b[0] : -1.f;
}

}  // namespace

TEST(math_the_rows_of_an_array_or_matrix_are_a_baselineskip_apart_whatever_they_hold) {
  // TeX's strut: 0.7 and 0.3 of the 1.2em baselineskip, in every row.
  for (const char* rows : {R"(x \\ x)", R"(y \\ y)", R"(x \\ y)"}) {
    CHECK(std::fabs(pitch("matrix", rows) - 24.f) < 0.05);
    CHECK(std::fabs(pitch("array", rows, typeface(), "{c}") - 24.f) < 0.05);
  }
  // cases (\arraystretch 1.2): its brace is a glyph of its own, so the rows
  // are those of the glyphs right of it.
  const auto cases = check::of(rec(R"(\begin{cases} x \\ x \end{cases})"), DrawRecord::GLYPH);
  CHECK_EQ(cases.size(), std::size_t(3));
  if (cases.size() == 3) CHECK(std::fabs(cases[2].y - cases[1].y - 28.8f) < 0.05);
  // \arraystretch is a multiplier of the baselineskip.
  const auto stretched = baselines(R"(\renewcommand{\arraystretch}{1.5}\begin{matrix} x \\ x \end{matrix})");
  CHECK_EQ(stretched.size(), std::size_t(2));
  if (stretched.size() == 2) CHECK(std::fabs(stretched[1] - stretched[0] - 36.f) < 0.05);
  // smallmatrix: a baselineskip of 6pt, which is 0.6em.
  CHECK(std::fabs(pitch("smallmatrix", R"(x \\ x)") - 12.f) < 0.05);
  // \substack: the font's stack constants in a script (0.56em in Latin
  // Modern, 0.67em in Fira Math), well under a matrix's 1.2em.
  const auto stack = baselines(R"(\substack{x \\ x})");
  CHECK_EQ(stack.size(), std::size_t(2));
  if (stack.size() == 2) CHECK(stack[1] - stack[0] > 8.f && stack[1] - stack[0] < 16.f);
}

TEST(math_the_rows_of_amsmaths_alignments_are_a_baselineskip_and_a_jot_apart) {
  // 1.2em + 3pt (0.3em) = 1.5em, for rows that fit in it.
  for (const char* env : {"aligned", "gathered", "align*", "gather*", "flalign*", "split"}) {
    for (const char* rows : {R"(x \\ x)", R"(y \\ y)"}) {
      CHECK(std::fabs(pitch(env, rows) - 30.f) < 0.05);
    }
  }
  CHECK(std::fabs(pitch("alignat*", R"(x & x \\ x & x)", typeface(), "{1}") - 30.f) < 0.05);
  CHECK(std::fabs(pitch("eqnarray*", R"(x&&x \\ x&&x)") - 30.f) < 0.05);
  // At a width, in a document: the lines of gather and multline.
  Options doc(InputMode::document, 300);
  doc.path = false;
  CHECK(std::fabs(pitch("gather*", R"(x \\ x)", doc) - 30.f) < 0.05);
  CHECK(std::fabs(pitch("multline*", R"(x \\ x)", doc) - 30.f) < 0.05);
}

TEST(math_rows_too_tall_for_the_baselineskip_stand_a_lineskip_and_a_jot_apart) {
  // 1pt + 3pt = 0.4em between the rows (amsmath), and nothing in an array.
  const auto height = [](const std::string& tex) { return rec(tex).height; };
  const std::string frac = R"(\displaystyle\frac{x}{x})";
  const int one = height("\\begin{aligned}" + frac + "\\end{aligned}");
  const int two = height("\\begin{aligned}" + frac + "\\\\" + frac + "\\end{aligned}");
  CHECK(std::fabs(two - (2 * one + 8)) <= 1.5);
  const int onea = height("\\begin{matrix}" + frac + "\\end{matrix}");
  const int twoa = height("\\begin{matrix}" + frac + "\\\\" + frac + "\\end{matrix}");
  CHECK(std::fabs(twoa - 2 * onea) <= 1.5);
}

TEST(math_the_cells_of_an_array_are_in_text_style_and_those_of_an_alignment_in_display_style) {
  Options display = typeface();
  display.display = true;
  const auto height = [&](const std::string& env) {
    return check::render("\\begin{" + env + "}\\frac{x}{x}\\end{" + env + "}", display).height;
  };
  // TeX's `$...$` in a cell begins in text style; aligned's cells are display.
  CHECK(height("matrix") + 4 <= height("aligned"));
  CHECK(check::render(R"(\begin{array}{c}\frac{x}{x}\end{array})", display).height + 4 <= height("gathered"));
  // A column asks for display style as before.
  CHECK_EQ(check::render(R"(\begin{array}{>{\displaystyle}c}\frac{x}{x}\end{array})", display).height, height("aligned"));
}
