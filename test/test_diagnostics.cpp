// Malformed input is drawn as far as it can be, and each problem is a
// diagnostic at its line:col. Only running out of capacity is an error. What
// R makes of the diagnostics (one warning, the cache) is tested in R.

#include <string>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Pairs;
using microtex::InputMode;

namespace {

bool quiet(const std::string& tex, const Options& opt = {}) { return check::render(tex, opt).warnings.empty(); }

bool warnsAbout(const std::string& tex, const std::string& part, const Options& opt = {}) {
  return check::warns(check::render(tex, opt), part);
}

}  // namespace

TEST(diagnostics_one_problem_each_at_its_line_and_column_in_order) {
  const auto l = check::render("a\n\\foo b\n\\bar", Options(InputMode::math));
  CHECK_EQ(l.warnings, check::Strings({"2:1: unknown command \\foo: drawn as its name",
                                       "3:5: missing argument for \\bar: an empty one is used"}));
}

TEST(diagnostics_valid_input_does_not_warn) {
  CHECK(quiet(R"(\begin{pmatrix} a & b \\ c & d \end{pmatrix} + \left( \frac12 \right))"));
}

TEST(diagnostics_an_unclosed_environment_is_closed_at_the_end_of_the_input) {
  // bmatrix's closing bracket comes from its \end.
  for (const char* env : {"bmatrix", "pmatrix", "matrix", "cases"}) {
    const std::string open = std::string(R"(\begin{)") + env + "} a & b";
    CHECK(warnsAbout(open, std::string("missing \\end{") + env + "} inserted"));
    CHECK_PAIRS((Pairs{{open, open + " \\end{" + env + "}"}}));
  }
}

TEST(diagnostics_a_stray_end_is_left_out_with_a_warning) {
  for (const char* env : {"array", "bmatrix"}) {
    const std::string tex = std::string(R"(x \end{)") + env + "} y";
    CHECK(warnsAbout(tex, std::string("\\end{") + env + "} without \\begin ignored"));
    CHECK_PAIRS((Pairs{{tex, "x y"}}));
  }
}

TEST(diagnostics_an_end_of_another_name_ends_a_list_as_in_latex) {
  const Options doc(InputMode::document);
  const std::string wrong = R"(\begin{itemize}\item a\end{enumerate}

After.)";
  CHECK(warnsAbout(wrong, R"(\end{enumerate} ends \begin{itemize})", doc));
  CHECK_PAIRS_WITH((Pairs{{wrong, "\\begin{itemize}\\item a\\end{itemize}\n\nAfter."}}), doc);
  // A list in it still ends its own.
  CHECK(quiet(R"(\begin{itemize}\item a\begin{enumerate}\item b\end{enumerate}\end{itemize} c)", doc));
}

TEST(diagnostics_an_unknown_environment_draws_its_body_with_a_warning) {
  const std::string tex = R"(\begin{quote}hello\end{quote})";
  const auto l = check::render(tex);
  CHECK(check::warns(l, "unknown environment quote"));
  CHECK(!check::drawsColour(l, check::kRed));
  CHECK_PAIRS((Pairs{{tex, R"(\begin{matrix}hello\end{matrix})"}}));
}

TEST(diagnostics_an_unknown_environment_met_in_text_is_text_as_in_latex) {
  // As LaTeX recovers: the body is text in a group.
  for (const InputMode mode : {InputMode::document, InputMode::mixed}) {
    const Options opt(mode);
    CHECK(warnsAbout(R"(\begin{quote}One two.\end{quote})", "unknown environment quote: its body is set as text",
                     opt));
    const std::string body = R"(a \begin{foo}\bf b c\end{foo} d)";
    CHECK_PAIRS_WITH((Pairs{{body, R"(a {\bf b c} d)"}}), opt);
    CHECK_EQ(check::texts(check::render(body, opt)), check::Strings({"a ", "b c", " d"}));
  }
  // In a label, a line end inside it is still a line break.
  CHECK_PAIRS_WITH((Pairs{{"x \\begin{foo}one\ntwo\\end{foo} y", "x one\ntwo y"}}), Options(InputMode::mixed));
  // In math it is an array, as before.
  CHECK_PAIRS_WITH((Pairs{{R"($\begin{foo}a & b\end{foo}$)", R"($\begin{matrix}a & b\end{matrix}$)"}}),
                   Options(InputMode::mixed));
}

