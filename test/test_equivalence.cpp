// Each input must draw exactly what its spelled-out form draws: the same
// records, positions and size (engine.h). The parser's own tree is tested in
// test_parser.cpp; these are the whole engine's answer.

#include <set>

#include "check.h"
#include "engine.h"

using check::Pairs;
using microtex::InputMode;

TEST(equivalence_an_argument_without_braces_is_one_token) {
  CHECK_PAIRS((Pairs{
    {R"(\frac12)", R"(\frac{1}{2})"},
    {R"(\frac\alpha\beta)", R"(\frac{\alpha}{\beta})"},
    {R"(\frac 1 {x^2})", R"(\frac{1}{x^2})"},
    {R"(x^\frac12)", R"(x^{\frac{1}{2}})"},
    {R"(\sqrt[n]x)", R"(\sqrt[n]{x})"},
    {R"(\mathbb R^n)", R"(\mathbb{R}^n)"},
    {R"(\vec x_1)", R"(\vec{x}_1)"},
    {R"(\sqrt\frac12)", R"(\sqrt{\frac{1}{2}})"},
    {R"(\binom nk)", R"(\binom{n}{k})"},
    {R"(\textcolor{red}a)", R"(\textcolor{red}{a})"},
    {R"(\tfrac12)", R"(\tfrac{1}{2})"},
    {R"(\mathbf 1)", R"(\mathbf{1})"},
    // One character, not one byte of it.
    {R"(\frac αβ)", R"(\frac{α}{β})"},
    {R"(\sqrt α)", R"(\sqrt{α})"},
    {R"(\hat é)", R"(\hat{é})"},
    {R"(\overline ω)", R"(\overline{ω})"},
    {R"(\mathrm é)", R"(\mathrm{é})"},
  }));
}

TEST(equivalence_a_dimension_ends_after_its_two_letter_unit_so_what_follows_is_text) {
  // `\kern1emx` drew no x: the reader took x for more of the unit.
  CHECK_PAIRS((Pairs{
    {R"(a\kern1emx)", R"(a\kern1em x)"},
    {R"(a\mkern3muab)", R"(a\mkern3mu ab)"},
    {R"(a\mkern-7mub)", R"(a\mkern-7mu b)"},
    {R"(a\hskip1emb)", R"(a\hskip1em b)"},
    {R"(a\kern1truemmb)", R"(a\kern1truemm b)"},
  }));
}

TEST(equivalence_an_infix_command_takes_its_whole_group) {
  CHECK_PAIRS((Pairs{
    {R"({a \over b} + {c \over d})", R"({\frac{a}{b}} + {\frac{c}{d}})"},
    {R"(\frac{a}{b} \over c)", R"(\frac{\frac{a}{b}}{c})"},
    {R"(x \over y + z)", R"(\frac{x}{y + z})"},
  }));
}

TEST(equivalence_a_declaration_runs_to_the_end_of_its_group) {
  CHECK_PAIRS((Pairs{
    {R"({\color{red} a} + b)", R"(\textcolor{red}{a} + b)"},
    {R"({a {\color{red} b} c} d)", R"({a {\textcolor{red}{b}} c} d)"},
    {R"(\mathbf{a\color{red}b}c)", R"(\mathbf{a\textcolor{red}{b}}c)"},
    {R"({\bf a} b)", R"({\mathbf{a}} b)"},
    {R"({\bf a} {b})", R"({\mathbf{a}} {b})"},
    // A group after a declaration's group is a group of its own.
    {R"({\color{red} a} + {b})", R"(\textcolor{red}{a} + {b})"},
    {R"({\color{blue} a} {\color{red} b})", R"(\textcolor{blue}{a} \textcolor{red}{b})"},
    {R"({a \\ b} {c})", R"({a \\ b}{c})"},
    {R"(\bf a \\ b \color{red} c)", R"(\bf a \\ b {\color{red} c})"},
  }));
}

TEST(equivalence_middle_takes_a_delimiter_by_name_as_left_and_right_do) {
  // It was handed the text "\vert" and failed at layout: only a single
  // character worked.
  CHECK_PAIRS((Pairs{
    {R"(\left\{ x \middle\vert x > 0 \right\})", R"(\left\{ x \middle| x > 0 \right\})"},
    {R"(\left\langle a \middle\vert b \right\rangle)", R"(\left\langle a \middle| b \right\rangle)"},
  }));
}

TEST(equivalence_a_right_the_parser_inserts_is_texs_null_delimiter) {
  // `\left(x` warns "missing \right. inserted" and must then draw what
  // `\left(x\right.` draws, the fence stretched.
  for (const char* tex : {R"(\left(\frac{a}{b})", R"(\left[\begin{matrix}a\\b\end{matrix})",
                          R"(\left\{x^{y^z})"}) {
    const std::string open = tex;
    CHECK_PAIRS((Pairs{{open, open + R"(\right.)"}}));
    CHECK(check::warns(check::render(open), "missing \\right. inserted"));
  }
}

TEST(equivalence_cal_and_frak_switch_the_math_alphabet_for_the_rest_of_the_group) {
  // TeX's old declarations; their argument was lost.
  CHECK_PAIRS((Pairs{
    {R"({\cal x} y)", R"(\mathcal{x} y)"},
    {R"({\frak x} y)", R"(\mathfrak{x} y)"},
    {R"(\oldstylenums{123})", R"(\text{123})"},
  }));
}

