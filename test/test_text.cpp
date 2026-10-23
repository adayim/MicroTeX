// TeX's text ligatures, the commands a single grob cannot carry out (a
// reference, a citation, a footnote), and the caps on runaway input (engine.h).

#include <string>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

check::Layout rec(const std::string& tex, InputMode mode = InputMode::document) {
  Options opt(mode);
  opt.path = false;
  return check::render(tex, opt);
}

// The text records of a layout, in drawing order, joined with a bar.
std::string drawn(const std::string& tex, InputMode mode = InputMode::document) {
  std::string out;
  bool first = true;
  for (const auto& t : check::texts(rec(tex, mode))) {
    out += (first ? "" : "|") + t;
    first = false;
  }
  return out;
}

// What is drawn bold.
Strings bold(const std::string& tex) {
  Strings out;
  for (const auto& r : check::of(rec(tex), DrawRecord::TEXT)) {
    if ((r.font_style & 2) != 0) out.push_back(r.text);
  }
  return out;
}

}  // namespace

TEST(text_a_run_of_hyphens_is_texs_dash) {
  CHECK_EQ(drawn("a--b"), std::string("a\u2013b"));   // en dash
  CHECK_EQ(drawn("a---b"), std::string("a\u2014b"));  // em dash
  // Four is the em dash and a hyphen, as TeX reads them left to right.
  CHECK_EQ(drawn("a----b"), std::string("a\u2014-b"));
  // One is a hyphen, and spaced ones are not a ligature at all.
  CHECK_EQ(drawn("a-b"), std::string("a-b"));
  CHECK_EQ(drawn("a - b"), std::string("a - b"));
  CHECK_EQ(drawn("1--5 and 10---20"), std::string("1\u20135 and 10\u201420"));
}

TEST(text_quotes_are_the_curly_ones_as_texs_text_fonts_set_them) {
  CHECK_EQ(drawn("``q''"), std::string("\u201cq\u201d"));
  // A single one too: ` and ' are TeX's opening and closing single quotes,
  // and ' its apostrophe.
  CHECK_EQ(drawn("don't"), std::string("don\u2019t"));
  CHECK_EQ(drawn("`single'"), std::string("\u2018single\u2019"));
  // Three are the double quote, then the single one.
  CHECK_EQ(drawn("'''"), std::string("\u201d\u2019"));
}

TEST(text_ligatures_are_a_text_thing_and_not_for_a_typewriter_font) {
  // In math `''` is a double prime and `-` an operator; nothing ligates.
  CHECK_EQ(drawn("$a-b$", InputMode::math), std::string(""));
  // Inside \text{} in a formula they do, because that is text.
  CHECK_EQ(drawn("\\text{a--b ``q''}", InputMode::math), std::string("a\u2013b \u201cq\u201d"));
  // A typewriter font has no ligatures in TeX, so code keeps its hyphens.
  CHECK_EQ(drawn("\\texttt{--verbose}"), std::string("--verbose"));
  CHECK_EQ(drawn("\\texttt{a--b ``q''}"), std::string("a--b ``q''"));
  // \tt is a declaration, but its body is typewriter all the same.
  CHECK_EQ(drawn("{\\tt a--b} c--d"), std::string("a--b| c\u2013d"));
  // A URL is verbatim, whatever is in it.
  CHECK_EQ(drawn("\\url{http://a--b.com}"), std::string("http://a--b.com"));
}

