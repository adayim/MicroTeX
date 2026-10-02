// The macro expander of the front end (lib/front/expander.h), checked
// through its text output. Each expectation is what TeX / LaTeX make of the
// input.

#include <set>
#include <string>

#include "check.h"
#include "front/expander.h"
#include "front/spec.h"

using namespace microtex::front;
using check::Strings;

namespace {

struct Expanded {
  std::string text;
  Strings messages;
};

// As a parse sets the expander up, except that the prelude is left
// unexpanded, so what a user macro became can be read; its names still count
// as built in. A bad definition or a runaway throws, as without `recover`.
Expanded expand(const std::string& tex) {
  static const std::set<std::string> preludeCommands = [] {
    const auto v = preludeCommandNames();
    return std::set<std::string>(v.begin(), v.end());
  }();
  static const std::set<std::string> preludeEnvs = [] {
    const auto v = preludeEnvironmentNames();
    return std::set<std::string>(v.begin(), v.end());
  }();
  ExpanderOptions opts;
  opts.isBuiltinCommand = [](const std::string& name) {
    return findCommand(name) != nullptr || preludeCommands.count(name) != 0;
  };
  opts.isBuiltinEnvironment = [](const std::string& name) {
    return findEnvironment(name) != nullptr || preludeEnvs.count(name) != 0;
  };
  Diagnostics diags;
  Expanded out;
  out.text = Expander(tex, std::move(opts), diags).expandToText();
  for (const auto& d : diags.items()) out.messages.push_back(d.message);
  return out;
}

std::string ex(const std::string& tex) { return expand(tex).text; }

// define_macro()'s macros, removed again when the test ends.
struct PersistentMacro {
  PersistentMacro(const std::string& name, const std::string& body) {
    setPersistentMacro(name, body);
  }
  ~PersistentMacro() { clearPersistentMacros(); }
};

}  // namespace

TEST(expander_returns_text_with_no_user_macro_byte_for_byte) {
  for (const std::string tex :
       {"a  % note\n  b\\alpha{}", "\\frac12 + \\sqrt[3]{x}",
        "\\begin{matrix} a & b \\\\ c & d \\end{matrix}", "\\text{50\\% off} x^{\\prime}",
        "\\left( x \\right.  ", "α✔️ x", "a\\"}) {
    CHECK_EQ(ex(tex), tex);
  }
}

TEST(expander_substitutes_parameters_in_one_pass) {
  // `#2` inside an argument is the argument's text, not a parameter.
  CHECK_EQ(ex("\\newcommand{\\A}[2]{#1,#2}\\A{\\#2}{z}"), std::string("\\#2,z"));
  CHECK_EQ(ex("\\newcommand{\\A}[2]{#1,#2}\\A{#2}{z}"), std::string("#2,z"));
  CHECK_EQ(ex("\\newcommand{\\twice}[1]{#1#1}\\twice{ab}"), std::string("abab"));
}

TEST(expander_undelimited_argument_is_one_token_or_one_group_never_a_byte) {
  CHECK_EQ(ex("\\newcommand{\\sq}[1]{#1^2}\\sq α"), std::string("α^2"));
  CHECK_EQ(ex("\\newcommand{\\sq}[1]{#1^2}\\sq{ab}"), std::string("ab^2"));
  // A command argument is the command alone, as in TeX.
  CHECK_EQ(ex("\\newcommand{\\sq}[1]{(#1)}\\sq\\alpha"), std::string("(\\alpha)"));
}

TEST(expander_optional_first_argument_of_newcommand_takes_its_default) {
  CHECK_EQ(ex("\\newcommand{\\p}[2][x]{#1_#2}\\p{1}\\p[y]{2}"), std::string("x_1y_2"));
}

TEST(expander_starred_definition_commands_define) {
  CHECK_EQ(ex("\\newcommand*{\\f}{x}\\f"), std::string("x"));
  CHECK_EQ(ex("\\newcommand\\g{y}\\g"), std::string("y"));
  CHECK_EQ(ex("\\DeclareMathOperator{\\am}{am}\\am"),
           std::string("\\mathop{\\mathrm{am}}\\nolimits"));
  CHECK_EQ(ex("\\DeclareMathOperator*{\\am}{am}\\am"),
           std::string("\\mathop{\\mathrm{am}}\\limits"));
}