TEST(equivalence_a_double_bar_is_vert_as_in_latex) {
  // The engine's symbol table had it as a single bar.
  CHECK_PAIRS((Pairs{
    {R"(\|x\|)", R"(\Vert x\Vert)"},
    {R"(\left\| x \right\|)", R"(\left\Vert x \right\Vert)"},
  }));
}

TEST(equivalence_a_prime_is_the_superscript_prime_as_in_tex) {
  CHECK_PAIRS((Pairs{
    {R"(f'(x))", R"(f^{\prime}(x))"},
    {R"(f'')", R"(f^{\prime\prime})"},
    {R"(f'^2)", R"(f^{\prime 2})"},
    {R"(f'^{ab})", R"(f^{\prime ab})"},
    {R"(f'_1)", R"(f^{\prime}_1)"},
    {R"(f_1')", R"(f_1^{\prime})"},
  }));
}

TEST(equivalence_a_backtick_and_a_double_quote_in_math_are_characters_not_primes) {
  CHECK_PAIRS((Pairs{
    {"f`", "f{`}"},
    {"f\"", "f{\"}"},
    {"f'`", R"(f^{\prime}{`})"},
  }));
  // On the baseline with the f, not raised as a script.
  check::Options typeface;
  typeface.path = false;
  const auto l = check::render("f`\"", typeface);
  std::set<float> ys;
  for (const auto& r : l.records) {
    if (r.type == microtex::DrawRecord::GLYPH) ys.insert(r.y);
  }
  CHECK_EQ(ys.size(), std::size_t(1));
}

TEST(equivalence_scripts_attach_the_same_in_either_order) {
  CHECK_PAIRS((Pairs{
    {"a^{b}_{c}", "a_{c}^{b}"},
    {R"(\sum_{i=1}^{n})", R"(\sum^{n}_{i=1})"},
  }));
}

TEST(equivalence_a_macro_expands_to_what_it_stands_for) {
  CHECK_PAIRS((Pairs{
    {R"(\newcommand{\half}{\frac12} x^\half)", R"(x^{\frac{1}{2}})"},
    {R"(\newcommand{\sq}[1]{#1^2} \sq{y} + \sq z)", "y^2 + z^2"},
    {R"(\newcommand{\p}[2][x]{#1_#2} \p{1} + \p[y]{2})", "x_1 + y_2"},
    {R"(\newcommand{\twice}[1]{#1#1} \twice{\alpha})", R"(\alpha\alpha)"},
    {R"(\newcommand{\bmat}{\begin{bmatrix}} \bmat a & b \end{bmatrix})",
     R"(\begin{bmatrix} a & b \end{bmatrix})"},
    {R"(\def\foo#1#2{#2#1} \foo ab)", "ba"},
  }));
}

TEST(equivalence_a_starred_command_is_the_commands_starred_form_not_a_star_argument) {
  CHECK_PAIRS((Pairs{
    {R"(\operatorname*{argmax}_x f)", R"(\mathop{\mathrm{argmax}}\limits_x f)"},
    {R"(\newcommand*{\f}{x}\f)", "x"},
    {R"(\hspace*{1em}a)", R"(\hspace{1em}a)"},
    {R"(\DeclareMathOperator*{\am}{am}\am_x f)", R"(\mathop{\mathrm{am}}\limits_x f)"},
    {R"(\begin{aligned} a \\* b \end{aligned})", R"(\begin{aligned} a \\ b \end{aligned})"},
  }));
}

TEST(equivalence_macro_arguments_are_substituted_once_and_may_be_delimited) {
  CHECK_PAIRS((Pairs{
    {R"(\newcommand{\A}[2]{#1,#2} \A{\#2}{z})", R"(\#2,z)"},
    {R"(\def\foo#1.{[#1]} \foo abc.)", "[abc]"},
    {R"(\newcommand{\sq}[1]{#1^2}\sq α)", "α^2"},
    {R"(\let\oldfrac\frac\renewcommand{\frac}[2]{\oldfrac{#2}{#1}}\frac{a}{b})", R"(\frac{b}{a})"},
  }));
}

TEST(equivalence_text_arguments_keep_their_words_braces_and_escapes) {
  CHECK_PAIRS((Pairs{
    {R"(\text{a {b} c})", R"(\text{a b c})"},
    {R"(x^\text{a b})", R"(x^{\text{a b}})"},
  }));
}

TEST(equivalence_over_and_kin_end_at_a_row_break_and_the_rows_go_on) {
  CHECK_PAIRS((Pairs{
    {R"(a \over b \\ c)", R"({a \over b} \\ c)"},
    {R"(a \over b \\ c \over d)", R"({a \over b} \\ {c \over d})"},
    {R"(a \choose b \cr c)", R"({a \choose b} \cr c)"},
  }));
}

TEST(equivalence_an_alignments_position_is_not_its_columns) {
  CHECK_PAIRS((Pairs{
    // [c] is the middle, as none is; [t] and [b] move the baseline (test_tables).
    {R"(\begin{array}[c]{cc} a & b \end{array})", R"(\begin{array}{cc} a & b \end{array})"},
    {R"(\begin{aligned}[b] a &= b \end{aligned})", R"(\begin{aligned} a &= b \end{aligned})"},
    {R"(\begin{gathered}[c] a \end{gathered})", R"(\begin{gathered} a \end{gathered})"},
  }));
}