TEST(diagnostics_an_environment_inside_an_argument_or_a_list_is_read) {
  const auto nested =
    check::render(R"(\begin{itemize} \item a \begin{itemize}\item b\end{itemize} \end{itemize})");
  CHECK(!check::drawsColour(nested, check::kRed));
  const auto frac = check::render(R"(\frac{1}{\begin{matrix} a \\ b \end{matrix}})");
  CHECK(!check::drawsColour(frac, check::kRed));
}

TEST(diagnostics_double_dollar_inside_text_does_not_open_display_math) {
  // In \text (restricted horizontal mode) TeX reads $$ as an empty formula.
  CHECK(quiet(R"(\text{a$$})"));
  CHECK_PAIRS((Pairs{{R"(\text{a$$})", R"(\text{a})"}}));
}

TEST(diagnostics_a_delimited_macro_whose_delimiter_never_comes_is_left_out) {
  // As after TeX's runaway-argument error: the call goes, the rest is read.
  for (const char* tex : {R"(\def\a#1.{[#1]}\a x)", R"(\def\a#1.{\a#1}\a x)"}) {
    CHECK(warnsAbout(tex, "missing its delimiter; it is left out"));
    CHECK_PAIRS((Pairs{{tex, "x"}}));
  }
  const std::string tex = R"(\def\q[#1]{<#1>}\q x)";
  CHECK(warnsAbout(tex, "does not match its definition; it is left out"));
  CHECK_PAIRS((Pairs{{tex, "x"}}));
}

TEST(diagnostics_an_abandoned_call_puts_back_what_every_argument_read) {
  // All of `x.y z` is read again, not only what #2 read.
  const std::string tex = R"(\def\a#1.#2!{[#1|#2]} \a x.y z)";
  CHECK(warnsAbout(tex, "missing its delimiter"));
  CHECK_PAIRS((Pairs{{tex, "x.y z"}}));
}

TEST(diagnostics_an_environment_left_open_inside_a_group_ends_with_the_group) {
  const std::string env = R"(\newenvironment{gmp}{\left(}{\right)})";
  const std::string tex = env + R"(\frac{\begin{gmp} a}{b} c)";
  CHECK(warnsAbout(tex, "missing \\end{gmp} inserted"));
  CHECK_PAIRS((Pairs{{tex, env + R"(\frac{\begin{gmp} a\end{gmp}}{b} c)"}}));
}

TEST(diagnostics_a_row_gap_in_an_alignment_adds_that_much_space_below_its_row) {
  for (const char* env : {"aligned", "matrix", "gather"}) {
    const bool gather = std::string(env) == "gather";
    const std::string first = gather ? "a" : "a & b";
    const std::string second = gather ? "b" : "c & d";
    const auto body = [&](const std::string& gap) {
      return std::string("\\begin{") + env + "} " + first + " \\\\" + gap + " " + second + " \\end{" + env + "}";
    };
    const auto plain = check::render(body(""));
    const auto gapped = check::render(body("[10pt]"));
    CHECK_EQ(gapped.height - plain.height, 10);
    // The [10pt] is not drawn.
    CHECK(check::pathCharacters(gapped).find(U'[') == std::u32string::npos);
  }
}

TEST(diagnostics_an_environment_in_an_optional_argument_keeps_its_name) {
  CHECK(quiet(R"(\sqrt[\begin{matrix}3\end{matrix}]{x})"));
}

TEST(diagnostics_a_comment_inside_an_argument_is_not_drawn) {
  CHECK_PAIRS((Pairs{
    {"\\text{a % c\nb}", "\\text{a \nb}"},
    {"\\frac{a % c\n}{b}", "\\frac{a\n}{b}"},
    {"\\sqrt{a % }\n}", "\\sqrt{a\n}"},
  }));
}