TEST(expander_paired_delimiters_have_a_plain_a_sized_and_a_starred_form) {
  const std::string decl = "\\DeclarePairedDelimiter{\\abs}{\\lvert}{\\rvert}";
  CHECK_EQ(ex(decl + "\\abs{x}"), std::string("\\lvert x\\rvert"));
  CHECK_EQ(ex(decl + "\\abs[\\big]{x}"), std::string("\\big\\lvert x\\big\\rvert"));
  CHECK_EQ(ex(decl + "\\abs*{x}"), std::string("\\left\\lvert x\\right\\rvert"));
  // A star that is not directly after it is not its star.
  CHECK_EQ(ex(decl + "\\abs {x}*"), std::string("\\lvert x\\rvert*"));
  // Delimiters that are single characters.
  CHECK_EQ(ex("\\DeclarePairedDelimiter{\\p}{(}{)}\\p{a}\\p*{b}"),
           std::string("( a)\\left( b\\right)"));
  // Local to the group that defines it, both forms.
  CHECK_EQ(ex("{" + decl + "\\abs{x}}\\abs*"), std::string("{\\lvert x\\rvert}\\abs*"));
  CHECK_THROWS(ex("\\DeclarePairedDelimiter{\\frac}{(}{)}"), "already exists");
}

TEST(expander_newtheorem_makes_an_environment_with_a_head_the_lowering_numbers) {
  const std::string thm = "\\newtheorem{thm}{Theorem}";
  // The environment is a group, which ends the italics at its end.
  CHECK_EQ(ex(thm + "\\begin{thm}x\\end{thm}"),
           std::string("{\\par\\gmtheorem{plain}{thm}{}{Theorem}{}\\ \\itshape x \\par}"));
  CHECK_EQ(ex(thm + "\\begin{thm}[Euler]x\\end{thm}"),
           std::string("{\\par\\gmtheorem{plain}{thm}{}{Theorem}{Euler}\\ \\itshape x\\par}"));
  // A shared counter, one within a section, and none at all.
  CHECK_EQ(ex(thm + "\\newtheorem{lem}[thm]{Lemma}\\begin{lem}x\\end{lem}"),
           std::string("{\\par\\gmtheorem{plain}{thm}{}{Lemma}{}\\ \\itshape x \\par}"));
  CHECK_EQ(ex("\\newtheorem{thm}{Theorem}[section]\\newtheorem{lem}[thm]{Lemma}"
              "\\begin{lem}x\\end{lem}"),
           std::string("{\\par\\gmtheorem{plain}{thm}{section}{Lemma}{}\\ \\itshape x \\par}"));
  CHECK_EQ(ex("\\newtheorem*{rem}{Remark}\\begin{rem}x\\end{rem}"),
           std::string("{\\par\\gmtheorem{plain}{}{}{Remark}{}\\ \\itshape x \\par}"));
  // The style is the one in force when the theorem is declared.
  CHECK_EQ(ex("\\theoremstyle{definition}\\newtheorem{df}{Definition}\\begin{df}x\\end{df}"),
           std::string("{\\par\\gmtheorem{definition}{df}{}{Definition}{}\\ x\\par}"));
  CHECK_THROWS(ex(thm + thm), "already defined");
}

TEST(expander_tag_star_is_the_tag_without_parentheses) {
  CHECK_EQ(ex("\\tag*{x}"), std::string("\\gmtagstar{x}"));
  CHECK_EQ(ex("\\tag{x}"), std::string("\\tag{x}"));
}

TEST(expander_starred_builtins_become_forms_the_parser_reads) {
  CHECK_EQ(ex("\\operatorname*{x}"), std::string("\\mathop{\\mathrm{x}}\\limits"));
  CHECK_EQ(ex("\\hspace*{1em}"), std::string("\\hspace{1em}"));
  CHECK_EQ(ex("a\\\\*b"), std::string("a\\\\b"));
}

TEST(expander_def_reads_delimited_parameters) {
  CHECK_EQ(ex("\\def\\foo#1.{[#1]}\\foo abc."), std::string("[abc]"));
  // one pair of braces around a delimited argument is removed
  CHECK_EQ(ex("\\def\\p#1,#2.{(#1;#2)}\\p{a,b},c."), std::string("(a,b;c)"));
  // a delimiter before the first parameter must be there
  CHECK_EQ(ex("\\def\\q[#1]{<#1>}\\q[x]"), std::string("<x>"));
  CHECK(check::allContain(expand("\\def\\q[#1]{<#1>}\\q x").messages,
                          "does not match its definition"));
}

TEST(expander_double_hash_in_a_definition_makes_hash_in_the_expansion) {
  CHECK_EQ(ex("\\def\\a{\\def\\b##1{<##1>}}\\a\\b{z}"), std::string("<z>"));
}

TEST(expander_let_copies_a_meaning_and_a_builtin_stays_built_in) {
  // The classic save-and-redefine does not loop.
  CHECK_EQ(ex("\\let\\oldfrac\\frac\\renewcommand{\\frac}[2]{\\oldfrac{#2}{#1}}\\frac{a}{b}"),
           std::string("\\frac{b}{a}"));
  CHECK_EQ(ex("\\newcommand{\\x}{X}\\let\\y\\x\\renewcommand{\\x}{Z}\\y\\x"), std::string("XZ"));
  CHECK_EQ(ex("\\let\\c=y\\c"), std::string("y"));
}

TEST(expander_providecommand_defines_only_what_does_not_exist) {
  CHECK_EQ(ex("\\newcommand{\\x}{1}\\providecommand{\\x}{2}\\x"), std::string("1"));
  CHECK_EQ(ex("\\providecommand{\\y}{3}\\y"), std::string("3"));
}

