// The packages in res/tex, as a label meets them: each command must draw as
// the spelled-out form it stands for, and a package file written in LaTeX
// must work as written. How the loader reads a package is tested through the
// expander's text, in test_expander.cpp; these are the drawings.

#include <string>
#include <vector>

#include "check.h"
#include "engine.h"
#include "front/expander.h"
#include "front/packages.h"

using check::Pairs;

namespace {

// Every spelling of `pairs` with `prefix` in front of it.
Pairs after(const std::string& prefix, const Pairs& pairs) {
  Pairs out;
  for (const auto& p : pairs) out.push_back({prefix + p.first, p.second});
  return out;
}

// esdiff sets a derivative in display size wherever it stands.
std::string chosen(const std::string& x) {
  return "\\mathchoice{" + x + "}{\\displaystyle{" + x + "}}{\\scriptstyle{" + x + "}}{\\scriptstyle{" + x +
         "}}";
}


}  // namespace

// --- the small packages every label starts with ---------------------------

TEST(small_packages_bbm_nicefrac_centernot_and_mleftright_draw_as_what_they_stand_for) {
  CHECK_PAIRS((Pairs{
    {R"(\mathbbm{1})", R"(\mathbb{1})"},
    {R"(\nicefrac{1}{2})", R"(\sfrac{1}{2})"},
    {R"(\nicefrac[\mathbf]{1}{2})", R"(\sfrac{1}{2})"},
    {R"(a \centernot\implies b)", R"(a \not\implies b)"},
    {R"(\mleft( x \mright))", R"(\left( x \right))"},
  }));
}

TEST(small_packages_esdiffs_derivatives_draw_as_the_fractions_they_stand_for) {
  CHECK_PAIRS((Pairs{
    {R"(\diff{f}{x})", chosen(R"(\frac{\mathrm{d} f}{\mathrm{d} x})")},
    {R"(\diff[2]{f}{x})", chosen(R"(\frac{\mathrm{d}^{2} f}{\mathrm{d} x^{2}})")},
    {R"(\diff*{f}{x}{a})", chosen(R"(\left(\frac{\mathrm{d} f}{\mathrm{d} x}\right)_{\mkern-7mu a})")},
    {R"(\diffp{f}{x})", chosen(R"(\frac{\partial f}{\partial x})")},
    {R"(\diffp[2]{f}{x})", chosen(R"(\frac{\partial^{2} f}{\partial x^{2}})")},
    {R"(\diffp{f}{xy})", chosen(R"(\frac{\partial^{2} f}{\partial x\partial y})")},
    {R"(\diffp{f}{xyz})", chosen(R"(\frac{\partial^{3} f}{\partial x\partial y\partial z})")},
    {R"(\diffp*{f}{x}{a})", chosen(R"(\left(\frac{\partial f}{\partial x}\right)_{\mkern-7mu a})")},
    // A group after a derivative is not taken for its argument.
    {R"(\diff{f}{x}{y})", chosen(R"(\frac{\mathrm{d} f}{\mathrm{d} x})") + "{y}"},
  }));
}

TEST(small_packages_relsize_sets_the_rest_of_the_group_a_step_larger_or_smaller) {
  CHECK_PAIRS((Pairs{
    {R"(a{\larger b})", R"(a{\relscale{1.2} b})"},
    {R"(a{\smaller[2] b})", R"(a{\relscale{0.69444} b})"},
    {R"(a{\relsize{3} b})", R"(a{\relscale{1.728} b})"},
    {R"(a{\relsize{-1} b})", R"(a{\relscale{0.83333} b})"},
    {R"(a\textlarger{b})", R"(a{\relscale{1.2} b})"},
  }));
}

TEST(small_packages_ohm_and_textmu_are_the_symbols_they_name) {
  CHECK_PAIRS((Pairs{
    {R"(\ohm)", R"(\Omega)"},
    {R"(\textmu)", "µ"},
  }));
}

TEST(small_packages_a_name_is_free_to_define_until_the_label_asks_for_the_package) {
  CHECK(check::render(R"(\newcommand{\diff}{x}\diff)").warnings.empty());
  CHECK(check::render(R"(\newcommand{\nicefrac}[2]{x})").warnings.empty());
  CHECK(check::warns(check::render(R"(\usepackage{esdiff}\newcommand{\diff}{x})"), "already exists"));
}

// --- physics, which a label asks for --------------------------------------

TEST(physics_is_not_loaded_until_asked_for) {
  CHECK(check::warns(check::render(R"(\abs{x})"), "unknown command"));
}