TEST(text_a_reference_citation_or_footnote_warns_and_draws_what_latex_draws) {
  // LaTeX draws a bold ?? for a reference it cannot resolve, and a bold ?
  // in brackets for a citation.
  CHECK(check::warns(rec("see \\ref{fig:a}"), "reference `fig:a' is undefined"));
  CHECK_EQ(drawn("see \\ref{fig:a} there"), std::string("see |??| there"));
  CHECK_EQ(bold("see \\ref{fig:a} there"), Strings({"??"}));
  CHECK_EQ(drawn("eq \\eqref{eq:1}"), std::string("eq |(|??|)"));
  CHECK_EQ(drawn("page \\pageref{p}"), std::string("page |??"));
  CHECK(check::warns(rec("\\cite{smith_2020}"), "citation `smith_2020' is undefined: drawn as [?]"));
  CHECK_EQ(drawn("as in \\cite{smith_2020} here"), std::string("as in |[|?|]| here"));
  CHECK_EQ(bold("as in \\cite{smith_2020} here"), Strings({"?"}));
  // One ? for each key, and the note after them, as LaTeX prints it.
  CHECK_EQ(drawn("\\cite{a, b}"), std::string("[|?|, |?|]"));
  CHECK_EQ(drawn("\\cite[p. 5]{key}"), std::string("[|?|, |p. 5|]"));
  // A label has no page for a footnote, so its text is set where it stands; a document sets the mark, and the
  // note after its body, numbered as the counter has it, or as the optional number says.
  CHECK(check::warns(rec("text\\footnote{a note} on", InputMode::mixed), "no page to put a note on"));
  CHECK_EQ(drawn("text\\footnote{a note} on", InputMode::mixed), std::string("text|a note| on"));
  CHECK_EQ(drawn("text\\footnote{a note} on"), std::string("text|1| on|1|a note"));
  CHECK_EQ(drawn("text\\footnote[2]{a note} on"), std::string("text|2| on|2|a note"));
  CHECK(!check::warns(rec("text\\footnote{a note} on"), "no page"));
}

TEST(text_a_book_length_body_is_within_the_expansion_caps) {
  // 10,000 expansions was too tight: this reached it at about 16,000.
  std::string para;
  for (int i = 0; i < 8; i++) para += (i ? " " : "") + std::string("We found \\emph{clear} evidence for the effect here.");
  std::string tex;
  for (int i = 0; i < 2000; i++) tex += (i ? "\n\n" : "") + para;
  CHECK(tex.size() > 800 * 1024);
  Options opt(InputMode::document, 500);
  CHECK(check::render(tex, opt).warnings.empty());
}

TEST(text_a_runaway_inside_an_optional_argument_is_stopped) {
  // Each expansion opens a \sqrt[ that is never closed; after the error
  // nothing is read again.
  for (const char* tex : {"\\newcommand{\\a}{x\\sqrt[\\a}\\a", "\\newcommand{\\a}[1]{#1#1\\sqrt[\\a{#1}}\\a{xy}",
                          "\\def\\a{\\sqrt[\\a\\a}\\a"}) {
    CHECK_THROWS(check::render(tex), "macro defined in terms of itself");
  }
}

// \makebox[w][pos]{text} is a box of width w whatever the text: wider text stands out of it, as in TeX, and is not wrapped
// (LuaLaTeX, 10pt: `\makebox[0pt][l]{two words}x` is the width of x, `\makebox[2em][r]{some label}x` 2em and x).
TEST(text_a_makebox_is_as_wide_as_asked_and_its_text_is_not_wrapped) {
  Options opt(InputMode::mixed);
  opt.textSize = 100;
  const auto width = [&](const std::string& tex) { return check::render(tex, opt).width; };
  const int x = width("x");
  CHECK(std::abs(width(R"(\makebox[0pt][l]{two words}x)") - x) <= 2);
  CHECK(std::abs(width(R"(\makebox[0pt][r]{two words}x)") - x) <= 2);
  CHECK(std::abs(width(R"(\makebox[0pt]{two words}x)") - x) <= 2);
  CHECK(std::abs(width(R"(\makebox[2em][r]{some label}x)") - x - 200) <= 2);
  CHECK(std::abs(width(R"(\makebox[5em][l]{ab}x)") - x - 500) <= 2);
  CHECK(std::abs(width(R"(\makebox[3em][s]{a b}x)") - x - 300) <= 2);
  // One line: nothing is taller than the text itself.
  CHECK_EQ(check::render(R"(\makebox[2em][r]{some label}x)", opt).height, check::render("some labelx", opt).height);
}