TEST(expander_redefinition_follows_latexs_rules) {
  CHECK_THROWS(ex("\\newcommand{\\frac}{Q}"), "already exists");
  CHECK_THROWS(ex("\\renewcommand{\\nosuch}{Q}"), "no defined");
  CHECK_EQ(ex("\\renewcommand{\\frac}{Q}\\frac"), std::string("Q"));
  CHECK_EQ(ex("\\def\\x{1}\\def\\x{2}\\x"), std::string("2"));
  CHECK_THROWS(ex("\\newcommand{x}{y}"), "Invalid name");
  CHECK_THROWS(ex("\\def{notacs}{body}"), "expected");
}

TEST(expander_user_environments_expand_at_begin_and_end_as_a_group) {
  // The braces make the environment one atom, as the old parser made it.
  CHECK_EQ(ex("\\newenvironment{pm}{\\begin{pmatrix}}{\\end{pmatrix}}\\begin{pm}a\\end{pm}"),
           std::string("{\\begin{pmatrix}a\\end{pmatrix}}"));
  CHECK_EQ(ex("\\newenvironment{bx}[1]{[#1:}{]}\\begin{bx}{t}x\\end{bx}"),
           std::string("{[t:x]}"));
  CHECK_THROWS(ex("\\newenvironment{matrix}{}{}"), "already exists");
}

TEST(expander_definition_made_in_a_group_ends_with_it_as_in_tex) {
  // Local: outside the group the name means what it meant before, and
  // nothing when it meant nothing.
  CHECK_EQ(ex("{\\newcommand{\\aa}{x}\\aa}\\aa"), std::string("{x}\\aa"));
  CHECK_EQ(ex("\\newcommand{\\q}{a}{\\renewcommand{\\q}{b}\\q}\\q"), std::string("{b}a"));
  CHECK_EQ(ex("\\def\\x{1}{\\def\\x{2}\\x}\\x"), std::string("{2}1"));
  // \gdef defines globally, as in TeX; \def does not.
  CHECK_EQ(ex("{\\gdef\\g{1}\\def\\d{2}}\\g\\d"), std::string("{}1\\d"));
  // A definition at the top level outlives a group that ends after it.
  CHECK_EQ(ex("\\def\\t{4}{x}\\t"), std::string("{x}4"));
  // An environment is a group too, including its own.
  CHECK_EQ(ex("\\newenvironment{e}{}{}\\begin{e}\\def\\h{3}\\h\\end{e}\\h"),
           std::string("{3}\\h"));
  CHECK_EQ(ex("{\\newenvironment{f}{[}{]}\\begin{f}x\\end{f}}\\begin{f}y\\end{f}"),
           std::string("{{[x]}}\\begin{f}y\\end{f}"));
}

TEST(expander_two_tokens_that_were_apart_do_not_run_together) {
  CHECK_EQ(ex("\\newcommand{\\x}[1]{#1\\beta}\\x{a}c"), std::string("a\\beta c"));
  CHECK_EQ(ex("\\makeatletter\\def\\a@b{Q}\\a@b\\makeatother"),
           std::string("\\makeatletter Q\\makeatother"));
}

TEST(expander_runaway_recursion_stops_with_an_error) {
  CHECK_THROWS(ex("\\def\\a{\\a}\\a"), "Too many macro expansions");
  // Either limit may trip first: the count or the bytes.
  std::string what;
  try {
    ex("\\def\\a#1{\\a{#1#1}}\\a{x}");
  } catch (const std::exception& e) {
    what = e.what();
  }
  CHECK(check::contains(what, "too large") || check::contains(what, "Too many"));
}

TEST(expander_missing_argument_is_reported_not_fatal) {
  const Expanded e = expand("\\newcommand{\\f}[1]{<#1>}\\f");
  CHECK(check::allContain(e.messages, "missing argument for \\f"));
  CHECK_EQ(e.text, std::string("<>"));
}

TEST(expander_persistent_macro_name_is_matched_as_tex_reads_names) {
  const PersistentMacro rr("RR", "\\mathbb{R}");
  // `\\RR` is a line break followed by the letters RR, and \RRx is another
  // name; the old regex expander rewrote the first of these.
  CHECK_EQ(ex("a \\\\RR \\RR \\RRx"), std::string("a \\\\RR \\mathbb{R} \\RRx"));
}

TEST(expander_renewcommand_overrides_a_persistent_macro_for_one_parse_only) {
  const PersistentMacro rr("RR", "\\mathbb{R}");
  CHECK_EQ(ex("\\renewcommand{\\RR}{Q}\\RR"), std::string("Q"));
  CHECK_EQ(ex("\\RR"), std::string("\\mathbb{R}"));
  CHECK_THROWS(ex("\\newcommand{\\RR}{Q}"), "already exists");
}