TEST(physics_automatic_brackets_draw_as_the_brackets_they_stand_for) {
  CHECK_PAIRS(after(R"(\usepackage{physics})", Pairs{
    {R"(\qty(\frac ab))", R"(\left(\frac ab\right))"},
    {R"(\qty[x])", R"(\left[x\right])"},
    {R"(\qty{x})", R"(\left\lbrace x\right\rbrace)"},
    {R"(\qty|x|)", R"(\left\lvert x\right\rvert)"},
    {R"(\qty\big(x))", R"(\bigl(x\bigr))"},
    {R"(\qty\Bigg[x])", R"(\Biggl[x\Biggr])"},
    {R"(\pqty{x})", R"(\left(x\right))"},
    {R"(\bqty{x})", R"(\left[x\right])"},
    {R"(\Bqty{x})", R"(\left\lbrace x\right\rbrace)"},
    {R"(\vqty{x})", R"(\left\lvert x\right\rvert)"},
    {R"(\pqty\bigg{x})", R"(\biggl(x\biggr))"},
    {R"(\abs{x})", R"(\left\lvert x\right\rvert)"},
    {R"(\norm{x})", R"(\left\lVert x\right\rVert)"},
    {R"(\order{x^2})", R"(\mathcal{O}\mathclose{\left(x^2\right)})"},
    {R"(\comm{A}{B})", R"(\left[A,B\right])"},
    {R"(\acomm{A}{B})", R"(\left\lbrace A,B\right\rbrace)"},
    {R"(\pb{f}{g})", R"(\left\lbrace f,g\right\rbrace)"},
  }));
}

TEST(physics_vector_notation_gradient_and_friends_draw_as_what_they_stand_for) {
  CHECK_PAIRS(after(R"(\usepackage{physics})", Pairs{
    {R"(\vb{x})", R"(\mathbf{x})"},
    {R"(\vb*{x})", R"(\boldsymbol{x})"},
    {R"(\va{x})", R"(\vec{\mathbf{x}})"},
    {R"(\vu{x})", R"(\mathbf{\hat{x}})"},
    {R"(a\vdot b)", R"(a\boldsymbol\cdot b)"},
    {R"(a\cross b)", R"(a\boldsymbol\times b)"},
    {R"(\grad{f})", R"(\boldsymbol\nabla f)"},
    {R"(\div{F})", R"(\boldsymbol\nabla\boldsymbol\cdot F)"},
    {R"(\curl{F})", R"(\boldsymbol\nabla\boldsymbol\times F)"},
    {R"(\laplacian{f})", R"(\nabla^2 f)"},
    {R"(\grad(f))", R"(\boldsymbol\nabla\mathclose{\left(f\right)})"},
  }));
}

TEST(physics_derivatives_draw_as_the_fractions_and_differentials_they_stand_for) {
  CHECK_PAIRS(after(R"(\usepackage{physics})", Pairs{
    {R"(\dd{x})", R"(\mathinner{\mathrm{d}x})"},
    {R"(\dd)", R"(\mathrm{d})"},
    {R"(\dv{f}{x})", R"(\frac{\mathrm{d} f}{\mathrm{d} x})"},
    {R"(\dv[2]{f}{x})", R"(\frac{\mathrm{d}^{2} f}{\mathrm{d} x^{2}})"},
    {R"(\dv{x})", R"(\frac{\mathrm{d}}{\mathrm{d} x})"},
    {R"(\pdv{f}{x})", R"(\frac{\partial f}{\partial x})"},
    {R"(\pdv{f}{x}{y})", R"(\frac{\partial^2 f}{\partial x\partial y})"},
    {R"(\var{F})", R"(\mathinner{\delta F})"},
    {R"(\fdv{F}{g})", R"(\frac{\delta F}{\delta g})"},
  }));
}

TEST(physics_bra_ket_notation_draws_as_the_brackets_and_bars_it_stands_for) {
  CHECK_PAIRS(after(R"(\usepackage{physics})", Pairs{
    {R"(\ket{a})", R"(\left\lvert a\right\rangle)"},
    {R"(\bra{a})", R"(\left\langle a\right\rvert)"},
    {R"(\braket{a}{b})", R"(\left\langle a\middle\vert b\right\rangle)"},
    {R"(\braket{a})", R"(\left\langle a\middle\vert a\right\rangle)"},
    {R"(\bra{a}\ket{b})", R"(\left\langle a\middle\vert b\right\rangle)"},
    {R"(\op{a}{b})", R"(\left\lvert a\middle\rangle\!\middle\langle b\right\rvert)"},
    {R"(\expval{A})", R"(\left\langle A\right\rangle)"},
    {R"(\expval{A}{\psi})",
     R"(\vphantom{A}\left\langle\psi\middle\vert\smash{A}\middle\vert\psi\right\rangle)"},
    {R"(\mel{n}{H}{m})",
     R"(\vphantom{H}\left\langle n\middle\vert\smash{H}\middle\vert m\right\rangle)"},
    {R"(\vev{A})",
     R"(\vphantom{A}\left\langle 0\middle\vert\smash{A}\middle\vert 0\right\rangle)"},
  }));
}