TEST(diagnostics_a_runaway_argument_in_a_loop_is_an_error_not_a_slow_render) {
  const std::string tex = R"(\def\gmrun#1.{}\newcommand{\gmloop}{\gmrun\gmloop}\gmloop)" + std::string(2000, '1');
  CHECK_THROWS(check::render(tex), "Too many runaway arguments");
}

TEST(diagnostics_a_delimiter_that_is_not_one_is_read_again_as_itself_with_a_warning) {
  // As TeX does: a null delimiter in its place, and the token read again.
  CHECK(warnsAbout(R"(\left( a \middle x b \right))", R"(\middle: x is not a delimiter; drawn after it)"));
  CHECK_PAIRS((Pairs{{R"(\left( a \middle x b \right))", R"(\left( a \middle. x b \right))"}}));
  CHECK(warnsAbout(R"(\left x a \right))", R"(\left: x is not a delimiter; drawn inside the fence)"));
  CHECK_PAIRS((Pairs{{R"(\left x a \right))", R"(\left. x a \right))"}}));
  CHECK(warnsAbout(R"(\left( a \right d)", R"(\right: d is not a delimiter; drawn after the fence)"));
  CHECK_PAIRS((Pairs{{R"(\left( a \right d)", R"(\left( a \right. d)"}}));
  // In a command the engine builds, the command is drawn as its name.
  const auto l = check::render(R"(\genfrac{x}{y}{0pt}{}{a}{b} + c)");
  CHECK(check::warns(l, "x is not a delimiter"));
  CHECK(check::drawsColour(l, check::kRed));
}

TEST(diagnostics_primes_and_a_superscript_apart_are_a_double_superscript_as_in_tex) {
  for (const char* tex : {"f'_1'", "f'_1^2"}) CHECK(warnsAbout(tex, "double superscript"));
  CHECK_PAIRS((Pairs{{"f'_1^2", "{f'_1}^2"}}));
}

TEST(diagnostics_a_definition_that_cannot_be_made_is_dropped_whole) {
  for (const char* tex : {R"(\newcommand{x}[1]{y}z)", R"(\def{x}{y}z)", R"(\newenvironment{}{a}{b}z)",
                          R"(\newcommand{\gmfoo}[x]{BODY}z)", R"(\newenvironment{gme}[x]{B}{E}z)"}) {
    CHECK(!quiet(tex));
    CHECK_PAIRS((Pairs{{tex, "z"}}));
  }
}

TEST(diagnostics_nesting_past_400_is_an_error_however_it_nests) {
  const auto repeat = [](const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s;
    return out;
  };
  const Options mixed(InputMode::mixed);
  for (const std::string& tex : {repeat(R"(\emph)", 500), "$" + repeat(R"(\frac)", 500) + "$",
                                 "a" + repeat(R"(\cite[{)", 500) + "x" + repeat("}]{k}", 500)}) {
    CHECK_THROWS(check::render(tex, mixed), "nested too deeply");
  }
  const std::string deep = repeat(R"(\ensuremath{)", 500) + "x" + repeat("}", 500);
  CHECK_THROWS(check::render(deep), "nested too deeply");
  // One after another is not nesting.
  Options typeface;
  typeface.path = false;
  const auto side = check::render(repeat(R"(\ensuremath{x})", 500), typeface);
  CHECK_EQ(check::count(side, microtex::DrawRecord::GLYPH), 500);
}

TEST(diagnostics_a_runaway_let_and_long_division_of_a_negative_number_end_at_once) {
  const std::string loop = "{\\def\\b{" + std::string(30000, 'x') + "}\\def\\a{\\let\\c\\b\\a}\\a}";
  CHECK_THROWS(check::render(loop), "too large");
  CHECK(warnsAbout(R"(\longdiv{-2147483648}{-1})", "must not be negative", Options(InputMode::mixed)));
}
