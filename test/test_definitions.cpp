// Macros a label defines and the commands it redefines, what is drawn for
// input the engine cannot make sense of, and degenerate sizes (engine.h):
// a label's definitions are its own, and bad input is drawn as far as it can be.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"
#include "front/expander.h"

using check::Options;
using check::Pairs;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

check::Layout rec(const std::string& tex, InputMode mode = InputMode::math, bool path = true) {
  Options opt(mode);
  opt.path = path;
  return check::render(tex, opt);
}

bool hasRule(const std::string& tex) { return check::count(rec(tex), DrawRecord::LINE) > 0; }

bool has(const Strings& all, const std::string& s) {
  for (const auto& x : all) {
    if (x == s) return true;
  }
  return false;
}

}  // namespace

TEST(definitions_empty_input_is_a_zero_size_layout) {
  const auto l = rec("", InputMode::mixed);
  CHECK_EQ(l.width, 0);
  CHECK_EQ(l.height, 0);
  CHECK_EQ(l.records.size(), std::size_t(0));
}

TEST(definitions_an_unknown_command_is_set_as_its_own_name_not_dropped_and_warns) {
  const auto l = rec("\\notavalidcommand{x}");
  CHECK(check::warns(l, "1:1: unknown command \\notavalidcommand"));
  CHECK(l.records.size() >= std::string("notavalidcommand").size());
  CHECK(l.width > 0);
}

TEST(definitions_a_formula_that_draws_nothing_has_a_finite_baseline) {
  for (const char* tex : {"", "\\phantom{}", "\\resizebox{0pt}{0pt}{x}", "\\end{x}"}) {
    CHECK_EQ(rec(tex, InputMode::mixed).baseline, 0.0f);
  }
}

TEST(definitions_an_absurdly_tall_delimiter_is_capped_not_built_piece_by_piece) {
  const auto l = rec("\\left(\\rule{1pt}{1e8pt}\\right)", InputMode::mixed);
  CHECK(l.height > 0);
}

TEST(definitions_def_defines_a_zero_argument_macro_and_draws_as_newcommand_does) {
  CHECK_EQ(rec("\\def\\mymacroA{x^2} \\mymacroA").records.size(),
           rec("\\newcommand{\\mymacroB}{x^2} \\mymacroB").records.size());
}

TEST(definitions_def_silently_overwrites_an_existing_macro_and_the_last_wins) {
  const std::string got = "\\def\\myoverwrite{x} \\def\\myoverwrite{y} \\myoverwrite";
  CHECK_PAIRS((Pairs{{got, "y"}}));
  CHECK(!check::sameInk(rec(got), rec("x")));
}

TEST(definitions_def_with_sequential_parameters_expands_like_newcommand) {
  CHECK_EQ(rec("\\def\\sqA#1{#1^2} \\sqA{a}").records.size(), rec("\\newcommand{\\sqB}[1]{#1^2} \\sqB{a}").records.size());
  CHECK_EQ(rec("\\def\\pairA#1#2{#1+#2} \\pairA{a}{b}").records.size(),
           rec("\\newcommand{\\pairB}[2]{#1+#2} \\pairB{a}{b}").records.size());
}

TEST(definitions_a_def_with_an_invalid_name_warns_and_is_dropped_whole) {
  const auto l = rec("\\def{notacontrolseq}{body}x");
  CHECK(check::warns(l, "expected '\\' before the name"));
  // Nothing of the definition is drawn, only what follows it.
  CHECK_EQ(l.records.size(), std::size_t(1));
}

TEST(definitions_def_rejects_non_sequential_or_malformed_parameter_patterns) {
  // Each warns and is dropped whole, so only the trailing `y` is drawn.
  const std::vector<std::pair<std::string, std::string>> rejected = {
    {"\\def\\bad#2#1{#1#2}y", "parameters must be sequential"},   // #2 before #1
    {"\\def\\skip#1#3{#1#3}y", "parameters must be sequential"},  // #1 then #3
    {"\\def\\noarg#x{x}y", "'#' must be followed by a digit"},    // no digit
  };
  for (const auto& r : rejected) {
    const auto l = rec(r.first);
    CHECK(check::warns(l, r.second));
    CHECK_EQ(l.records.size(), std::size_t(1));
  }
}

TEST(definitions_an_error_inside_a_commands_argument_is_reported_not_swallowed) {
  // It warns, and the broken command is drawn as its name, in red.
  for (const char* tex : {"\\text{a \\begin{array}{q}x\\end{array} b}", "\\frac{1}{\\begin{array}{q}x\\end{array}} + y",
                          "\\sqrt{\\begin{array}{q}x\\end{array}}", "a \\begin{array}{q}x\\end{array} b"}) {
    const auto l = rec(tex);
    CHECK(check::warns(l, "Invalid alignment"));
    CHECK(check::drawsColour(l, check::kRed));
  }
  // Only the bad part is lost: the text around it survives.
  const auto around = check::texts(rec("\\text{a \\begin{array}{q}x\\end{array} b}"));
  CHECK(has(around, "a ") && has(around, " b"));
  // An unknown command in an argument is drawn in red; the space after it
  // goes, as TeX drops a space after a control word.
  const auto unknown = rec("\\text{a \\nosuchcmd b}");
  CHECK(check::warns(unknown, "unknown command \\nosuchcmd"));
  CHECK(has(check::texts(unknown), "a ") && has(check::texts(unknown), "b"));
  CHECK(check::drawsColour(unknown, check::kRed));
}