TEST(physics_matrices_operators_and_quick_text_draw_as_what_they_stand_for) {
  CHECK_PAIRS(after(R"(\usepackage{physics})", Pairs{
    {R"(\mqty(a & b \\ c & d))", R"(\mathord{\begin{pmatrix}a & b \\ c & d\end{pmatrix}})"},
    {R"(\mqty[a & b \\ c & d])", R"(\mathord{\begin{bmatrix}a & b \\ c & d\end{bmatrix}})"},
    {R"(\mqty{a & b \\ c & d})", R"(\mathord{\begin{matrix}a & b \\ c & d\end{matrix}})"},
    {R"(\mdet{a & b \\ c & d})", R"(\begin{vmatrix}a & b \\ c & d\end{vmatrix})"},
    {R"(\tr A)", R"(\operatorname{tr} A)"},
    {R"(\tr(A))", R"(\operatorname{tr}\mathclose{\left(A\right)})"},
    {R"(\erf x)", R"(\operatorname{erf} x)"},
    {R"(\qq{and})", R"(\quad\text{and}\quad)"},
    {R"(\qq*{and})", R"(\text{and}\quad)"},
    {R"(x\qc y)", R"(x,\quad y)"},
    {R"(\qif)", R"(\quad\text{if}\quad)"},
  }));
}

TEST(physics_takes_over_qty_bra_and_div_from_siunitx_braket_and_the_division_sign) {
  const auto phys = [](const std::string& tex) { return check::render("\\usepackage{physics}" + tex); };
  CHECK(!check::sameInk(phys(R"(\qty{1}{m})"), check::render(R"(\qty{1}{m})")));
  CHECK(!check::sameInk(phys(R"(a\div b)"), check::render(R"(a\div b)")));
  // What the package replaces is the division sign, still there as \divisionsymbol.
  CHECK(check::sameInk(phys(R"(a\divisionsymbol b)"), check::render(R"(a\div b)")));
}

TEST(physics_a_name_it_defines_is_no_longer_free_to_define_once_it_is_loaded) {
  CHECK(check::render(R"(\newcommand{\abs}[1]{x})").warnings.empty());
  CHECK(check::warns(check::render(R"(\usepackage{physics}\newcommand{\abs}[1]{x})"), "already exists"));
}

TEST(physics_as_a_start_package_is_loaded_before_every_input) {
  check::Options opt;
  opt.packages = {"physics"};
  CHECK_PAIRS_WITH((Pairs{{R"(\abs{x})", R"(\left\lvert x\right\rvert)"}}), opt);
}

// --- \usepackage, as a label meets it ---------------------------------------

TEST(packages_usepackage_of_a_package_we_have_or_have_not_changes_nothing_a_label_draws) {
  CHECK_PAIRS((Pairs{
    {R"(\usepackage{geometry}x)", "x"},
    {R"(\usepackage[utf8]{inputenc}\usepackage{katex}x)", "x"},
    {R"(\usepackage{amsmath,nosuchpackage}\dfrac{a}{b})", R"(\dfrac{a}{b})"},
  }));
}

TEST(packages_a_registered_package_is_read_when_asked_for_and_not_before) {
  microtex::front::registerPackage("drawprobe", "\\newcommand{\\fromdrawprobe}{P}");
  CHECK_PAIRS((Pairs{
    {R"(\usepackage{drawprobe}\fromdrawprobe)", "P"},
    {R"(\RequirePackage{drawprobe}\fromdrawprobe)", "P"},
  }));
  CHECK(check::warns(check::render(R"(\fromdrawprobe)"), "unknown command"));
}

