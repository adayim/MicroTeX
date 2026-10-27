// The macro expander of the front end (lib/front/expander.h), checked
// through its text output. Each expectation is what TeX / LaTeX make of the
// input.

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "check.h"
#include "front/expander.h"
#include "front/packages.h"
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

// The text without its spaces: where whitespace falls around a definition is not the point.
std::string nospace(const std::string& tex) {
  std::string out;
  for (const char c : ex(tex)) {
    if (c != ' ') out += c;
  }
  return out;
}

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
}

TEST(expander_tag_star_is_the_tag_without_parentheses) {
  CHECK_EQ(ex("\\tag*{x}"), std::string("\\gmtagstar{x}"));
  CHECK_EQ(ex("\\tag{x}"), std::string("\\tag{x}"));
}

TEST(expander_starred_builtins_become_forms_the_parser_reads) {
  CHECK_EQ(ex("\\operatorname*{x}"), std::string("\\mathop{\\mathrm{x}}"));
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

TEST(expander_expandafter_expands_the_second_token_once_before_the_first) {
  CHECK_EQ(nospace("\\def\\f#1{[#1]}\\def\\g{abc}\\expandafter\\f\\g"), std::string("[a]bc"));
  CHECK_EQ(nospace("\\def\\g{abc}\\expandafter x\\g"), std::string("xabc"));
  // Two of them nest: the inner one expands what comes after the second.
  CHECK_EQ(nospace("\\def\\a{A}\\def\\b{B}\\expandafter x\\expandafter y\\a"), std::string("xyA"));
  // A token that does not expand is left as it is.
  CHECK_EQ(nospace("\\expandafter x y"), std::string("xy"));
}

TEST(expander_noexpand_leaves_the_next_token_alone) {
  CHECK_EQ(nospace("\\def\\a{A}\\noexpand\\a"), std::string("\\a"));
}

TEST(expander_edef_expands_its_body_when_it_is_defined) {
  CHECK_EQ(nospace("\\def\\a{1}\\edef\\b{\\a 2}\\def\\a{9}\\b"), std::string("12"));
  CHECK_EQ(nospace("\\def\\a{1}\\edef\\b{\\noexpand\\a 2}\\def\\a{9}\\b"), std::string("92"));
  // Parameters are kept for the use.
  CHECK_EQ(nospace("\\def\\a{1}\\edef\\f#1{\\a#1}\\def\\a{9}\\f x"), std::string("1x"));
}

TEST(expander_xdef_is_edef_with_a_global_result) {
  CHECK_EQ(nospace("{\\def\\a{1}\\xdef\\b{\\a 2}}\\b"), std::string("{}12"));
  CHECK_EQ(nospace("{\\def\\a{1}\\edef\\b{\\a 2}}\\b"), std::string("{}\\b"));
}

TEST(expander_futurelet_gives_a_name_the_meaning_of_the_second_token) {
  CHECK_EQ(nospace("\\def\\a{A}\\futurelet\\q\\frac\\a\\q"), std::string("\\fracAA"));
}

TEST(expander_arraystretch_goes_ahead_of_the_array_that_reads_it) {
  CHECK_EQ(nospace("\\def\\arraystretch{2}\\begin{array}{c}a\\end{array}"),
           std::string("\\gmarraystretch{2}\\begin{array}{c}a\\end{array}"));
  // 1 is what it is anyway, and a group ends it.
  CHECK_EQ(nospace("\\def\\arraystretch{1}\\begin{array}{c}a\\end{array}"),
           std::string("\\begin{array}{c}a\\end{array}"));
  CHECK_EQ(nospace("{\\def\\arraystretch{2}}\\begin{array}{c}a\\end{array}"),
           std::string("{}\\begin{array}{c}a\\end{array}"));
}

TEST(expander_ce_and_pu_become_math) {
  CHECK_EQ(nospace("\\ce{H2O}"), std::string("\\ensuremath{\\mathrm{H}_{2}\\mathrm{O}}"));
  CHECK_EQ(nospace("\\pu{3 m}"), std::string("\\ensuremath{3\\,\\mathrm{m}}"));
  // A document's own \ce is left alone.
  CHECK_EQ(nospace("\\newcommand{\\ce}[1]{<#1>}\\ce{x}"), std::string("<x>"));
}

TEST(expander_global_makes_the_next_definition_outlast_its_group) {
  CHECK_EQ(nospace("{\\global\\def\\a{1}}\\a"), std::string("{}1"));
  CHECK_EQ(nospace("{\\global\\edef\\a{x}}\\a"), std::string("{}x"));
  CHECK_EQ(nospace("{\\global\\let\\a=x}\\a"), std::string("{}x"));
  // And only the next one.
  CHECK_EQ(nospace("{\\global\\def\\a{1}\\def\\b{2}}\\b"), std::string("{}\\b"));
}

TEST(expander_ifstar_and_ifnextchar_look_at_the_next_token) {
  CHECK_EQ(nospace("\\@ifstar{S}{N}*x"), std::string("Sx"));
  CHECK_EQ(nospace("\\@ifstar{S}{N}x"), std::string("Nx"));
  CHECK_EQ(nospace("\\@ifnextchar[{Y}{N}[x]"), std::string("Y[x]"));
  CHECK_EQ(nospace("\\@ifnextchar[{Y}{N}x"), std::string("Nx"));
}

TEST(expander_a_parameter_next_to_a_control_word_is_a_token_of_its_own) {
  // TeX never pastes tokens: `\alpha` and `b` stay two, whichever side the
  // parameter is on.
  CHECK_EQ(ex("\\newcommand{\\x}[1]{#1b}\\x{\\alpha}"), std::string("\\alpha b"));
  CHECK_EQ(ex("\\newcommand{\\x}[1]{\\alpha#1}\\x{b}"), std::string("\\alpha b"));
  CHECK_EQ(ex("\\newcommand{\\x}[1]{\\mathrm#1}\\x{ab}"), std::string("\\mathrm ab"));
  // An empty argument leaves the control word and the letters after it apart too.
  CHECK_EQ(ex("\\newcommand{\\x}[1]{\\alpha#1b}\\x{}"), std::string("\\alpha b"));
  // `\\` is a control symbol: the letters after it are plain letters, and
  // there is nothing to separate.
  CHECK_EQ(ex("\\newcommand{\\x}[1]{\\\\#1}\\x{ab}"), std::string("\\\\ab"));
  // Digits and symbols never extended a name.
  CHECK_EQ(ex("\\newcommand{\\x}[1]{\\frac#1}\\x{12}"), std::string("\\frac12"));
}

TEST(expander_a_name_with_at_defined_under_makeatletter_is_one_name_where_it_is_used) {
  // The body is read again where it is used, with `@` back to an ordinary
  // character; the definition remembers that it was made with `@` a letter.
  CHECK_EQ(nospace("\\makeatletter\\newcommand{\\my@a}{A}\\newcommand{\\usea}{\\my@a}"
                   "\\makeatother\\usea"),
           std::string("\\makeatletter\\makeatotherA"));
  // The same for \def, an environment, and a \let of such a name.
  CHECK_EQ(nospace("\\makeatletter\\def\\my@a{A}\\def\\usea{\\my@a}\\makeatother\\usea"),
           std::string("\\makeatletter\\makeatotherA"));
  CHECK_EQ(nospace("\\makeatletter\\newcommand{\\my@a}{A}\\newenvironment{e}{\\my@a}{\\my@a}"
                   "\\makeatother\\begin{e}x\\end{e}"),
           std::string("\\makeatletter\\makeatother{AxA}"));
  CHECK_EQ(nospace("\\makeatletter\\newcommand{\\my@a}{A}\\let\\other\\my@a\\makeatother\\other"),
           std::string("\\makeatletter\\makeatotherA"));
  // A definition made without it keeps `@` apart, as it always did.
  CHECK_EQ(nospace("\\newcommand{\\usea}{\\my@a}\\usea"), std::string("\\my@a"));
}

TEST(expander_a_let_alias_of_a_primitive_does_what_the_primitive_does) {
  CHECK_EQ(ex("\\let\\mydef\\def\\mydef\\foo{F}\\foo"), std::string("F"));
  CHECK_EQ(ex("\\let\\mynew\\newcommand\\mynew{\\foo}{F}\\foo"), std::string("F"));
  CHECK_EQ(ex("\\let\\mylet\\let\\mylet\\a=x\\a"), std::string("x"));
  // A built-in that is not the expander's still passes through, as before.
  CHECK_EQ(ex("\\let\\oldfrac\\frac\\oldfrac{a}{b}"), std::string("\\frac{a}{b}"));
}

TEST(packages_the_format_and_the_packages_it_loads_read_without_a_problem) {
  // A name defined twice, or a command the expander could not read.
  CHECK_EQ(preludeProblems(), Strings{});
}

TEST(packages_usepackage_reads_a_package_once_and_ignores_one_it_has_not_got) {
  registerPackage("probe", "\\newcommand{\\fromprobe}{P}");
  CHECK_EQ(ex("\\usepackage{probe}\\fromprobe"), std::string("P"));
  CHECK_EQ(ex("\\usepackage[opt]{probe,nosuchpackage}\\fromprobe"), std::string("P"));
  CHECK_EQ(ex("\\RequirePackage{probe}[2020/01/01]\\fromprobe"), std::string("P"));
  // Twice is once: defining its macro again would be "already exists".
  CHECK_EQ(ex("\\usepackage{probe}\\usepackage{probe}\\fromprobe"), std::string("P"));
  // Until it is used, its names are not there.
  CHECK_EQ(ex("\\fromprobe"), std::string("\\fromprobe"));
  // A package we have not got is ignored, as \usepackage always was.
  CHECK_EQ(ex("\\usepackage{geometry}a"), std::string("a"));
}

TEST(packages_a_package_reads_with_at_a_letter_and_draws_nothing) {
  registerPackage("atprobe",
                  "\\ProvidesPackage{atprobe}[2026/10/11 probe]\n"
                  "\\newcommand{\\my@a}{A}\n\\newcommand{\\usea}{\\my@a}\n");
  const Expanded loaded = expand("\\usepackage{atprobe}");
  CHECK_EQ(loaded.text, std::string(""));
  CHECK_EQ(loaded.messages, Strings{});
  // `@` is an ordinary character again in the input, and \usea still means A.
  CHECK_EQ(ex("\\usepackage{atprobe}\\usea"), std::string("A"));
}

TEST(packages_what_a_package_does_besides_define_is_done_where_it_is_asked_for) {
  // A command that is not a definition, \hspace here, is left in the input
  // at the place of the \usepackage, for the parser to act on.
  registerPackage("noisyprobe", "\\newcommand{\\fromnoisy}{N}\n\\hspace{1em}\n");
  CHECK_EQ(ex("a\\usepackage{noisyprobe}\\fromnoisy"), std::string("a\\hspace{1em}N"));
}

namespace {

// As a parse starts: from the format and the packages every input starts with,
// where a bad definition is a warning and the parse goes on.
Expanded expandInFormat(const std::string& tex, const std::vector<std::string>& packages = {}) {
  ExpanderOptions opts;
  opts.prelude = true;
  opts.packages = packages;
  opts.persistent = false;
  opts.recover = true;
  opts.isBuiltinCommand = [](const std::string& name) { return findCommand(name) != nullptr; };
  opts.isBuiltinEnvironment = [](const std::string& name) { return findEnvironment(name) != nullptr; };
  Diagnostics diags;
  Expanded out;
  out.text = Expander(tex, std::move(opts), diags).expandToText();
  for (const auto& d : diags.items()) out.messages.push_back(d.message);
  return out;
}

}  // namespace

TEST(packages_a_name_from_a_package_the_input_did_not_ask_for_is_its_to_define) {
  // LaTeX has no \dfrac without amsmath, no proof without amsthm, and KaTeX's
  // \R is no LaTeX at all: the input defines them with no complaint.
  const Expanded dfrac = expandInFormat("\\newcommand{\\dfrac}[2]{Q}\\dfrac{a}{b}");
  CHECK_EQ(dfrac.text, std::string("Q"));
  CHECK_EQ(dfrac.messages, Strings{});
  CHECK_EQ(expandInFormat("\\renewcommand{\\dfrac}[2]{Q}\\dfrac{a}{b}").text, std::string("Q"));
  CHECK_EQ(expandInFormat("\\newenvironment{proof}{[}{]}\\begin{proof}x\\end{proof}").text,
           std::string("{[x]}"));
  CHECK_EQ(expandInFormat("\\newcommand{\\R}{Q}\\R").messages, Strings{});
  CHECK_EQ(expandInFormat("\\newtheorem{proof}{Proof}").messages, Strings{});
}

TEST(packages_a_name_is_taken_once_the_input_asks_for_its_package_and_always_when_it_is_the_formats) {
  CHECK(check::allContain(
    expandInFormat("\\usepackage{amsmath}\\newcommand{\\dfrac}[2]{Q}").messages, "already exists"));
  CHECK(check::allContain(
    expandInFormat("\\usepackage{amsthm}\\newenvironment{proof}{}{}").messages, "already exists"));
  CHECK(check::allContain(
    expandInFormat("\\usepackage{amsthm}\\newtheorem{proof}{Proof}").messages, "already exists"));
  // A package the one asked for requires counts as asked for.
  registerPackage("needsmath", "\\RequirePackage{amsmath}");
  CHECK(check::allContain(
    expandInFormat("\\usepackage{needsmath}\\newcommand{\\dfrac}[2]{Q}").messages, "already exists"));
  // LaTeX's own names, which no package gave, are never free.
  CHECK(check::allContain(expandInFormat("\\newcommand{\\hfill}{Q}").messages, "already exists"));
  CHECK(check::allContain(expandInFormat("\\newcommand{\\ldots}{Q}").messages, "already exists"));
  CHECK(check::allContain(expandInFormat("\\newenvironment{center}{}{}").messages, "already exists"));
}

TEST(packages_a_problem_in_a_package_is_reported_at_its_own_line_and_the_input_is_not_touched) {
  registerPackage("badprobe", "\n\n\\newcommand{\\frac}{x}\n\\newcommand{\\fromBad}{B}\n");
  const Expanded bad = expandInFormat("\\usepackage{badprobe}\\fromBad");
  // \frac exists: the line and column are the package's, the rest still reads.
  CHECK(check::anyContains(bad.messages, "package badprobe line 3:1: \\newcommand: Command frac already exists"));
  CHECK_EQ(bad.text, std::string("B"));
  CHECK_EQ(checkPackage("badprobe").size(), std::size_t{1});
  CHECK(check::anyContains(checkPackage("badprobe"), "package badprobe line 3:1:"));
  // One that is all right has nothing to report, nor has one we have not got.
  registerPackage("goodprobe", "\\newcommand{\\fromgood}{G}");
  CHECK_EQ(checkPackage("goodprobe"), Strings{});
  CHECK_EQ(checkPackage("nosuchprobe"), Strings{});
}

TEST(packages_start_packages_are_loaded_before_the_input_and_are_no_longer_free_to_define) {
  registerPackage("startprobe", "\\newcommand{\\fromstart}{S}\n\\newcommand{\\startnoise}{n}\n\\hspace{2em}\n");
  // The input need not ask for it, and what it did besides define comes first.
  CHECK_EQ(expandInFormat("a\\fromstart", {"startprobe"}).text, std::string("\\hspace{2em}aS"));
  // The same input without it has neither.
  CHECK_EQ(expandInFormat("a\\fromstart").text, std::string("a\\fromstart"));
  // A name it defines is taken, as if the input had asked for the package.
  CHECK(check::allContain(
    expandInFormat("\\newcommand{\\fromstart}{Q}", {"startprobe"}).messages, "already exists"));
  // A list of two, in order, and a format package named in the list is requested by it.
  CHECK_EQ(expandInFormat("\\fromprobe\\fromstart", {"probe", "startprobe"}).text,
           std::string("\\hspace{2em}PS"));
  CHECK(check::allContain(
    expandInFormat("\\newcommand{\\dfrac}[2]{Q}", {"amsmath"}).messages, "already exists"));
}

TEST(packages_physics_reads_cleanly_and_takes_qty_from_siunitx_only_when_asked_for) {
  CHECK_EQ(checkPackage("physics"), Strings{});
  // The comments a macro's text carries are in the expander's text output.
  const auto bare = [](const std::string& tex) {
    std::string out;
    for (const char c : expandInFormat(tex).text) {
      if (c != ' ' && c != '\n' && c != '%') out += c;
    }
    return out;
  };
  CHECK_EQ(bare("\\usepackage{physics}\\abs{x}"), std::string("\\left\\lvertx\\right\\rvert"));
  // Without it, \qty is siunitx's: a number and a unit, and no brackets.
  CHECK(bare("\\qty{1}{m}").find("\\left") == std::string::npos);
  // With it, the brackets.
  CHECK(bare("\\usepackage{physics}\\qty{1}{m}").find("\\left\\lbrace1") != std::string::npos);
}

TEST(packages_a_name_with_at_survives_a_kernel_macro_it_is_passed_through) {
  // \@firstoftwo is the format's, written without @ a letter; what it is
  // given was written with it, and is read so.
  registerPackage("passprobe",
                  "\\newcommand{\\my@a}{A}\n"
                  "\\newcommand{\\usea}{\\@firstoftwo{\\my@a}{no}}\n"
                  "\\newcommand{\\useb}{\\@ifundefined{nosuch}{\\my@a}{no}}\n");
  CHECK_EQ(nospace(expandInFormat("\\usepackage{passprobe}\\usea").text), std::string("A"));
  CHECK_EQ(nospace(expandInFormat("\\usepackage{passprobe}\\useb").text), std::string("A"));
}

TEST(packages_a_package_is_a_world_of_its_own) {
  registerPackage("worldprobe", "\\newcommand{\\fromworld}{W}");
  // Its definitions are global even where \usepackage sits in a group, and
  // the group's own definitions still end with the group.
  CHECK_EQ(ex("{\\def\\local{L}\\usepackage{worldprobe}\\local}\\fromworld\\local"),
           std::string("{L}W\\local"));
}

// --- the TeX a package file is written in --------------------------------

TEST(expander_a_conditional_reads_one_branch_and_skips_the_other) {
  CHECK_EQ(nospace("\\iftrue a\\else b\\fi"), std::string("a"));
  CHECK_EQ(nospace("\\iffalse a\\else b\\fi"), std::string("b"));
  CHECK_EQ(nospace("\\iftrue a\\fi b"), std::string("ab"));
  CHECK_EQ(nospace("\\iffalse a\\fi b"), std::string("b"));
  CHECK_EQ(nospace("\\unless\\iftrue a\\else b\\fi"), std::string("b"));
  CHECK_EQ(nospace("\\unless\\iffalse a\\else b\\fi"), std::string("a"));
  // Conditionals nest, in the branch that is read and in the one that is not.
  CHECK_EQ(nospace("\\iftrue a\\iffalse b\\else c\\fi d\\else e\\fi"), std::string("acd"));
  CHECK_EQ(nospace("\\iffalse x\\iftrue y\\else z\\fi w\\else v\\fi"), std::string("v"));
  // What is skipped does nothing.
  CHECK_EQ(nospace("\\iffalse \\def\\x{1}\\fi \\x"), std::string("\\x"));
}

TEST(expander_a_conditional_it_does_not_evaluate_goes_through_with_its_else_and_fi) {
  // kableExtra's \resizebox{\ifdim\width>\linewidth ...\else...\fi} is read by the parser: a side that is not a length the
  // expander can read (\width, \linewidth are the lowering's) leaves the whole conditional to it.
  CHECK_EQ(nospace("\\ifdim\\width<2pt a\\else b\\fi"), std::string("\\ifdim\\width<2pta\\elseb\\fi"));
  CHECK_EQ(nospace("\\iftrue x\\ifdim\\width<2pt a\\else b\\fi y\\else z\\fi"),
           std::string("x\\ifdim\\width<2pta\\elseb\\fiy"));
  CHECK_EQ(nospace("\\iffalse x\\ifdim\\width<2pt a\\else b\\fi y\\else z\\fi"), std::string("z"));
  // With no conditional open they are not ours either.
  CHECK_EQ(nospace("a\\fi b"), std::string("a\\fib"));
  CHECK_EQ(nospace("a\\else b"), std::string("a\\elseb"));
}

TEST(expander_a_conditional_out_of_place_is_a_warning) {
  CHECK(check::anyContains(expand("\\iffalse a\\else b\\else c\\fi").messages, "extra \\else"));
  // After a branch that was read, TeX skips everything up to the \fi.
  CHECK_EQ(nospace("\\iftrue a\\else b\\else c\\fi"), std::string("a"));
  const Expanded open = expand("\\iffalse a");
  CHECK_EQ(open.text, std::string(""));
  CHECK(check::anyContains(open.messages, "\\fi"));
}

TEST(expander_ifx_compares_meanings_not_spellings) {
  CHECK_EQ(nospace("\\ifx aa Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifx ab Y\\else N\\fi"), std::string("N"));
  // Macros by what they are: parameters and replacement text.
  CHECK_EQ(nospace("\\def\\a{x}\\def\\b{x}\\ifx\\a\\b Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\def\\a{x}\\def\\b{y}\\ifx\\a\\b Y\\else N\\fi"), std::string("N"));
  CHECK_EQ(nospace("\\def\\a#1{x}\\def\\b{x}\\ifx\\a\\b Y\\else N\\fi"), std::string("N"));
  // A \let copy is the same thing, and two names that mean nothing are equal.
  CHECK_EQ(nospace("\\def\\a{x}\\let\\c\\a\\ifx\\a\\c Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifx\\nosucha\\nosuchb Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\def\\a{x}\\ifx\\a\\nosuch Y\\else N\\fi"), std::string("N"));
  // A built-in is equal to a copy of itself, and not to another.
  CHECK_EQ(nospace("\\let\\oldfrac\\frac\\ifx\\oldfrac\\frac Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifx\\frac\\sqrt Y\\else N\\fi"), std::string("N"));
  CHECK_EQ(nospace("\\let\\d\\def\\ifx\\d\\def Y\\else N\\fi"), std::string("Y"));
}

TEST(expander_ifdefined_and_ifcsname_ask_whether_a_name_means_something) {
  CHECK_EQ(nospace("\\def\\a{}\\ifdefined\\a Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifdefined\\nosuch Y\\else N\\fi"), std::string("N"));
  CHECK_EQ(nospace("\\ifdefined\\frac Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifdefined\\def Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\def\\ab{}\\ifcsname ab\\endcsname Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifcsname nosuch\\endcsname Y\\else N\\fi"), std::string("N"));
  // Asking does not define it.
  CHECK_EQ(nospace("\\ifcsname nosuch\\endcsname\\fi\\ifdefined\\nosuch Y\\else N\\fi"),
           std::string("N"));
}

TEST(expander_csname_builds_a_control_sequence_from_expanded_text) {
  CHECK_EQ(nospace("\\def\\ab{X}\\csname ab\\endcsname"), std::string("X"));
  CHECK_EQ(nospace("\\def\\a#1{[#1]}\\csname a\\endcsname z"), std::string("[z]"));
  CHECK_EQ(nospace("\\def\\n{b}\\def\\ab{X}\\csname a\\n\\endcsname"), std::string("X"));
  // The name can be anything TeX could not write: a hyphen, an @.
  CHECK_EQ(nospace("\\expandafter\\def\\csname my-cmd\\endcsname{Q}\\csname my-cmd\\endcsname"),
           std::string("Q"));
  // One that means nothing means \relax, as TeX makes it.
  CHECK_EQ(nospace("\\csname nosuch\\endcsname a"), std::string("a"));
  CHECK_EQ(nospace("\\expandafter\\ifx\\csname nosuch\\endcsname\\relax Y\\else N\\fi"),
           std::string("Y"));
  CHECK_EQ(nospace("\\def\\x{}\\expandafter\\ifx\\csname x\\endcsname\\relax Y\\else N\\fi"),
           std::string("N"));
}

TEST(expander_ifnum_compares_integers_after_expansion) {
  CHECK_EQ(nospace("\\ifnum 1<2 Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifnum 2<1 Y\\else N\\fi"), std::string("N"));
  CHECK_EQ(nospace("\\ifnum 3=3 Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifnum 10>9 Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\ifnum -1<0 Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\def\\n{5}\\ifnum\\n>4 Y\\else N\\fi"), std::string("Y"));
}

TEST(expander_newif_makes_a_conditional_with_its_switches) {
  CHECK_EQ(nospace("\\newif\\iffoo \\iffoo Y\\else N\\fi"), std::string("N"));
  CHECK_EQ(nospace("\\newif\\iffoo \\footrue\\iffoo Y\\else N\\fi"), std::string("Y"));
  CHECK_EQ(nospace("\\newif\\iffoo \\footrue\\foofalse\\iffoo Y\\else N\\fi"), std::string("N"));
  // A switch is local to its group, and a skipped branch knows the new conditional.
  CHECK_EQ(nospace("\\newif\\iffoo {\\footrue}\\iffoo Y\\else N\\fi"), std::string("{}N"));
  CHECK_EQ(nospace("\\newif\\iffoo \\iffalse \\iffoo a\\else b\\fi c\\else d\\fi"), std::string("d"));
  CHECK_THROWS(ex("\\newif\\foo"), "\\newif");
}

TEST(expander_relax_does_nothing) {
  CHECK_EQ(nospace("a\\relax b"), std::string("ab"));
}

TEST(expander_the_kernels_helpers_for_package_code) {
  CHECK_EQ(nospace(expandInFormat("\\@ifundefined{nosuch}{Y}{N}").text), std::string("Y"));
  CHECK_EQ(nospace(expandInFormat("\\def\\x{}\\@ifundefined{x}{Y}{N}").text), std::string("N"));
  CHECK_EQ(nospace(expandInFormat("\\@namedef{a-b}{X}\\@nameuse{a-b}").text), std::string("X"));
  CHECK_EQ(nospace(expandInFormat("\\@gobble{x}a\\@gobbletwo xyb\\@firstofone{c}").text),
           std::string("abc"));
  CHECK_EQ(nospace(expandInFormat("\\@ifpackageloaded{amsmath}{Y}{N}").text), std::string("Y"));
  CHECK_EQ(nospace(expandInFormat("\\@ifpackageloaded{nosuchprobe}{Y}{N}").text), std::string("N"));
  registerPackage("loadedprobe", "\\relax");
  CHECK_EQ(nospace(expandInFormat("\\usepackage{loadedprobe}\\@ifpackageloaded{loadedprobe}{Y}{N}").text),
           std::string("Y"));
}

TEST(packages_declarerobustcommand_defines_or_replaces_without_complaint) {
  CHECK_EQ(nospace("\\DeclareRobustCommand{\\foo}{A}\\DeclareRobustCommand{\\foo}{B}\\foo"),
           std::string("B"));
  CHECK_EQ(expandInFormat("\\DeclareRobustCommand{\\dfrac}[2]{Q}\\dfrac{a}{b}").messages, Strings{});
}

TEST(packages_options_reach_the_package_that_declares_them) {
  registerPackage("optprobe",
                  "\\DeclareOption{big}{\\def\\size{L}}\n"
                  "\\DeclareOption{small}{\\def\\size{S}}\n"
                  "\\DeclareOption*{\\def\\size{?\\CurrentOption}}\n"
                  "\\ExecuteOptions{small}\n"
                  "\\ProcessOptions\\relax\n");
  CHECK_EQ(nospace("\\usepackage{optprobe}\\size"), std::string("S"));
  CHECK_EQ(nospace("\\usepackage[big]{optprobe}\\size"), std::string("L"));
  CHECK_EQ(nospace("\\usepackage[weird]{optprobe}\\size"), std::string("?weird"));
  // In order: the last one given wins.
  CHECK_EQ(nospace("\\usepackage[small,big]{optprobe}\\size"), std::string("L"));
  // An option nothing declares, and no default to take it.
  registerPackage("strictprobe", "\\DeclareOption{ok}{}\\ProcessOptions");
  CHECK(check::anyContains(expand("\\usepackage[nope]{strictprobe}").messages, "nope"));
  CHECK_EQ(expand("\\usepackage[ok]{strictprobe}").messages, Strings{});
}

TEST(packages_atbegindocument_and_endinput) {
  // Code for the start of the document runs where the package ends.
  registerPackage("abdprobe", "\\AtBeginDocument{\\def\\late{L}}\\def\\early{E}");
  CHECK_EQ(nospace("\\usepackage{abdprobe}\\early\\late"), std::string("EL"));
  // The rest of the file is not read after \endinput.
  registerPackage("endprobe", "\\def\\a{A}\\endinput\\def\\b{B}");
  CHECK_EQ(nospace("\\usepackage{endprobe}\\a\\b"), std::string("A\\b"));
  // Outside a package it runs at once.
  CHECK_EQ(nospace("\\AtBeginDocument{x}y"), std::string("xy"));
}

// --- xparse's commands ---------------------------------------------------

namespace {

std::string inFormat(const std::string& tex) { return nospace(expandInFormat(tex).text); }

}  // namespace

TEST(xparse_a_document_command_reads_its_arguments_as_its_spec_says) {
  // m: a mandatory argument, as \newcommand's.
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{mm}{[#1|#2]}\\f{a}{b}"), std::string("[a|b]"));
  // o: optional, -NoValue- when it is not there; O: with a default.
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{om}{(#1)(#2)}\\f[x]{y}"), std::string("(x)(y)"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{om}{(#1)(#2)}\\f{y}"), std::string("(-NoValue-)(y)"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{O{D}m}{(#1)(#2)}\\f{y}"), std::string("(D)(y)"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{O{D}m}{(#1)(#2)}\\f[x]{y}"), std::string("(x)(y)"));
  // r: required, between the delimiters; d and D: optional ones.
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{r()}{<#1>}\\f(abc)"), std::string("<abc>"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{d<>m}{\\IfNoValueTF{#1}{-}{#1}#2}\\f<x>{y}\\f{z}"),
           std::string("xy-z"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{D<>{d}m}{#1#2}\\f{z}\\f<x>{y}"), std::string("dzxy"));
  // g: an optional braced group; G with a default.
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{gm}{\\IfNoValueTF{#1}{-}{#1}#2}\\f{a}{b}\\f c"),
           std::string("ab-c"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{G{g}m}{#1#2}\\f c\\f{a}{b}"), std::string("gcab"));
}

TEST(xparse_star_and_token_arguments_are_booleans) {
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{sm}{\\IfBooleanTF{#1}{S}{P}#2}\\f*{a}\\f{b}"),
           std::string("SaPb"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{tym}{\\IfBooleanTF{#1}{Y}{N}#2}\\f y{a}\\f{b}"),
           std::string("YaNb"));
  // T and F alone, and the tests for a value.
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{sm}{\\IfBooleanT{#1}{S}#2\\IfBooleanF{#1}{P}}\\f*{a}\\f{b}"),
           std::string("SabP"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{om}{\\IfValueTF{#1}{V}{U}\\IfNoValueT{#1}{!}#2}\\f[x]{a}\\f{b}"),
           std::string("Va" "U!b"));
}

TEST(xparse_the_four_ways_of_defining_follow_latexs_rules) {
  CHECK_THROWS(ex("\\NewDocumentCommand{\\frac}{m}{x}"), "already exists");
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{m}{1}\\ProvideDocumentCommand{\\f}{m}{2}\\f{}"), std::string("1"));
  CHECK_EQ(inFormat("\\ProvideDocumentCommand{\\g}{m}{3}\\g{}"), std::string("3"));
  CHECK_EQ(inFormat("\\NewDocumentCommand{\\f}{m}{1}\\RenewDocumentCommand{\\f}{m}{2}\\f{}"), std::string("2"));
  CHECK_THROWS(ex("\\RenewDocumentCommand{\\nosuch}{m}{2}"), "not defined");
  CHECK_EQ(inFormat("\\DeclareDocumentCommand{\\f}{m}{1}\\DeclareDocumentCommand{\\f}{m}{2}\\f{}"), std::string("2"));
  // A name from a package the input did not ask for is free, as with \newcommand.
  CHECK_EQ(expandInFormat("\\NewDocumentCommand{\\dfrac}{mm}{Q}\\dfrac{a}{b}").messages, Strings{});
}

TEST(xparse_a_document_environment_takes_its_arguments_at_begin) {
  CHECK_EQ(inFormat("\\NewDocumentEnvironment{e}{om}{[#1|#2:}{]}\\begin{e}[x]{y}z\\end{e}"),
           std::string("{[x|y:z]}"));
  CHECK_EQ(inFormat("\\NewDocumentEnvironment{e}{O{D}}{[#1:}{]}\\begin{e}z\\end{e}"),
           std::string("{[D:z]}"));
  CHECK_EQ(inFormat("\\NewDocumentEnvironment{e}{}{<}{>}\\RenewDocumentEnvironment{e}{}{(}{)}\\begin{e}z\\end{e}"),
           std::string("{(z)}"));
}

TEST(xparse_a_spec_it_cannot_read_is_a_warning_and_the_command_is_not_made) {
  const Expanded embellish = expandInFormat("\\NewDocumentCommand{\\f}{e{_}}{x}\\f");
  CHECK(check::anyContains(embellish.messages, "not supported"));
  CHECK_EQ(nospace(embellish.text), std::string("\\f"));
  CHECK(check::anyContains(expandInFormat("\\NewDocumentCommand{\\f}{q}{x}").messages, "argument type"));
  // A missing required argument is reported and the rest still reads.
  CHECK(check::anyContains(expandInFormat("\\NewDocumentCommand{\\f}{r()}{<#1>}\\f x").messages, "missing"));
}

TEST(expander_gmenvironmentkind_keyvalue_leaves_the_arguments_of_the_environments_it_names_as_written) {
  // `\a` is a command to apply later, given no argument of its own: read as written it is `k=\a`, expanded it takes the comma.
  const std::string body = "\\def\\a#1{[#1]}\\begin{foo}{k=\\a, y=1}z\\end{foo}";
  CHECK_EQ(nospace(ex(body)), std::string("\\begin{foo}{k=[,]y=1}z\\end{foo}"));
  // A package says the arguments are data it reads itself, as the arguments of a macro are in TeX.
  CHECK_EQ(nospace(ex("\\gmenvironmentkind{keyvalue}{bar, foo}" + body)), std::string("\\begin{foo}{k=\\a,y=1}z\\end{foo}"));
  CHECK_EQ(nospace(ex("\\gmenvironmentkind{keyvalue}{bar}" + body)), std::string("\\begin{foo}{k=[,]y=1}z\\end{foo}"));
}

// \DeclareMathOperator, \DeclarePairedDelimiter and \newfontfamily are LaTeX in amsmath.sty, mathtools.sty and fontspec.sty.
namespace {
std::string nospaces(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}
std::string formatted(const std::string& tex) { return nospaces(expandInFormat(tex).text); }
bool formatSays(const std::string& tex, const std::string& what) {
  return check::anyContains(expandInFormat(tex).messages, what);
}
}  // namespace

TEST(packages_declaremathoperator_makes_an_operator_with_its_limits_beside_it_unless_starred) {
  CHECK_EQ(formatted("\\DeclareMathOperator{\\am}{am}\\am"), std::string("\\mathop{\\mathrm{am}}\\nolimits"));
  CHECK_EQ(formatted("\\DeclareMathOperator*{\\am}{am}\\am"), std::string("\\mathop{\\mathrm{am}}"));
  CHECK(formatSays("\\DeclareMathOperator{\\frac}{x}", "already exists"));
}

TEST(packages_paired_delimiters_have_a_plain_a_sized_and_a_starred_form) {
  const std::string decl = "\\DeclarePairedDelimiter{\\abs}{\\lvert}{\\rvert}";
  CHECK_EQ(formatted(decl + "\\abs{x}"), std::string("\\lvertx\\rvert"));
  CHECK_EQ(formatted(decl + "\\abs[\\big]{x}"), std::string("\\big\\lvertx\\big\\rvert"));
  CHECK_EQ(formatted(decl + "\\abs*{x}"), std::string("\\left\\lvertx\\right\\rvert"));
  // A star that is not directly after it is not its star.
  CHECK_EQ(formatted(decl + "\\abs {x}*"), std::string("\\lvertx\\rvert*"));
  // Delimiters that are single characters.
  CHECK_EQ(formatted("\\DeclarePairedDelimiter{\\p}{(}{)}\\p{a}\\p*{b}"), std::string("(a)\\left(b\\right)"));
  // Local to the group that defines it, both forms.
  CHECK_EQ(formatted("{" + decl + "\\abs{x}}\\abs*"), std::string("{\\lvertx\\rvert}\\abs*"));
  CHECK(formatSays("\\DeclarePairedDelimiter{\\frac}{(}{)}", "already exists"));
}

TEST(packages_newfontfamily_defines_a_command_that_is_fontspec_with_the_same_arguments) {
  CHECK_EQ(formatted("\\newfontfamily\\mono{LM Mono}{\\mono a}"), std::string("{\\fontspec{LMMono}a}"));
  CHECK_EQ(formatted("\\newfontface\\f[Scale=0.9]{Pagella}[Ligatures=TeX]\\f"), std::string("\\fontspec[Scale=0.9]{Pagella}[Ligatures=TeX]"));
  // Local to the group that defines it, like any definition.
  CHECK_EQ(formatted("{\\newfontfamily\\mono{X}\\mono}\\mono"), std::string("{\\fontspec{X}}\\mono"));
  CHECK(formatSays("\\newfontfamily\\frac{X}", "already exists"));
}

// amsthm's \newtheorem and \theoremstyle are LaTeX in amsthm.sty (a counter, an environment, a head macro); what they make is
// read here, and what the lowering draws of it in test_document.cpp.
TEST(packages_newtheorem_makes_an_environment_with_a_counter_and_the_style_in_force) {
  const auto text = [](const std::string& tex) { return expandInFormat(tex).text; };
  const auto has = [&](const std::string& tex, const std::string& part) { return text(tex).find(part) != std::string::npos; };
  const std::string thm = "\\newtheorem{thm}{Theorem}";
  CHECK_EQ(expandInFormat(thm + "\\begin{thm}x\\end{thm}").messages, Strings{});
  // plain: the body in italics, the head bold, and the counter is stepped
  CHECK(has(thm + "\\begin{thm}x\\end{thm}", "\\it x"));
  CHECK(has(thm + "\\begin{thm}x\\end{thm}", "\\stepcounter{thm}"));
  CHECK(has(thm + "\\begin{thm}x\\end{thm}", "\\textbf{Theorem"));
  // the note is the optional argument
  CHECK(has(thm + "\\begin{thm}[Euler]x\\end{thm}", "(Euler)"));
  // the style in force when it is declared: upright body, an italic head for a remark
  CHECK(!has("\\theoremstyle{definition}\\newtheorem{df}{Definition}\\begin{df}x\\end{df}", "\\it x"));
  CHECK(has("\\theoremstyle{remark}\\newtheorem{rk}{Remark}\\begin{rk}x\\end{rk}", "\\textit{Remark"));
  // no number: no counter
  CHECK(!has("\\newtheorem*{rem}{Remark}\\begin{rem}x\\end{rem}", "\\stepcounter"));
  // a shared counter steps the one it shares; one `within` a section is reset by it
  CHECK(has(thm + "\\newtheorem{lem}[thm]{Lemma}\\begin{lem}x\\end{lem}", "\\stepcounter{thm}"));
  CHECK(has("\\newtheorem{thm}{Theorem}[section]\\thethm", "\\arabic{section}."));
  CHECK(check::anyContains(expandInFormat(thm + thm).messages, "already exists"));
}

// tex.web (conv_toks): \string<token> is the token's characters, a control sequence's with its backslash and no space. In a
// \csname it makes the name slashed.sty builds with `\csname sla@\string#5\endcsname` (the real file, loaded unchanged).
TEST(expander_string_in_a_csname_is_the_name_of_the_token_with_its_backslash) {
  CHECK_EQ(ex("\\expandafter\\def\\csname f\\string\\x\\endcsname{B}\\csname f\\string\\x\\endcsname"), std::string("B"));
  CHECK_EQ(ex("\\expandafter\\def\\csname g\\string a\\endcsname{C}\\csname ga\\endcsname"), std::string("C"));
  CHECK_EQ(ex("\\expandafter\\def\\csname h\\string\\partial\\endcsname{D}\\csname h\\string\\partial\\endcsname"), std::string("D"));
}

// latex.ltx (line 1764) stores the branches of \@ifnextchar with `\def\reserved@a{#2}\def\reserved@b{#3}`, so a `##1` in a branch
// is `#1` when it runs: how enumitem makes its keys. Without it a macro made in a branch had no parameter (`'#' must be followed
// by a digit`).
TEST(expander_ifnextchar_branches_are_stored_as_def_stores_them_so_a_double_hash_is_one) {
  CHECK_EQ(ex("\\makeatletter\\@ifnextchar a{\\def\\b##1{<##1>}\\b Z}{no}a"), std::string("\\makeatletter<Z>a"));
  CHECK_EQ(ex("\\makeatletter\\@ifnextchar a{yes}{\\def\\b##1{<##1>}\\b Z}c"), std::string("\\makeatletter<Z>c"));
  CHECK_EQ(ex("\\makeatletter\\@ifstar{\\def\\b##1{<##1>}\\b Z}{no}*"), std::string("\\makeatletter<Z>"));
}

// Registers (tex.web 410-479, 1236): \count, \dimen and \skip, set, added to, multiplied and divided, read with \the, compared
// by \ifnum and \ifdim, local to a group unless \global. A length is kept as points, ems and exs, since the font is not
// known here: one that is only ems prints as ems (`\the` of 1.5em is `1.5em`), a mixture as the points of a 10pt page.
TEST(registers_count_registers_are_set_added_multiplied_and_divided_as_tex_does) {
  CHECK_EQ(nospace("\\newcount\\n \\n=5 \\advance\\n by 3 \\multiply\\n by 2 \\divide\\n by 4 [\\the\\n]"), std::string("[4]"));
  // `by` is optional, a number may be hex, octal or a character code, and a division truncates toward zero
  CHECK_EQ(nospace("\\newcount\\n \\n=\"1F \\advance\\n 1 [\\the\\n]"), std::string("[32]"));
  CHECK_EQ(nospace("\\newcount\\n \\n='17 [\\the\\n]\\n=`a [\\the\\n]\\n=-7 \\divide\\n by 2 [\\the\\n]"),
           std::string("[15][97][-3]"));
  // a numbered register, and one named by \countdef
  CHECK_EQ(nospace("\\count255=7 [\\the\\count255]\\countdef\\k=255 [\\the\\k]"), std::string("[7][7]"));
  // \number, \ifodd and a \chardef constant
  CHECK_EQ(nospace("\\newcount\\n \\n=12 \\number\\n"), std::string("12"));
  CHECK_EQ(nospace("\\ifodd 3 a\\else b\\fi\\ifodd 4 a\\else b\\fi"), std::string("ab"));
  CHECK_EQ(nospace("\\chardef\\k=5 \\ifnum\\k=5 y\\fi"), std::string("y"));
}

TEST(registers_a_length_is_kept_as_points_ems_and_exs) {
  // tex.web: 1cm is 28.45274pt, and a length prints with the fewest digits that read back as it
  CHECK_EQ(nospace("\\dimen0=1cm [\\the\\dimen0]"), std::string("[28.45274pt]"));
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=1pt [\\the\\dist]\\dist=.5pt [\\the\\dist]\\dist=-1.25pt [\\the\\dist]"),
           std::string("[1.0pt][0.5pt][-1.25pt]"));
  // only ems: kept as ems, so that the render's font sets them; a mixture is points of a 10pt page (1em = 10pt)
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=1.5em \\multiply\\dist by 2 [\\the\\dist]"), std::string("[3.0em]"));
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=1.5em \\advance\\dist by 2pt [\\the\\dist]"), std::string("[17.0pt]"));
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=0.5\\dimen0 [\\the\\dist]"), std::string("[0.0pt]"));
  // glue: the natural size, its stretch and its shrink, with fil orders
  CHECK_EQ(nospace("\\newskip\\s \\s=1pt plus 2fil minus 3pt [\\the\\s]"), std::string("[1.0ptplus2.0filminus3.0pt]"));
  // the one place a length is wanted, a skip is its natural size
  CHECK_EQ(nospace("\\newskip\\s \\s=1pt plus 2pt \\hspace{\\s}"), std::string("\\hspace{1.0pt}"));
}

TEST(registers_are_local_to_a_group_unless_global) {
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=1pt {\\dist=2pt [\\the\\dist]}[\\the\\dist]"), std::string("{[2.0pt]}[1.0pt]"));
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=1pt {\\global\\dist=3pt}[\\the\\dist]"), std::string("{}[3.0pt]"));
  CHECK_EQ(nospace("\\newcount\\n \\n=1 {\\advance\\n by 1 {\\advance\\n by 1 [\\the\\n]}[\\the\\n]}[\\the\\n]"),
           std::string("{{[3]}[2]}[1]"));
}

TEST(registers_ifnum_and_ifdim_read_them) {
  CHECK_EQ(nospace("\\newcount\\n \\n=5 \\ifnum\\n>3 a\\else b\\fi\\ifnum\\n=4 a\\else b\\fi"), std::string("ab"));
  CHECK_EQ(nospace("\\ifdim 1pt<2pt a\\else b\\fi\\ifdim 2pt<1pt a\\else b\\fi"), std::string("ab"));
  // 1em is 10pt on the page the sizes are read for
  CHECK_EQ(nospace("\\ifdim 1em=10pt a\\else b\\fi\\ifdim 1em>1pt a\\else b\\fi"), std::string("aa"));
  CHECK_EQ(nospace("\\newdimen\\dist \\dist=3pt \\ifdim\\dist>2pt a\\else b\\fi\\ifdim\\dist>4pt a\\else b\\fi"), std::string("ab"));
  // a side the expander cannot read leaves the conditional to the parser (see the test of foreign conditionals)
  CHECK_EQ(nospace("\\ifdim\\unknown>2pt a\\else b\\fi"), std::string("\\ifdim\\unknown>2pta\\elseb\\fi"));
}

TEST(registers_an_operand_is_its_value_and_a_factor_before_it_multiplies) {
  CHECK_EQ(nospace("\\newdimen\\gap \\gap=3em \\hspace{\\gap}\\hspace{2\\gap}\\hspace{-\\gap}\\hspace{.5\\gap}"),
           std::string("\\hspace{3.0em}\\hspace{6.0em}\\hspace{-3.0em}\\hspace{1.5em}"));
  // what is read ahead for `plus` does not run the command that follows: \newdimen here is run in its turn
  CHECK_EQ(nospace("\\newskip\\s \\s=1pt \\newdimen\\tmpl \\tmpl=2pt [\\the\\tmpl]"), std::string("[2.0pt]"));
}

TEST(registers_setlength_and_addtolength_keep_the_sums_and_leave_the_lowering_its_own) {
  // LaTeX's \newlength is a \newskip; the commands go on to the lowering, which keeps lengths of its own, and a register that
  // was made from them is read here too
  const std::string made = nospace("\\newlength{\\a}\\setlength{\\a}{1em}\\addtolength{\\a}{2em}\\hspace{\\a}");
  CHECK(made.find("\\hspace{3.0em}") != std::string::npos);
  CHECK(made.find("\\setlength{\\a}{1em}") != std::string::npos);
  // a length the expander cannot work out is the lowering's: the register stays a name
  CHECK(nospace("\\newlength{\\w}\\setlength{\\w}{\\linewidth}\\hspace{\\w}").find("\\hspace{\\w}") != std::string::npos);
  // one length from another
  CHECK(nospace("\\newlength{\\lena}\\newlength{\\lenb}\\setlength{\\lena}{2em}\\setlength{\\lenb}{\\lena}\\hspace{\\lenb}").find("\\hspace{2.0em}") !=
        std::string::npos);
}

// tex.web 232-235: \catcode<n> is the category of a character, and \catcode<n>=<v> sets it for the rest of the group.
TEST(registers_catcode_is_read_and_set_and_ends_with_its_group) {
  // (a control word as a character code ends the number only at the next token, which \the\k would already have been expanded in:
  // the \relax is what TeX needs here too)
  CHECK_EQ(nospace("\\newcount\\kk \\kk=\\catcode`\\a\\relax \\the\\kk,\\kk=\\catcode`\\@ \\the\\kk"), std::string("11,12"));
  CHECK_EQ(nospace("{\\catcode`\\@=11 \\def\\a@b{X}\\a@b}\\newcount\\k \\k=\\catcode`\\@ \\the\\k"), std::string("{X}12"));
  CHECK_EQ(nospace("\\catcode`\\@=11 \\def\\a@b{Y}\\a@b"), std::string("Y"));
}

// xparse: `l` is read up to the first `{`, `u{<tokens>}` up to those tokens, which are not part of it.
TEST(expander_xparse_l_and_u_arguments_are_read_up_to_their_delimiters) {
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{l m}{[#1|#2]}\\f ab{c}"), std::string("[ab|c]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{l m}{[#1|#2]}\\f{c}"), std::string("[|c]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\g}{u{;} m}{[#1|#2]}\\g ab;{c}"), std::string("[ab|c]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\g}{u{;} m}{[#1|#2]}\\g a{b;}c;{d}"), std::string("[a{b;}c|d]"));
}

// \newdimen may name what \csname made \relax (kernel's \@ifundefined leaves such a name)
TEST(registers_a_name_that_means_relax_may_be_made_a_register) {
  CHECK_EQ(nospace("\\expandafter\\ifx\\csname gmq\\endcsname\\relax\\fi\\newdimen\\gmq \\gmq=2pt [\\the\\gmq]"), std::string("[2.0pt]"));
}

// tex.web (conv_toks): \string<token> is the token's characters, a control sequence's with its backslash and no space; the
// characters are category 12, so `\expandafter\g\string\foo` hands \g the backslash alone, as `\@gobble\string` relies on. (Here the
// backslash is a private character in the text between expansions, and a backslash again where it is drawn or a name is made.)
TEST(expander_string_gives_the_characters_of_a_token) {
  CHECK_EQ(nospace("\\def\\g#1{}\\expandafter\\g\\string\\foo"), std::string("foo"));
  CHECK_EQ(nospace("\\string~\\string a\\string\\%"), std::string("~a\\textbackslash{}%"));
  CHECK_EQ(nospace("\\string\\foo"), std::string("\\textbackslash{}foo"));
  // a name made of the characters
  CHECK_EQ(nospace("\\expandafter\\def\\csname x\\string\\y\\endcsname{Z}\\csname x\\string\\y\\endcsname"), std::string("Z"));
}

// e-TeX: \numexpr and \dimexpr. 62 cases of what they print are compared with LuaLaTeX in tools/latex-ref/audit/ctan/registers.R;
// these are the ones that decide the rules: precedence, `/` rounding to the nearest with halves away from zero, `a*b/c` in one step.
TEST(registers_numexpr_and_dimexpr_follow_etex) {
  CHECK_EQ(nospace("\\the\\numexpr 1+2*3\\relax"), std::string("7"));
  CHECK_EQ(nospace("\\the\\numexpr(1+2)*3\\relax"), std::string("9"));
  CHECK_EQ(nospace("[\\the\\numexpr 7/2\\relax][\\the\\numexpr -7/2\\relax][\\the\\numexpr 5/2\\relax]"), std::string("[4][-4][3]"));
  CHECK_EQ(nospace("[\\the\\numexpr 2*3/4\\relax][\\the\\numexpr 100*3/7\\relax]"), std::string("[2][43]"));
  CHECK_EQ(nospace("\\the\\dimexpr 1pt+2pt\\relax"), std::string("3.0pt"));
  CHECK_EQ(nospace("\\the\\dimexpr 3pt*2/4\\relax"), std::string("1.5pt"));
  CHECK_EQ(nospace("\\the\\dimexpr(1pt+2pt)*3\\relax"), std::string("9.0pt"));
  // ems stay ems, and an expression is read where a length or a number is
  CHECK_EQ(nospace("\\the\\dimexpr 1em+2em\\relax"), std::string("3.0em"));
  CHECK_EQ(nospace("\\ifnum\\numexpr 2+2=4 y\\fi\\ifdim\\dimexpr 1pt+1pt>1pt y\\fi"), std::string("yy"));
  CHECK_EQ(nospace("\\newdimen\\gap \\gap=\\dimexpr 2em*3\\relax \\hspace{\\gap}"), std::string("\\hspace{6.0em}"));
  CHECK(nospace("\\newlength{\\a}\\setlength{\\a}{\\dimexpr 2em+1em\\relax}\\hspace{\\a}").find("\\hspace{3.0em}") != std::string::npos);
}

// tex.web 1063: \aftergroup<token> puts the token after the `}` that ends the group it is said in (several: in the order said)
TEST(expander_aftergroup_puts_its_tokens_after_the_group) {
  CHECK_EQ(nospace("{a\\aftergroup b\\aftergroup c d}e"), std::string("{ad}bce"));
  CHECK_EQ(nospace("{a{\\aftergroup b}c\\aftergroup d}"), std::string("{a{}bc}d"));
}

// xparse (xparse-2018-04-12.sty, \__xparse_split_list:nn): `>{\SplitList{,}}m` hands the argument over as its pieces, each in braces,
// and `>{\TrimSpaces}` trims it; physics' \diagonalmatrix is read with the first.
TEST(expander_xparse_argument_processors_split_and_trim) {
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{>{\\SplitList{,}}m}{[#1]}\\f{a,b,c}"), std::string("[{a}{b}{c}]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{>{\\SplitList{,}}m}{[#1]}\\f{a{b,c},d}"), std::string("[{a{b,c}}{d}]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{>{\\TrimSpaces}m}{[#1]}\\f{ a }"), std::string("[a]"));
  CHECK_EQ(nospace("\\NewDocumentCommand{\\f}{O{} >{\\SplitList{;}}m}{[#1|#2]}\\f[x]{a;b}"), std::string("[x|{a}{b}]"));
}

// tex.web 1226: a \toks register is set to tokens and read back by \the; it is local to a group unless \global, and a group that
// holds an assignment ends where it should (the `{` of the value is not a group of the input).
TEST(registers_toks_registers_hold_tokens) {
  CHECK_EQ(nospace("\\newtoks\\gt \\gt={ab}[\\the\\gt]"), std::string("[ab]"));
  CHECK_EQ(nospace("\\newtoks\\gt \\gt={a}{\\gt={b}[\\the\\gt]}[\\the\\gt]"), std::string("{[b]}[a]"));
  CHECK_EQ(nospace("\\newtoks\\gt \\gt={a}{\\global\\gt={b}}[\\the\\gt]"), std::string("{}[b]"));
  CHECK_EQ(nospace("\\newtoks\\gt \\gt={a}\\def\\ad{b}\\global\\gt=\\expandafter{\\the\\expandafter\\gt\\ad}[\\the\\gt]"), std::string("[ab]"));
  CHECK_EQ(nospace("\\newtoks\\gt \\newtoks\\gu \\gt={a}\\gu=\\gt [\\the\\gu]"), std::string("[a]"));
  // (TeX expands what follows a register to find its `{`)
  CHECK_EQ(nospace("\\newtoks\\gt \\gt{a}\\def\\ad{b}\\gt\\expandafter{\\the\\gt\\ad}[\\the\\gt]"), std::string("[ab]"));
  CHECK_EQ(nospace("\\newtoks\\gt \\gt{a}\\def\\ob{{c}}\\gt\\ob [\\the\\gt]"), std::string("[c]"));
  // a group with a toks assignment does not end the macros made in it late
  CHECK_EQ(nospace("\\newtoks\\gt \\def\\a{1}{\\def\\a{2}\\gt={x}}\\a"), std::string("{}1"));
}

// latex.ltx 137: \loop ... \repeat, which a loop in a loop (physics' \identitymatrix) needs to restore the outer \iterate when the
// inner group ends.
TEST(registers_loop_repeat_and_nested_loops) {
  CHECK_EQ(nospace(expandInFormat("\\newcount\\gn \\loop\\advance\\gn by 1 [\\the\\gn]\\ifnum\\gn<3 \\repeat").text), std::string("[1][2][3]"));
  CHECK_EQ(nospace(expandInFormat("\\newcount\\gr \\newcount\\gc \\loop\\gc=0 \\advance\\gr by 1 {\\loop\\advance\\gc by 1 (\\the\\gr\\the\\gc)\\ifnum\\gc<2 \\repeat}\\ifnum\\gr<3 \\repeat").text),
           std::string("{(11)(12)}{(21)(22)}{(31)(32)}"));
}

// tex.web 510: an \else or \fi that comes while the test of its \ifnum is read waits: `\ifnum\z@=`}\fi` reads `} as a number.
TEST(registers_a_fi_while_the_test_is_read_waits_for_the_test) {
  CHECK_EQ(nospace("\\ifnum 0=`}\\fi x"), std::string("x"));
  CHECK_EQ(nospace("\\ifnum 125=`}\\fi x"), std::string("x"));
  // a length is an integer in scaled points, and a count is its value
  CHECK_EQ(nospace("\\newdimen\\gd \\gd=1pt \\ifnum\\gd=65536 y\\else n\\fi\\newcount\\gc \\gc=3 \\ifnum\\gc=3 y\\else n\\fi"), std::string("yy"));
}

// tex.web 1191: \left ... \right is a group (math_left_group), so a token saved by \aftergroup inside it comes after the delimiter that
// ends it, and \bgroup and \egroup are braces: physics' \argopen and \argclose open `\mathclose\bgroup\left` and end `\right` with the
// \egroup that closes the \bgroup.
TEST(expander_aftergroup_works_in_a_left_right_group_and_bgroup_is_a_brace) {
  CHECK_EQ(nospace("\\mathclose\\bgroup\\left(x\\aftergroup\\egroup\\right)y"), std::string("\\mathclose\\bgroup\\left(x\\right)\\egroupy"));
  CHECK_EQ(nospace("\\def\\a{1}\\bgroup\\def\\a{2}\\a\\egroup\\a"), std::string("\\bgroup2\\egroup1"));
  CHECK_EQ(nospace("\\def\\a{1}\\left(\\def\\a{2}\\a\\right)\\a"), std::string("\\left(2\\right)1"));
}

// tex.web 506-507: \if compares character codes after expansion, \ifcat categories; a control sequence that does not expand is code 256
// and the category of \relax, so two of them agree, and a letter and an `other` character have the same code but not the same category.
TEST(expander_if_and_ifcat_compare_the_next_two_expanded_tokens) {
  CHECK_EQ(nospace("\\if aay\\else n\\fi\\if abn\\else y\\fi"), std::string("yy"));
  CHECK_EQ(nospace("\\def\\a{b}\\if\\a by\\else n\\fi\\if\\relax\\relax y\\else n\\fi\\if\\relax a n\\else y\\fi"), std::string("yyy"));
  CHECK_EQ(nospace("\\ifcat aby\\else n\\fi\\ifcat a1 n\\else y\\fi\\ifcat\\relax\\relax y\\else n\\fi\\if a1 n\\else y\\fi"), std::string("yyyy"));
  CHECK_EQ(nospace("\\unless\\if ab y\\fi"), std::string("y"));
}

// latex.ltx: \PackageWarning and \PackageError have no log here, so each is one warning at the call, with \MessageBreak a space and the
// info forms silent; \@ifl@t@r compares the format's date (ours: 2025/11/01) with a date as YYYY/MM/DD or YYYY-MM-DD.
TEST(kernel_messages_warn_once_and_the_format_date_is_compared) {
  const Expanded w = expandInFormat("\\PackageWarning{pk}{one\\MessageBreak two}\\PackageInfo{pk}{quiet}x");
  CHECK_EQ(nospace(w.text), std::string("x"));
  CHECK_EQ(w.messages, Strings{"Package pk Warning: one two."});
  CHECK_EQ(expandInFormat("\\PackageError{pk}{bad}{help}").messages, Strings{"Package pk Error: bad."});
  CHECK_EQ(nospace(expandInFormat("\\IfFormatAtLeastTF{2018-02-21}{old}{new}").text), std::string("old"));
  CHECK_EQ(nospace(expandInFormat("\\IfFormatAtLeastTF{2025/11/01}{old}{new}").text), std::string("old"));
  CHECK_EQ(nospace(expandInFormat("\\IfFormatAtLeastTF{2025/11/02}{old}{new}").text), std::string("new"));
  CHECK_EQ(nospace(expandInFormat("\\makeatletter\\@ifl@t@r\\fmtversion{2030-01-01}{old}{new}").text), std::string("\\makeatletternew"));
}

// A name that `@` is a letter in stays one through a \csname: tensor's `\makebox[\@tempdima][c]{$\m@th#1\indexmarker$}` made
// `\gmmakeboxc` by \csname, and `\m@th` in its argument was read as `\m` and `@th` once the call was at a place where `@` is not a letter.
TEST(expander_a_csname_call_reads_its_arguments_as_the_csname_was) {
  const std::string out = nospace(expandInFormat("\\makeatletter\\newcommand\\zz[1]{\\makebox[3pt][l]{$\\m@th #1$}}\\makeatother\\zz{x}").text);
  CHECK(out.find("m@th") == std::string::npos);
  CHECK(out.find("\\rlap{$x$}") != std::string::npos);
}

// mleftright.sty (LuaLaTeX: `f\mleft( x \mright)` is 19.30pt, 1.6pt narrower than with \left and \right): \mleft opens
// `\mathopen{}\mathclose\bgroup\left`, and \mright's \aftergroup\egroup closes the \bgroup after the delimiter of \right. \mleftright
// makes \left and \right those, and the builtin they were is still what the alias hands on, though the name is a macro by then.
TEST(packages_mleftright_sets_left_and_right_in_a_group_of_their_own) {
  // (the expander writes the \bgroup ... \egroup it made as braces)
  const std::string pair = "\\mathopen{}\\mathclose{\\left(x\\right)}";
  CHECK_EQ(nospace(expandInFormat("\\mleft(x\\mright)", {"mleftright"}).text), pair);
  CHECK_EQ(nospace(expandInFormat("\\mleftright\\left(x\\right)", {"mleftright"}).text), pair);
  const std::string restored = nospace(expandInFormat("\\mleftright\\left(x\\right)\\mleftrightrestore\\left(y\\right)", {"mleftright"}).text);
  CHECK(restored.rfind(pair, 0) == 0);
  CHECK(restored.find("\\mathopen{}", pair.size()) == std::string::npos);
  CHECK(restored.find("\\left(y\\right)") != std::string::npos);
}