TEST(definitions_redefining_a_built_in_lasts_one_label_and_never_breaks_the_next) {
  CHECK(hasRule("\\frac{1}{2}"));
  // As in LaTeX, \newcommand refuses a name that exists.
  const auto refused = rec("\\newcommand{\\frac}{Q}\\frac{1}{2}");
  CHECK(check::warns(refused, "already exists"));
  CHECK(check::count(refused, DrawRecord::LINE) > 0);
  CHECK(hasRule("\\frac{1}{2}"));
  // \renewcommand and \def may redefine a built-in, for that label only.
  CHECK(has(check::texts(rec("\\renewcommand{\\frac}{\\text{R}}\\frac")), "R"));
  CHECK(hasRule("\\frac{1}{2}"));
  CHECK(has(check::texts(rec("\\def\\sqrt{\\text{D}}\\sqrt")), "D"));
  CHECK(!has(check::texts(rec("\\sqrt{x}")), "D"));
  // A built-in defined in LaTeX source (\degree) comes back too.
  CHECK(has(check::texts(rec("\\renewcommand{\\degree}{\\text{G}}\\degree")), "G"));
  CHECK(!has(check::texts(rec("90\\degree")), "G"));
  // \renewcommand of something undefined warns and defines it, as LaTeX does.
  const auto undefined = rec("\\renewcommand{\\nosuch}{\\text{Q}}\\nosuch");
  CHECK(check::warns(undefined, "\\nosuch was not defined; defined now"));
  CHECK(has(check::texts(undefined), "Q"));
}

TEST(definitions_a_macro_that_expands_to_itself_is_a_parse_error_not_a_hang) {
  for (const char* tex : {"\\def\\gmloop{\\gmloop}\\gmloop", "\\def\\gmping{\\gmpong}\\def\\gmpong{\\gmping}\\gmping",
                          "\\def\\gmgrow{x\\gmgrow}\\gmgrow"}) {
    CHECK_THROWS(rec(tex), "macro expansions");
  }
}

TEST(definitions_an_unterminated_double_dollar_at_the_end_of_text_mode_lays_out_as_empty) {
  CHECK_EQ(rec("\\text{$$}").records.size(), std::size_t(0));
  CHECK_EQ(rec("\\text{a$$}").width, rec("\\text{a}").width);
}

TEST(definitions_an_empty_argument_is_an_empty_box) {
  const auto glyphs = [](const std::string& tex) { return check::count(rec(tex, InputMode::math, false), DrawRecord::GLYPH); };
  CHECK_EQ(glyphs("\\mathop{}\\!\\mathrm{d}x"), 2);
  CHECK_EQ(glyphs("f\\mathopen{}\\left(x\\right)\\mathclose{}"), 4);
  CHECK_EQ(glyphs("a\\reflectbox{}b"), 2);
  CHECK_EQ(glyphs("a\\raisebox{1pt}{}b"), 2);
}

TEST(definitions_an_argument_read_a_second_time_keeps_its_text) {
  const auto fills = [](const std::string& tex) {
    std::vector<unsigned> out;
    for (const auto& r : check::ink(rec(tex, InputMode::math, false))) {
      if (r.type != DrawRecord::GLYPH && std::find(out.begin(), out.end(), r.col) == out.end()) out.push_back(r.col);
    }
    return out;
  };
  CHECK(fills("\\ensuremath{\\colorbox{yellow}{x}}") == fills("\\colorbox{yellow}{x}"));
  CHECK(check::drawsColour(rec("\\sqrt[\\textcolor{red}{3}]{x}"), check::kRed));
}

TEST(definitions_a_number_too_large_or_nan_still_gives_a_layout_that_draws) {
  for (const std::string& tex : {"a\\hspace{" + std::string(45, '9') + "pt}b", std::string("a\\scalebox{1e300}{x}b"),
                                 std::string("a\\rotatebox{nan}{x}b")}) {
    const auto l = rec(tex);
    CHECK(l.width >= 0 && l.height >= 0 && l.depth >= 0);
    CHECK(std::isfinite(l.baseline));
  }
  const auto relscale = rec("a \\relscale{1e30} b", InputMode::mixed);
  CHECK(relscale.width >= 0 && relscale.height >= 0);
}