TEST(packages_a_name_from_a_package_the_input_did_not_ask_for_is_its_own_to_define) {
  // LaTeX has no \dfrac without amsmath, no proof without amsthm, and KaTeX's
  // \R is no LaTeX at all.
  CHECK(check::render(R"(\newcommand{\dfrac}[2]{Q}\dfrac{a}{b})").warnings.empty());
  CHECK(check::render(R"(\newenvironment{proof}{}{})").warnings.empty());
  CHECK(check::render(R"(\newcommand{\R}{Q}\R)").warnings.empty());
  CHECK_PAIRS((Pairs{
    {R"(\newcommand{\dfrac}[2]{Q}\dfrac{a}{b})", "Q"},
    {R"(\newcommand{\R}{Q}\R)", "Q"},
  }));
}

TEST(packages_a_name_is_taken_once_the_input_asks_for_its_package_and_always_when_it_is_latexs) {
  for (const char* tex : {R"(\usepackage{amsmath}\newcommand{\dfrac}[2]{Q})",
                          R"(\usepackage{amsthm}\newenvironment{proof}{}{})",
                          // A name of the format's own, which no package gave.
                          R"(\newcommand{\hfill}{Q})", R"(\newcommand{\ldots}{Q})",
                          R"(\newcommand{\frac}{Q})"}) {
    CHECK(check::warns(check::render(tex), "already exists"));
  }
}

TEST(packages_a_package_written_with_conditionals_csname_and_options_works_as_written) {
  microtex::front::registerPackage(
    "condprobe",
    "\\NeedsTeXFormat{LaTeX2e}\n"
    "\\ProvidesPackage{condprobe}[2026/10/11 a probe]\n"
    "\\newif\\ifcompact\n"
    "\\DeclareOption{compact}{\\compacttrue}\n"
    "\\ProcessOptions\\relax\n"
    "\\newcommand{\\gap}{\\ifcompact\\,\\else\\quad\\fi}\n"
    "\\@namedef{unit@m}{\\mathrm{m}}\n"
    "\\newcommand{\\unit}[1]{\\@ifundefined{unit@#1}{\\mathrm{#1}}{\\@nameuse{unit@#1}}}\n");
  CHECK_EQ(microtex::front::checkPackage("condprobe"), check::Strings{});
  CHECK_PAIRS((Pairs{
    {R"(\usepackage{condprobe}a\gap b)", R"(a\quad b)"},
    {R"(\usepackage[compact]{condprobe}a\gap b)", R"(a\,b)"},
    {R"(\usepackage{condprobe}\unit{m})", R"(\mathrm{m})"},
    {R"(\usepackage{condprobe}\unit{kg})", R"(\mathrm{kg})"},
  }));
}

TEST(packages_a_package_written_with_newdocumentcommand_reads_its_arguments_as_the_spec_says) {
  microtex::front::registerPackage(
    "xparseprobe",
    "\\NewDocumentCommand{\\pair}{O{x} m}{\\left(#1, #2\\right)}\n"
    "\\NewDocumentCommand{\\flag}{s m}{\\IfBooleanTF{#1}{\\mathbf{#2}}{#2}}\n"
    "\\NewDocumentCommand{\\opt}{o m}{\\IfNoValueTF{#1}{#2}{#2_{#1}}}\n"
    "\\NewDocumentEnvironment{boxed}{O{b}}{[#1:}{]}\n");
  CHECK_EQ(microtex::front::checkPackage("xparseprobe"), check::Strings{});
  CHECK_PAIRS((Pairs{
    {R"(\usepackage{xparseprobe}\pair{y})", R"(\left(x, y\right))"},
    {R"(\usepackage{xparseprobe}\pair[a]{y})", R"(\left(a, y\right))"},
    {R"(\usepackage{xparseprobe}\flag*{z})", R"(\mathbf{z})"},
    {R"(\usepackage{xparseprobe}\flag{z})", "z"},
    {R"(\usepackage{xparseprobe}\opt[1]{a})", "a_{1}"},
    {R"(\usepackage{xparseprobe}\opt{a})", "a"},
    {R"(\usepackage{xparseprobe}\begin{boxed}x\end{boxed})", "{[b:x]}"},
  }));
}

TEST(packages_what_a_start_package_does_besides_define_is_done_a_definecolor_for_one) {
  microtex::front::registerPackage("colorprobe", "\\definecolor{brandprobe}{rgb}{0,1,0}");
  const std::string tex = R"(\textcolor{brandprobe}{a})";
  CHECK(check::drawsColour(check::render("\\usepackage{colorprobe}" + tex), 0xFF00FF00u));
  check::Options opt;
  opt.packages = {"colorprobe"};
  CHECK(check::drawsColour(check::render(tex, opt), 0xFF00FF00u));
}