TEST(definitions_what_a_label_defines_is_its_own) {
  rec("\\definecolor{red}{rgb}{0,0,1}x", InputMode::mixed);
  for (const auto& r : check::ink(rec("\\textcolor{red}{still red}", InputMode::mixed))) CHECK_EQ(r.col, check::kRed);
  rec("\\arrayrulecolor{blue}\\begin{array}{c}\\hline b\\end{array}");
  for (const auto& r : check::ink(rec("\\begin{array}{c}\\hline still black\\end{array}", InputMode::math, false))) {
    if (r.type != DrawRecord::GLYPH) CHECK_EQ(r.col, 0xFF000000u);
  }
  rec("\\breakEverywhere{true}x", InputMode::mixed);
  CHECK_EQ(rec("one run of words", InputMode::mixed).records.size(), std::size_t(1));
}

TEST(definitions_resizebox_of_an_empty_box_is_finite) {
  const auto d = rec("\\resizebox{20pt}{10pt}{\\phantom{}}", InputMode::mixed);
  CHECK(d.width >= 0 && d.height >= 0 && std::isfinite(d.baseline));
  // A box with no width still scales to a height, and the reverse, exactly
  // as one that has a little.
  const auto dim = [](const std::string& tex) { return rec(tex); };
  CHECK_EQ(dim("\\resizebox{!}{20pt}{\\rule{0pt}{5pt}}").height, dim("\\resizebox{!}{20pt}{\\rule{1pt}{5pt}}").height);
  CHECK(dim("\\resizebox{!}{20pt}{\\rule{0pt}{5pt}}").height > dim("\\rule{1pt}{5pt}").height * 3);
  CHECK_EQ(dim("\\resizebox{20pt}{!}{\\rule{5pt}{0pt}}").width, dim("\\resizebox{20pt}{!}{\\rule{5pt}{1pt}}").width);
}

// --- \mark and the macros a label defines ------------------------------------

TEST(definitions_mark_records_an_anchor_and_marks_are_in_source_order) {
  const auto one = rec("a + \\mark{eq} b = c", InputMode::math, false);
  int marks = 0;
  for (const auto& r : one.records) marks += r.type == DrawRecord::MARK ? 1 : 0;
  CHECK_EQ(marks, 1);
  const auto two = rec("\\mark{a} x + \\mark{b} y", InputMode::math, false);
  std::vector<DrawRecord> found;
  for (const auto& r : two.records) {
    if (r.type == DrawRecord::MARK) found.push_back(r);
  }
  CHECK_EQ(found.size(), std::size_t(2));
  CHECK_EQ(found[0].mark_name, std::string("a"));
  CHECK_EQ(found[1].mark_name, std::string("b"));
  CHECK(found[0].x < found[1].x);
}

TEST(definitions_a_macro_call_lays_out_exactly_as_its_body_written_out_longhand) {
  CHECK_PAIRS((Pairs{
    {R"(\def\norm#1{\left\lVert #1 \right\rVert} \norm{v})", R"(\left\lVert v \right\rVert)"},
    {R"(\def\inner#1#2{\langle #1, #2 \rangle} \inner{u}{v})", R"(\langle u, v \rangle)"},
    {R"(\def\frob#1{|#1|} \frob{M})", R"(|M|)"},
    {R"(\newcommand{\xyznorm}[1]{\lVert #1 \rVert} \xyznorm{v})", R"(\lVert v \rVert)"},
  }));
  Options typefaceMode;
  typefaceMode.path = false;
  CHECK_PAIRS_WITH((Pairs{{R"(\def\frob#1{|#1|} \frob{M})", R"(|M|)"}}), typefaceMode);
  CHECK_PAIRS_WITH((Pairs{{R"(\newcommand{\xyznorm}[1]{\lVert #1 \rVert} \xyznorm{v})", R"(\lVert v \rVert)"}}), typefaceMode);
}

TEST(definitions_textcolor_body_inherits_the_surrounding_math_mode) {
  // In math, `\textcolor{blue}{c^2}` sets a superscript, not a caret.
  for (const auto& t : check::texts(rec("$\\textcolor{blue}{c^2}$", InputMode::math, false))) {
    CHECK(t.find('^') == std::string::npos);
  }
}

TEST(definitions_a_color_declaration_colours_the_rest_of_the_enclosing_group) {
  CHECK(check::drawsColour(rec("$\\color{blue} E = mc^2$"), 0xFF0000FFu));
}

TEST(definitions_newcommand_and_def_do_not_leak_between_independent_parses) {
  const std::string src = "\\newcommand{\\xyzleak}{Q} \\xyzleak";
  const auto first = rec(src);
  // A second call with the same definition succeeds and lays out the same.
  CHECK(check::sameInk(first, rec(src)));
  CHECK_PAIRS((Pairs{{src, "Q"}}));
  // Built-in environments are still available.
  const auto pmat = rec("\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}");
  // Four cells and a pair of delimiters, not the literal source text.
  CHECK(pmat.records.size() >= 4);
  for (const auto& t : check::texts(pmat)) CHECK(t.find("pmatrix") == std::string::npos);
}
