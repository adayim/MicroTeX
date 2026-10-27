// input_mode = document: a LaTeX document body, by TeX's rules (engine.h).
// Positions come from the stub text measurer, so these compare rows, columns
// and sizes with each other and with what the same text is without them.

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Pairs;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

Options typeface(InputMode mode, float width = 0, float size = 20) {
  Options opt(mode, width);
  opt.path = false;
  opt.textSize = size;
  return opt;
}

check::Layout rec(const std::string& tex, InputMode mode = InputMode::document, float width = 0, float size = 20) {
  return check::render(tex, typeface(mode, width, size));
}

// The text records of a layout, top to bottom and left to right.
Records runs(const std::string& tex, InputMode mode = InputMode::document, float width = 0, float size = 20) {
  Records r = check::of(rec(tex, mode, width, size), DrawRecord::TEXT);
  std::stable_sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
  return r;
}

Strings textsOf(const Records& rs) {
  Strings out;
  for (const auto& r : rs) out.push_back(r.text);
  return out;
}

std::vector<float> xsOf(const Records& rs) { return check::column(rs, &DrawRecord::x); }
std::vector<float> ysOf(const Records& rs) { return check::column(rs, &DrawRecord::y); }

// A run's text, x and y, one a line, for a failed check to show.
std::string show(const Records& rs) {
  std::string out;
  char buf[160];
  for (const auto& r : rs) {
    std::snprintf(buf, sizeof buf, "%s @ %.3f, %.3f\n", r.text.c_str(), r.x, r.y);
    out += buf;
  }
  return out;
}

float indentOf(float size = 20) { return rec("\\kern1.5em", InputMode::mixed, 0, size).width; }

// The records whose text is `word`.
Records named(const Records& rs, const std::string& word) {
  Records out;
  for (const auto& r : rs) {
    if (r.text == word) out.push_back(r);
  }
  return out;
}

float first(const std::vector<float>& v) { return v.empty() ? -1e9f : v.front(); }

// The texts of some records, one after the other.
std::string joinText(const Records& rs) {
  std::string s;
  for (const auto& r : rs) s += r.text;
  return s;
}

// The same without the spaces.
std::string allText(const Records& rs) {
  std::string s;
  for (const auto& r : rs) s += r.text;
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

bool close(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

bool closeAll(const std::vector<float>& a, const std::vector<float>& b, float tolerance = 1e-4f) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); i++) {
    if (!close(a[i], b[i], tolerance)) return false;
  }
  return true;
}

// The leftmost x of each line (rows told apart to the pixel), top to bottom.
std::vector<float> lineStarts(const Records& rs) {
  std::map<long, float> least;
  for (const auto& r : rs) {
    const long row = std::lround(r.y);
    const auto it = least.find(row);
    least[row] = it == least.end() ? r.x : std::min(it->second, r.x);
  }
  std::vector<float> out;
  for (const auto& kv : least) out.push_back(kv.second);
  return out;
}

std::size_t rows(const Records& rs, int digits = 0) { return check::distinct(ysOf(rs), digits); }

}  // namespace

TEST(docmode_a_line_end_is_a_space_as_tex_reads_it) {
  // One line, and one run of text.
  const auto d = runs("one\ntwo");
  CHECK_EQ(d.size(), std::size_t(1));
  CHECK_EQ(d[0].text, std::string("one two"));
  // In a label it is two lines: mixed mode's one departure from TeX.
  const auto m = runs("one\ntwo", InputMode::mixed);
  CHECK_EQ(textsOf(m), Strings({"one", "two"}));
  CHECK(m[0].y != m[1].y);
}

TEST(docmode_a_paragraph_neither_starts_nor_ends_with_a_space_as_in_tex) {
  CHECK_EQ(show(runs("\nText.\n")), show(runs("Text.")));
  CHECK_EQ(show(runs("  Text.  ")), show(runs("Text.")));
}

TEST(docmode_a_blank_line_starts_a_paragraph_however_many_there_are) {
  const auto one = runs("one\n\ntwo");
  CHECK_EQ(one.size(), std::size_t(2));
  CHECK(one[0].y != one[1].y);
  // Two blank lines are one paragraph break, not two.
  CHECK(closeAll(ysOf(runs("one\n\n\n\ntwo")), ysOf(one)));
  // \par is the same break spelled out.
  CHECK(closeAll(ysOf(runs("one\\par two")), ysOf(one)));
}

TEST(docmode_a_paragraph_indents_its_first_line_by_texs_parindent) {
  // 1.5em, which follows the font size.
  for (const float size : {8.0f, 12.0f, 20.0f}) {
    const float indent = indentOf(size);
    CHECK(indent > 0);
    // Every paragraph is indented, the first one included.
    CHECK(closeAll(xsOf(runs("one\n\ntwo", InputMode::document, 0, size)), {indent, indent}));
  }
  // A label is not a document: no indent there.
  CHECK(closeAll(xsOf(runs("one\ntwo", InputMode::mixed)), {0, 0}));
}

TEST(docmode_math_and_environments_are_read_in_a_document_as_anywhere_else) {
  CHECK_EQ(runs("Total $x^2$ here").size(), std::size_t(2));  // "Total " and " here"
  const auto d = rec("a\n\n$$x$$\n\nb");
  CHECK(check::count(d, DrawRecord::TEXT) > 0);
  // A display formula is its own row, between the paragraphs.
  CHECK_EQ(check::distinct(ysOf(check::ink(d)), 2), std::size_t(3));
}

TEST(docmode_a_display_is_centred_on_a_line_of_its_own_as_in_latex) {
  const auto at = [](const std::string& tex, float width = 0) {
    return check::minimum(xsOf(check::of(rec(tex, InputMode::document, width), DrawRecord::GLYPH)));
  };
  const std::string tex = "Before\n\\[ x^2 + y^2 \\]\nafter.";
  // 100 more text width moves it 50 to the right.
  CHECK(close(at(tex, 400) - at(tex, 300), 50, 1e-4f));
  CHECK_EQ(rec(tex, InputMode::document, 300).width, 300);
  const float dw = static_cast<float>(rec(R"(\displaystyle x^2 + y^2)", InputMode::math).width);
  CHECK(std::fabs(at(tex, 300) - (300 - dw) / 2) < 1);
  // With no text width, it is centred on the widest line.
  CHECK(close(at("\\kern50pt " + tex) - at(tex), 50, 1e-4f));  // 50pt is 100px at the 20px of these tests
  // Each display LaTeX has sits between the text around it, starred or not.
  for (const char* d : {R"(\[x\])", R"($$x$$)", R"(\begin{equation}x\end{equation})",
                        R"(\begin{equation*}x\end{equation*})", R"(\begin{align*}x\end{align*})",
                        R"(\begin{gather}x\end{gather})", R"(\begin{displaymath}x\end{displaymath})"}) {
    const auto l = rec(std::string("Before ") + d + " after.");
    const auto glyphs = check::of(l, DrawRecord::GLYPH);
    // A numbered display has its number among the text records, on the
    // display's own line: only the prose above and below it is counted.
    Records prose;
    for (const auto& t : check::of(l, DrawRecord::TEXT)) {
      bool onDisplay = false;
      for (const auto& g : glyphs) onDisplay = onDisplay || std::lround(g.y) == std::lround(t.y);
      if (!onDisplay) prose.push_back(t);
    }
    CHECK_EQ(check::distinct(ysOf(prose), 6), std::size_t(2));
    const float gy = check::maximum(ysOf(glyphs));
    CHECK(gy > check::minimum(ysOf(prose)) && gy < check::maximum(ysOf(prose)));
  }
  // In a label, display math stays in its line.
  CHECK_EQ(rows(runs("a $$x$$ b", InputMode::mixed), 6), std::size_t(1));
}

TEST(docmode_a_display_interrupts_its_paragraph_without_ending_it) {
  const float indent = indentOf();
  // What follows the display goes on unindented, as in TeX ...
  CHECK(closeAll(xsOf(runs("Before\n\\[ x \\]\nafter.")), {indent, 0}));
  // ... unless a blank line starts a new paragraph.
  CHECK(closeAll(xsOf(runs("Before\n\\[ x \\]\n\nNew.")), {indent, indent}));
}

TEST(docmode_a_display_environment_is_in_display_style_as_a_bracket_display_is) {
  // In display style a fraction's numerator is at the body size.
  for (const char* d : {"\\[\\frac{a}{b}\\]", "\\begin{equation}\\frac{a}{b}\\end{equation}",
                        "\\begin{align*}\\frac{a}{b}\\end{align*}"}) {
    const auto l = rec(std::string("x ") + d);
    std::vector<float> sizes;
    for (const auto& r : l.records) {
      if (r.type == DrawRecord::GLYPH || r.type == DrawRecord::TEXT) sizes.push_back(r.font_size);
    }
    CHECK(check::uniqueSorted(sizes, 3) == std::vector<double>({20}));
  }
}

TEST(docmode_a_list_or_a_float_is_set_apart_from_its_paragraph) {
  // A list is a block; the text after it goes on unindented.
  const auto d = runs("Before\n\\begin{itemize}\\item one\\end{itemize}\nafter.");
  CHECK_EQ(textsOf(d), Strings({"Before", "one", "after."}));
  CHECK_EQ(rows(d), std::size_t(3));
  CHECK_EQ(first(xsOf(named(d, "after."))), 0.0f);
  // A float is set where it is written, on lines of its own.
  const auto f = runs("Before \\begin{table}\\caption{Cap}\\begin{tabular}{l}cell\\end{tabular}\\end{table} after.");
  CHECK_EQ(textsOf(f), Strings({"Before", "Table 1: ", "Cap", "cell", "after."}));
  CHECK_EQ(rows(f), std::size_t(4));
  // In a label a list is part of the line.
  const auto m = runs("Before \\begin{itemize}\\item one\\end{itemize}", InputMode::mixed);
  CHECK(first(xsOf(named(m, "one"))) > static_cast<float>(rec("Before", InputMode::mixed).width));
}

// pdflatex: a figure's caption is "Figure n: text" and a table's "Table n: text", each counting its own; a
// \caption* is the text alone.
TEST(docmode_a_floats_caption_is_numbered_by_its_kind) {
  const auto f = runs("\\begin{figure}\\caption{A}\\end{figure}\n\\begin{table}\\caption{B}\\end{table}\n"
                      "\\begin{figure}\\caption{C}\\end{figure}\n\\begin{figure}\\caption*{D}\\end{figure}");
  CHECK_EQ(textsOf(f), Strings({"Figure 1: ", "A", "Table 1: ", "B", "Figure 2: ", "C", "D"}));
}

// pdflatex with subcaption: subfigures are lettered "(a) First", the figure's own caption follows ("Figure 1:
// Both"), \ref{sa} is 1a, \subref{sa} is a, and the next figure starts again at (a).
TEST(docmode_a_subfigure_is_lettered_and_its_label_is_the_figures_number_and_the_letter) {
  const std::string body =
    "\\begin{figure}\\begin{subfigure}{0.45\\textwidth}AAA\\caption{First}\\label{sa}\\end{subfigure}\\hfill"
    "\\begin{subfigure}{0.45\\textwidth}BBB\\caption{Second}\\label{sb}\\end{subfigure}"
    "\\caption{Both}\\label{f1}\\end{figure}\n"
    "\\begin{figure}\\begin{subfigure}{0.9\\textwidth}CCC\\caption{Third}\\label{sc}\\end{subfigure}"
    "\\caption{Next}\\label{f2}\\end{figure}\n\n";
  const auto f = runs(body + "REFS \\ref{f1} \\ref{sa} \\subref{sa} \\ref{sb} \\ref{f2} \\ref{sc}");
  const Strings t = textsOf(f);
  for (const char* s : {"(a)", "First", "(b)", "Second", "Figure 1: ", "Both", "(a)", "Third", "Figure 2: ", "Next"}) {
    CHECK(std::find(t.begin(), t.end(), s) != t.end());
  }
  std::string refs;
  for (const auto& s : t) refs += s;
  CHECK(refs.find("REFS 1 1a a 1b 2 2a") != std::string::npos);
  CHECK(!check::warns(rec(body, InputMode::document), "undefined"));
}

// \vspace between two lines of a document is TeX's glue: added to the baselineskip glue, so `A\par\vspace{20pt}B` is 20pt
// farther apart than `A\par B` (pdflatex: 12 and 32). It was the next line's height, short by a line. \smallskip,
// \medskip and \bigskip are LaTeX's 3, 6 and 12pt, which at a size of 10 are 0.3, 0.6 and 1.2em.
TEST(docmode_vspace_adds_to_the_baselineskip_between_two_lines) {
  const float em = 20;
  const auto pitch = [&](const std::string& tex) {
    const auto r = runs(tex, InputMode::document, 400);
    return first(ysOf(named(r, "B"))) - first(ysOf(named(r, "A")));
  };
  const float plain = pitch("A\\par B");
  CHECK(close(pitch("A\\par\\vspace{1em}B") - plain, em, 0.01f));
  CHECK(close(pitch("A\\par\\vspace{2em}B") - plain, 2 * em, 0.01f));
  CHECK(close(pitch("A\\par\\vspace{1em}\\vspace{1em}B") - plain, 2 * em, 0.01f));
  CHECK(close(pitch("A\\par\\vspace{10pt}\\vspace{1cm}B") - pitch("A\\par\\vspace{1cm}\\vspace{10pt}B"), 0.f, 0.01f));
  CHECK(close(pitch("A\\par\\smallskip B") - plain, 0.3f * em, 0.01f));
  CHECK(close(pitch("A\\par\\medskip B") - plain, 0.6f * em, 0.01f));
  CHECK(close(pitch("A\\par\\bigskip B") - plain, 1.2f * em, 0.01f));
  // Inside a paragraph it lands after the line it stands in (vadjust): before the next line, which here is a new paragraph.
  const auto after = [&](const std::string& tex) {
    const auto r = runs(tex, InputMode::document, 400);
    return first(ysOf(named(r, "B"))) - first(ysOf(named(r, "Z")));
  };
  CHECK(close(after("Z\\par A \\vspace{1em} x\\par B") - after("Z\\par A x\\par B"), em, 0.01f));
  // Before anything is drawn it is what it was, the height of the first line (TeX would drop it at the top of a page).
  const auto top = runs("\\vspace{3em}A", InputMode::document, 400);
  CHECK(first(ysOf(named(top, "A"))) > first(ysOf(named(runs("A", InputMode::document, 400), "A"))) + em);
}

// cleveref's \cref and \Cref and hyperref's \autoref name the kind of what a label labels, as pdflatex sets them
// (scratchpad/col/rc.tex, ra.tex, c.tex; every expected text below is one of its outputs): \cref abbreviates
// ("eq. (1)", "fig. 1"), \Cref writes the capitalised full name, \autoref hyperref's; labels of one kind are joined
// "(1), (3) and (5)" and three consecutive ones are "(1) to (3)", kinds are joined "A, B, and C".
// \crefname, \Crefname and \creflabelformat change what a kind is called and how its number is set, as cleveref's do; every
// expected text is cleveref's output in LuaLaTeX for this preamble (two runs).
TEST(docmode_crefname_crefname_and_creflabelformat_rename_a_kind_and_reformat_its_number) {
  const std::string pre =
    "\\crefname{figure}{Fig.}{Figs.}\\Crefname{figure}{Figure!}{Figures!}\\creflabelformat{equation}{[#2#1#3]}\n"
    "\\crefname{table}{tab}{tabs}\n"
    "\\begin{equation}a\\label{e1}\\end{equation}\\begin{equation}a\\label{e2}\\end{equation}\n"
    "\\begin{figure}\\caption{F}\\label{f1}\\end{figure}\\begin{figure}\\caption{F}\\label{f2}\\end{figure}\n"
    "\\begin{table}\\caption{T}\\label{t1}\\end{table}\n\n";
  const auto drawn = [&](const std::string& ref) {
    std::string all;
    for (const auto& r : runs(pre + "XX " + ref, InputMode::document, 600)) all += r.text;
    all.erase(std::remove(all.begin(), all.end(), ' '), all.end());
    const auto at = all.rfind("XX");
    return at == std::string::npos ? all : all.substr(at + 2);
  };
  const std::vector<std::pair<std::string, std::string>> cases = {
    {"\\cref{e1}", "eq.[1]"},          {"\\cref{e1,e2}", "eqs.[1]and[2]"}, {"\\cref{f1}", "Fig.1"},
    {"\\cref{f1,f2}", "Figs.1and2"},   {"\\Cref{f1}", "Figure!1"},         {"\\Cref{f1,f2}", "Figures!1and2"},
    {"\\cref{t1}", "tab1"},            {"\\cref{e1,f1}", "eq.[1]andFig.1"}, {"\\crefrange{f1}{f2}", "Figs.1to2"},
    // hyperref's names are its own
    {"\\autoref{f1}", "Figure1"}};
  for (const auto& c : cases) CHECK_EQ(drawn(c.first), c.second);
}

TEST(docmode_cref_cref_and_autoref_name_the_kind_of_a_label_as_cleveref_and_hyperref_do) {
  const std::string pre =
    "\\newtheorem{thm}{Theorem}\\newtheorem{lem}{Lemma}\n"
    "\\section{First}\\label{s1}\n\\subsection{Sub}\\label{ss1}\n"
    "\\begin{equation}a\\label{e1}\\end{equation}\\begin{equation}a\\label{e2}\\end{equation}\n"
    "\\begin{equation}a\\label{e3}\\end{equation}\\begin{equation}a\\label{e4}\\end{equation}\n"
    "\\begin{equation}a\\label{e5}\\end{equation}\n"
    "\\begin{figure}\\caption{F}\\label{f1}\\end{figure}\\begin{figure}\\caption{F}\\label{f2}\\end{figure}\n"
    "\\begin{table}\\caption{T}\\label{t1}\\end{table}\n"
    "\\begin{thm}X\\label{th1}\\end{thm}\\begin{lem}X\\label{lm1}\\end{lem}\n"
    "\\begin{align}b &= 1\\label{a1}\\\\ c &= 2\\label{a2}\\end{align}\n\n";
  const auto drawn = [&](const std::string& ref) {
    std::string all;
    for (const auto& r : runs(pre + "XX " + ref, InputMode::document, 600)) all += r.text;
    all.erase(std::remove(all.begin(), all.end(), ' '), all.end());
    const auto at = all.rfind("XX");
    return at == std::string::npos ? all : all.substr(at + 2);
  };
  const std::vector<std::pair<std::string, std::string>> cases = {
    {"\\cref{e1}", "eq.(1)"}, {"\\Cref{e1}", "Equation(1)"}, {"\\autoref{e1}", "Equation1"},
    {"\\cref{e1,e2}", "eqs.(1)and(2)"}, {"\\cref{e1,e2,e3}", "eqs.(1)to(3)"}, {"\\cref{e1,e3}", "eqs.(1)and(3)"},
    {"\\cref{e1,e3,e5}", "eqs.(1),(3)and(5)"}, {"\\cref{e1,e2,e4}", "eqs.(1),(2)and(4)"},
    {"\\cref{e1,e2,e3,e5}", "eqs.(1)to(3)and(5)"}, {"\\cref{e3,e1}", "eqs.(1)and(3)"},
    {"\\cref{e1,f1}", "eq.(1)andfig.1"}, {"\\cref{e1,f1,t1}", "eq.(1),fig.1,andtable1"},
    {"\\cref{e1,e2,f1,f2}", "eqs.(1)and(2)andfigs.1and2"}, {"\\Cref{e1,e2}", "Equations(1)and(2)"},
    {"\\cref{f1}", "fig.1"}, {"\\Cref{f1}", "Figure1"}, {"\\autoref{f1}", "Figure1"}, {"\\cref{f1,f2}", "figs.1and2"},
    {"\\cref{t1}", "table1"}, {"\\autoref{t1}", "Table1"},
    {"\\cref{s1}", "section1"}, {"\\Cref{s1}", "Section1"}, {"\\autoref{s1}", "section1"},
    {"\\cref{ss1}", "section1.1"}, {"\\autoref{ss1}", "subsection1.1"}, {"\\cref{s1,ss1}", "sections1and1.1"},
    {"\\cref{th1}", "theorem1"}, {"\\Cref{th1}", "Theorem1"}, {"\\cref{lm1}", "lemma1"}, {"\\Cref{lm1}", "Lemma1"},
    {"\\cref{th1,lm1}", "theorem1andlemma1"},
    {"\\crefrange{e1}{e3}", "eqs.(1)to(3)"}, {"\\crefrange{e1}{e5}", "eqs.(1)to(5)"},
    {"\\Crefrange{f1}{f2}", "Figures1to2"}, {"\\crefrange{e1}{f1}", "eq.(1)andfig.1"},
    {"\\cref{a1,a2}", "eqs.(6)and(7)"}};
  for (const auto& c : cases) CHECK_EQ(drawn(c.first), c.second);
  // hyperref prints only the number for a theorem, with a warning; a title is a better name for it.
  CHECK_EQ(drawn("\\autoref{th1}"), std::string("Theorem1"));
  // A key no label names is ??, once, as \ref does; \ref is as it was.
  CHECK_EQ(drawn("\\cref{nosuch}"), std::string("??"));
  CHECK(check::warns(rec(pre + "\\cref{e1,nosuch} \\autoref{x}", InputMode::document, 600), "reference `nosuch' is undefined"));
  CHECK(check::warns(rec(pre + "\\autoref{x}", InputMode::document, 600), "reference `x' is undefined"));
  CHECK(!check::warns(rec(pre + "\\cref{e1} \\Cref{f1} \\autoref{s1}", InputMode::document, 600), "undefined"));
  CHECK_EQ(drawn("\\ref{e2}"), std::string("2"));
  // A reference may come before its label, and a sub-figure is its figure's number and a letter.
  CHECK(textsOf(runs("\\cref{late}\n\\begin{equation}a\\label{late}\\end{equation}")).front().find("eq.") == 0);
  const std::string sub =
    "\\begin{figure}\\begin{subfigure}{5cm}X\\caption{A}\\label{sa}\\end{subfigure}\\caption{M}\\end{figure}\nXX ";
  std::string all;
  for (const auto& r : runs(sub + "\\cref{sa}", InputMode::document, 600)) all += r.text;
  all.erase(std::remove(all.begin(), all.end(), ' '), all.end());
  CHECK(all.size() >= 6 && all.substr(all.size() - 6) == "fig.1a");
}

// article's \maketitle (scratchpad/col/mt.tex, pdflatex at 10pt): the title in \LARGE, the authors in \large in tabulars
// 1em apart for \and, the date in \large, all centred, one after the other, then the body 1.5em below; an element that
// was not given is left out, and \thanks makes footnotes marked by symbols, the counter 0 again after.
TEST(docmode_maketitle_sets_the_title_the_authors_and_the_date_centred_one_after_the_other) {
  const auto f = runs("\\title{Tit}\\author{Ann \\and Bob}\\date{Day}\\maketitle\nBody", InputMode::document, 400);
  CHECK_EQ(textsOf(f), Strings({"Tit", "Ann", "Bob", "Day", "Body"}));
  CHECK(close(first(ysOf(named(f, "Ann"))), first(ysOf(named(f, "Bob"))), 0.01f));
  CHECK(first(ysOf(named(f, "Tit"))) < first(ysOf(named(f, "Ann"))));
  CHECK(first(ysOf(named(f, "Ann"))) < first(ysOf(named(f, "Day"))));
  CHECK(first(ysOf(named(f, "Day"))) < first(ysOf(named(f, "Body"))));
  CHECK(first(xsOf(named(f, "Ann"))) < first(xsOf(named(f, "Bob"))));
  // Centred as `center` centres: the same place as the title set by hand.
  const auto by_hand = runs("\\begin{center}{\\LARGE Tit\\par}\\end{center}", InputMode::document, 400);
  CHECK(close(first(xsOf(named(f, "Tit"))), first(xsOf(named(by_hand, "Tit"))), 0.01f));
  // The date, from \today when none is given, and nothing when it is empty.
  CHECK(textsOf(runs("\\title{T}\\maketitle", InputMode::document, 400)).size() > 1);
  CHECK_EQ(textsOf(runs("\\title{T}\\date{}\\maketitle", InputMode::document, 400)), Strings({"T"}));
  CHECK_EQ(textsOf(runs("\\author{A}\\date{}\\maketitle\nB", InputMode::document, 400)), Strings({"A", "B"}));
  CHECK_EQ(textsOf(runs("\\maketitle\nB", InputMode::document, 400)).back(), std::string("B"));
  // \today is one run of text, "Month D, YYYY"; the title is drawn once, as article does (a second \maketitle is \relax).
  const auto today = textsOf(runs("\\today", InputMode::document, 400));
  CHECK_EQ(today.size(), std::size_t(1));
  CHECK(today[0].size() > 10 && today[0].find(", 20") != std::string::npos);
  CHECK_EQ(textsOf(runs("\\title{T}\\date{}\\maketitle\\maketitle", InputMode::document, 400)), Strings({"T"}));
  // The preamble's \title and \author are read, and draw nothing there.
  CHECK_EQ(textsOf(runs("\\documentclass{article}\\title{P}\\author{Q}\\date{R}\\begin{document}\\maketitle\\end{document}",
                        InputMode::document, 400)),
           Strings({"P", "Q", "R"}));
}

TEST(docmode_thanks_in_a_title_is_a_symbol_footnote_and_the_counter_starts_again_after_it) {
  const auto f = runs("\\title{T\\thanks{Funded.}}\\author{A\\thanks{Dept.}\\and B}\\date{}\\maketitle\nBody\\footnote{Note.}",
                      InputMode::document, 400);
  const Strings t = textsOf(f);
  // The two title marks are symbols, the body's own footnote is the number 1.
  CHECK(std::find(t.begin(), t.end(), "*") != t.end());
  CHECK(std::find(t.begin(), t.end(), "\xe2\x80\xa0") != t.end());  // the dagger
  CHECK(std::find(t.begin(), t.end(), "Funded.") != t.end());
  CHECK(std::find(t.begin(), t.end(), "Dept.") != t.end());
  CHECK(std::find(t.begin(), t.end(), "1") != t.end());
  CHECK(std::find(t.begin(), t.end(), "Note.") != t.end());
}

// caption.sty's \abovecaptionskip is 10pt above a float's caption, subcaption.sty's skip 6pt above a sub-caption (in
// \small): pdflatex's rows, a table's body to its caption 18bp against the 12bp of two lines. Here, in em.
TEST(docmode_a_float_caption_stands_below_its_body_by_the_caption_skip) {
  const auto two = runs("X\\par Y");
  const float plain = first(ysOf(named(two, "Y"))) - first(ysOf(named(two, "X")));
  const auto fig = runs("\\begin{figure}X\\caption{C}\\end{figure}");
  const float main = first(ysOf(named(fig, "Figure 1: "))) - first(ysOf(named(fig, "X")));
  const auto sub = runs("\\begin{figure}\\begin{subfigure}{5cm}X\\caption{C}\\end{subfigure}\\end{figure}");
  const float lettered = first(ysOf(named(sub, "(a)"))) - first(ysOf(named(sub, "X")));
  CHECK(main > plain + 5.f);
  CHECK(lettered > plain - 1.f);
  CHECK(lettered < main);
}

// tcolorbox.sty's defaults (size=normal), read off pdflatex's ink at 10pt: a frame of 1.42bp (boxrule=0.5mm) in
// black!75!white (#404040), its inside black!5!white (#F2F2F2) with 1mm of arc (the frame's own corners 1.5mm), the text
// 4mm + 1mm boxsep from the inside edge and 2mm + 1mm from the top, the box as wide as the line. Here in em.
TEST(docmode_a_tcolorbox_is_a_framed_rounded_box_as_wide_as_the_line) {
  const float em = 20;
  const auto l = rec("\\begin{tcolorbox}Hello\\end{tcolorbox}", InputMode::document, 400);
  const auto fills = check::of(l, DrawRecord::FILL_ROUND_RECT);
  CHECK_EQ(fills.size(), std::size_t(2));
  if (fills.size() == 2) {
    CHECK(fills[0].col == 0xFF404040u && fills[1].col == 0xFFF2F2F2u);
    CHECK(close(fills[0].width, 400.f, 0.01f));
    CHECK(close(fills[0].width - fills[1].width, 2 * 0.142264f * em, 0.01f));
    CHECK(close(fills[0].height - fills[1].height, 2 * 0.142264f * em, 0.01f));
    CHECK(close(fills[1].rx, 0.284528f * em, 0.01f));
    CHECK(close(fills[0].rx, (0.142264f + 0.284528f) * em, 0.01f));
    // The text stands the frame, `left` and `boxsep` in; the padding above and below it is top + boxsep.
    CHECK(close(first(xsOf(runs("\\begin{tcolorbox}Hello\\end{tcolorbox}", InputMode::document, 400))),
                (0.142264f + 1.138112f + 0.284528f) * em, 0.01f));
    CHECK(close(fills[1].height - rec("Hello", InputMode::document, 400).height, 2 * (0.569056f + 0.284528f) * em, 0.5f));
  }
  // Set off from the text around it as a list is, by half a baselineskip.
  CHECK_EQ(textsOf(runs("Before\n\\begin{tcolorbox}in\\end{tcolorbox}\nafter.")), Strings({"Before", "in", "after."}));
}

// A title is in a band of the frame's colour, white, above the body, with a rule of the frame's thickness between them
// (pdflatex: band = text + 2mm boxsep, then the 0.5mm rule); colbacktitle fills the band, coltitle colours the text.
TEST(docmode_a_tcolorbox_title_is_a_band_above_the_body) {
  const auto l = rec("\\begin{tcolorbox}[title=Head]Body\\end{tcolorbox}", InputMode::document, 400);
  const auto text = check::of(l, DrawRecord::TEXT);
  CHECK_EQ(text.size(), std::size_t(2));
  if (text.size() == 2) {
    const auto& head = text[0].text == "Head" ? text[0] : text[1];
    const auto& body = text[0].text == "Head" ? text[1] : text[0];
    CHECK(head.y < body.y);
    CHECK(head.col == 0xFFFFFFFFu);
    CHECK(close(head.x, body.x, 0.01f));
  }
  CHECK_EQ(check::of(l, DrawRecord::FILL_ROUND_RECT).size(), std::size_t(2));
  const auto filled = rec("\\begin{tcolorbox}[title=Head,colbacktitle=red,coltitle=black,colback=yellow,colframe=blue]"
                          "Body\\end{tcolorbox}", InputMode::document, 400);
  CHECK(check::drawsColour(filled, 0xFFFF0000u));
  CHECK(check::drawsColour(filled, 0xFFFFFF00u));
  CHECK(check::drawsColour(filled, 0xFF0000FFu));
  CHECK(check::of(filled, DrawRecord::FILL_ROUND_RECT).size() >= 3);
  // The band and its rule make it taller than a box with no title.
  const float plain = check::of(rec("\\begin{tcolorbox}Body\\end{tcolorbox}", InputMode::document, 400),
                                DrawRecord::FILL_ROUND_RECT)[0].height;
  CHECK(check::of(l, DrawRecord::FILL_ROUND_RECT)[0].height > plain + 20.f);
}

TEST(docmode_a_tcolorbox_key_that_is_not_drawn_warns_and_sharp_corners_have_no_arc) {
  CHECK(check::warns(rec("\\begin{tcolorbox}[foo=1]x\\end{tcolorbox}"), "tcolorbox: the key `foo' is not drawn"));
  CHECK(check::warns(rec("\\begin{tcolorbox}[colback=nosuchcolour]x\\end{tcolorbox}"), "undefined colour `nosuchcolour'"));
  CHECK(check::warns(rec("\\begin{tcolorbox}[boxrule=-3pt]x\\end{tcolorbox}"), "not a length of zero or more"));
  // An empty body is a box with nothing in it, and an empty multicols draws nothing.
  CHECK_EQ(check::of(rec("\\begin{tcolorbox}\\end{tcolorbox}"), DrawRecord::FILL_ROUND_RECT).size(), std::size_t(2));
  CHECK(check::of(rec("\\begin{multicols}{2}\\end{multicols}"), DrawRecord::TEXT).empty());
  CHECK(check::of(rec("\\begin{tcolorbox}[sharp corners]x\\end{tcolorbox}"), DrawRecord::FILL_ROUND_RECT).empty());
  CHECK(!check::of(rec("\\begin{tcolorbox}[sharp corners]x\\end{tcolorbox}"), DrawRecord::FILL_RECT).empty());
}

// multicol, in pdflatex (10pt, 345pt wide, \columnsep 10pt): seven one-line paragraphs in two columns are four and
// three, the second column starting 177.5pt in, (345 - 10) / 2 + 10; ten in three are four, four and two (a1..a4 | a5..a8 |
// a9 a10); the header is set in the whole width and the columns start a \multicolsep (12pt) below its line, the same
// again below them; with no header the skip is above them as well. Here in em, at 400 wide and a size of 20.
TEST(docmode_multicols_balances_its_body_in_columns_a_columnsep_apart) {
  const std::string seven = "one\\par two\\par three\\par four\\par five\\par six\\par seven";
  const auto two = runs("\\begin{multicols}{2}" + seven + "\\end{multicols}", InputMode::document, 400);
  const auto at = [&](const Records& rs, const std::string& word) { return named(rs, word); };
  const float second = (400.f - 20.f) / 2 + 20.f;
  for (const char* w : {"one", "two", "three", "four"}) CHECK_EQ(first(xsOf(at(two, w))), 0.f);
  for (const char* w : {"five", "six", "seven"}) CHECK(close(first(xsOf(at(two, w))), second, 0.01f));
  CHECK(close(first(ysOf(at(two, "one"))), first(ysOf(at(two, "five"))), 0.01f));
  // The short column is stretched to the height of the tallest: pdflatex has five, six, seven at 163, 181, 199 where
  // one..four are at 163, 175, 187, 199 -- the glue between its paragraphs takes the missing line, evenly.
  CHECK(close(first(ysOf(at(two, "seven"))), first(ysOf(at(two, "four"))), 0.01f));
  const float gapA = first(ysOf(at(two, "six"))) - first(ysOf(at(two, "five")));
  const float gapB = first(ysOf(at(two, "seven"))) - first(ysOf(at(two, "six")));
  CHECK(close(gapA, gapB, 0.01f));
  CHECK(gapA > first(ysOf(at(two, "two"))) - first(ysOf(at(two, "one"))) + 1.f);
  const auto three = runs("\\begin{multicols}{3}ant\\par bee\\par cat\\par dog\\par elk\\par fox\\par gnu\\par hen\\par "
                          "ibis\\par jay\\end{multicols}", InputMode::document, 400);
  const float middle = (400.f - 40.f) / 3 + 20.f;
  CHECK_EQ(first(xsOf(at(three, "dog"))), 0.f);
  CHECK(close(first(xsOf(at(three, "elk"))), middle, 0.01f));
  CHECK(close(first(xsOf(at(three, "hen"))), middle, 0.01f));
  CHECK(close(first(xsOf(at(three, "ibis"))), 2 * middle, 0.01f));
  CHECK(close(first(xsOf(at(three, "jay"))), 2 * middle, 0.01f));
  // A column two lines short (pdflatex: g9, g10 at 438, 450) is not stretched.
  CHECK(close(first(ysOf(at(three, "jay"))) - first(ysOf(at(three, "ibis"))),
              first(ysOf(at(three, "bee"))) - first(ysOf(at(three, "ant"))), 0.01f));
}

TEST(docmode_multicols_has_a_header_and_a_multicolsep_round_it) {
  const auto plain = runs("one\\par two", InputMode::document, 400);
  const float line = first(ysOf(named(plain, "two"))) - first(ysOf(named(plain, "one")));
  const float em = 20;
  // Header to the first line of the columns: a line and the 12pt; the text before it to the header: a line.
  const auto h = runs("Before\\par\\begin{multicols}{2}[Head]a\\par b\\end{multicols}\nAfter", InputMode::document, 400);
  CHECK(close(first(ysOf(named(h, "Head"))) - first(ysOf(named(h, "Before"))), line, 0.5f));
  CHECK(close(first(ysOf(named(h, "a"))) - first(ysOf(named(h, "Head"))), line + 1.2f * em, 0.5f));
  // Four lines in two columns: the skip is measured from the last line, not from the first.
  const auto k = runs("\\begin{multicols}{2}a\\par b\\par c\\par d\\end{multicols}\nAfter", InputMode::document, 400);
  CHECK(close(first(ysOf(named(k, "After"))) - first(ysOf(named(k, "b"))), line + 1.2f * em, 0.5f));
  // Below the columns the skip again; with no header, above them as well.
  CHECK(close(first(ysOf(named(h, "After"))) - first(ysOf(named(h, "b"))), line + 1.2f * em, 0.5f));
  const auto n = runs("Before\\par\\begin{multicols}{2}a\\par b\\end{multicols}", InputMode::document, 400);
  CHECK(close(first(ysOf(named(n, "a"))) - first(ysOf(named(n, "Before"))), line + 1.2f * em, 0.5f));
}

TEST(docmode_columnbreak_ends_a_column_early) {
  const std::string body = "\\begin{multicols}{2}a\\par b\\par c\\columnbreak\\par d\\end{multicols}";
  const auto f = runs(body, InputMode::document, 400);
  CHECK_EQ(first(xsOf(named(f, "c"))), 0.f);
  CHECK(close(first(xsOf(named(f, "d"))), 210.f, 0.01f));
  CHECK(close(first(ysOf(named(f, "d"))), first(ysOf(named(f, "a"))), 0.01f));
  // And with no break the four lines are two and two.
  const auto g = runs("\\begin{multicols}{2}a\\par b\\par c\\par d\\end{multicols}", InputMode::document, 400);
  CHECK(close(first(xsOf(named(g, "c"))), 210.f, 0.01f));
}

// multicol breaks a list between its items; a tabular is one box and stays in one column.
TEST(docmode_multicols_breaks_a_list_between_items_but_not_a_table) {
  const auto list = runs("\\begin{multicols}{2}\\begin{itemize}\\item ant\\item bee\\item cat\\item dog\\end{itemize}"
                         "\\end{multicols}", InputMode::document, 400);
  const float second = (400.f - 20.f) / 2 + 20.f;
  CHECK(first(xsOf(named(list, "bee"))) < 100.f);
  CHECK(first(xsOf(named(list, "cat"))) > second);
  CHECK(close(first(ysOf(named(list, "cat"))), first(ysOf(named(list, "ant"))), 0.01f));
  const auto table = runs("\\begin{multicols}{2}\\begin{tabular}{l}ant\\\\bee\\\\cat\\\\dog\\end{tabular}\\end{multicols}",
                          InputMode::document, 400);
  CHECK(close(first(xsOf(named(table, "dog"))), first(xsOf(named(table, "ant"))), 0.01f));
  CHECK(first(ysOf(named(table, "dog"))) > first(ysOf(named(table, "ant"))));
}

TEST(docmode_center_and_centering_centre_a_documents_lines) {
  const auto xOf = [](const std::string& tex, const std::string& word, float width, InputMode mode = InputMode::document) {
    return first(xsOf(named(runs(tex, mode, width), word)));
  };
  // 100 more text width moves a line 50 right; the text after is flush.
  for (const char* tex : {"\\begin{center}Short\\end{center}\nafter", "\\begin{figure}\\centering Short\\end{figure}\nafter"}) {
    CHECK(close(xOf(tex, "Short", 400) - xOf(tex, "Short", 300), 50, 1e-4f));
    CHECK_EQ(xOf(tex, "after", 300), 0.0f);
  }
  // A float without \centering is flush left, as LaTeX sets it.
  CHECK_EQ(xOf("\\begin{figure}Short\\end{figure}", "Short", 300), 0.0f);
  // A label is not a document: \centering does nothing there.
  CHECK(closeAll(xsOf(runs("\\centering Short", InputMode::mixed)), {0}));
}

TEST(docmode_a_heading_reads_latexs_optional_short_title_and_a_spaced_star) {
  CHECK_EQ(textsOf(runs("\\section[Intro]{Introduction}\nText.")), Strings({"1", "Introduction", "Text."}));
  // LaTeX finds the star past a space.
  CHECK_EQ(textsOf(runs("\\section *{A}\nText")), Strings({"A", "Text"}));
}

TEST(docmode_paragraph_starts_a_paragraph_of_its_own_flush_left_and_runs_in) {
  const auto d = runs("Para one.\n\n\\paragraph{Run} Text after.");
  CHECK_EQ(first(xsOf(named(d, "Run"))), 0.0f);
  float after = -1;
  for (const auto& r : d) {
    if (r.text.find("Text after") != std::string::npos) after = r.y;
  }
  CHECK_EQ(first(ysOf(named(d, "Run"))), after);
  CHECK(first(ysOf(named(d, "Run"))) > first(ysOf(named(d, "Para one."))));
}

TEST(docmode_a_caption_is_a_line_of_its_own_in_a_document) {
  // After the figure, so below it.
  const auto d = runs("\\begin{figure}\\centering x\\caption{Cap}\\end{figure}");
  CHECK(first(ysOf(named(d, "Cap"))) > first(ysOf(named(d, "x"))));
}

TEST(docmode_a_groups_paragraphs_and_headings_are_a_documents_its_declarations_kept) {
  const float indent = indentOf();
  const auto size = [](const std::string& tex, const std::string& word) {
    return first(check::column(named(check::of(rec(tex), DrawRecord::TEXT), word), &DrawRecord::font_size));
  };
  // \small lasts to the end of its group, across a paragraph break, and the
  // new paragraph is indented.
  const std::string tex = "Intro.\n\n{\\small first para\n\nsecond para}";
  CHECK_EQ(size(tex, "second para"), size(tex, "first para"));
  CHECK(size(tex, "second para") < size(tex, "Intro."));
  CHECK(close(first(xsOf(named(runs(tex), "second para"))), indent));
  // A heading inside a group is still a heading on a line of its own.
  const auto h = runs("{\\color{red}\\section{A}\nText one.}");
  CHECK(first(ysOf(named(h, "A"))) != first(ysOf(named(h, "Text one."))));
  // \small at the top reaches the text after a heading, and adds no line.
  const std::string small = "\\small\n\\section{A}\nText";
  CHECK_EQ(runs(small)[0].y, runs("\\section{A}\nText")[0].y);
  CHECK(size(small, "Text") < 20);
  // \centering ends with its group.
  const auto c = runs("{\\centering Title\\par}\nBody", InputMode::document, 300);
  CHECK(first(xsOf(named(c, "Title"))) > 50);
  CHECK(close(first(xsOf(named(c, "Body"))), indent));
}

TEST(docmode_a_centred_paragraph_broken_at_max_width_is_centred_line_by_line) {
  std::string words;
  for (char c = 'a'; c < 'a' + 12; c++) words += (c > 'a' ? " " : "") + std::string(3, c);
  const auto starts = lineStarts(runs("\\begin{center}" + words + "\\end{center}", InputMode::document, 80));
  // Several lines, none of them flush left.
  CHECK(starts.size() > 2);
  for (const float x : starts) CHECK(x > 0);
}

TEST(docmode_a_heading_is_numbered_as_latex_numbers_it) {
  CHECK_EQ(textsOf(runs("\\section{A}\n\n\\subsection{B}\n\n\\subsubsection{C}")),
           Strings({"1", "A", "1.1", "B", "1.1.1", "C"}));
  // A counter carries on, and a lower one restarts under it.
  const auto d = textsOf(runs("\\section{A}\n\n\\subsection{B}\n\n\\section{C}\n\n\\subsection{D}"));
  Strings numbers;
  for (const auto& t : d) {
    if (t == "1" || t == "1.1" || t == "2" || t == "2.1") numbers.push_back(t);
  }
  CHECK_EQ(numbers, Strings({"1", "1.1", "2", "2.1"}));
  // A starred heading has no number and does not advance the counter;
  // \paragraph is unnumbered too.
  CHECK_EQ(textsOf(runs("\\section*{A}\n\n\\section{B}")), Strings({"A", "1", "B"}));
  // The space after the run-in heading is the one written after its `}`.
  CHECK_EQ(textsOf(runs("\\paragraph{P} text")), Strings({"P", " text"}));
}

TEST(docmode_a_heading_is_bold_sized_and_on_a_line_of_its_own) {
  const auto r = check::of(rec("\\section{A}\n\n\\subsection{B}\n\n\\subsubsection{C}\n\nbody"), DrawRecord::TEXT);
  const auto size = [&](const std::string& word) { return first(check::column(named(r, word), &DrawRecord::font_size)); };
  const float body = size("body");
  // LaTeX's own sizes: \Large, \large, then the body size.
  CHECK(close(size("A") / body, 1.4f, 1e-5f));
  CHECK(close(size("B") / body, 1.2f, 1e-5f));
  CHECK(close(size("C") / body, 1.0f, 1e-5f));
  // Each heading has a row to itself, never indented.
  CHECK_EQ(check::distinct(ysOf(r), 6), std::size_t(4));
  CHECK_EQ(first(xsOf(named(r, "1"))), 0.0f);
  // \paragraph is the exception: LaTeX runs it into its paragraph.
  const auto p = runs("\\paragraph{P} text");
  CHECK_EQ(p[0].y, p[1].y);
}

TEST(docmode_the_paragraph_after_a_heading_is_not_indented_as_in_latex) {
  const float indent = indentOf();
  // After a line end or a blank line alike.
  for (const char* tex : {"\\section{H}\nfirst\n\nsecond", "\\section{H}\n\nfirst\n\nsecond"}) {
    const auto d = runs(tex);
    CHECK_EQ(first(xsOf(named(d, "first"))), 0.0f);
    CHECK(close(first(xsOf(named(d, "second"))), indent));
  }
  // \noindent says so for any paragraph.
  CHECK(closeAll(xsOf(runs("one\n\n\\noindent two")), {indent, 0}));
}

TEST(docmode_text_under_large_color_or_textcolor_wraps_at_max_width) {
  std::string longText;
  for (int i = 0; i < 40; i++) longText += (i ? " " : "") + std::string("word");
  for (const InputMode mode : {InputMode::document, InputMode::mixed}) {
    for (const std::string& tex : {"\\large " + longText, "\\color{red} " + longText, "\\textcolor{red}{" + longText + "}",
                                   "A {\\small " + longText + "}"}) {
      const auto l = rec(tex, mode, 200);
      CHECK(l.width <= 200);
      CHECK(check::distinct(ysOf(check::of(l, DrawRecord::TEXT)), 0) > 5);
    }
    // Every piece keeps the colour...
    for (const auto& r : check::of(rec("\\color{red} " + longText, mode, 200), DrawRecord::TEXT)) {
      CHECK_EQ(r.col, check::kRed);
    }
    // ...and the size.
    const auto sizes = [&](const std::string& tex, float width) {
      return check::uniqueSorted(check::column(check::ink(rec(tex, mode, width)), &DrawRecord::font_size), 3);
    };
    CHECK(sizes("\\large " + longText, 200) == sizes("\\large word word", 0));
  }
  // A line that fits is set the same at any measure it fits in.
  const std::string fits = "\\large a few {\\color{blue} words} here";
  CHECK(check::sameInk(rec(fits, InputMode::document, 1000), rec(fits, InputMode::document, 5000)));
}

TEST(docmode_a_line_breaks_at_the_last_break_before_it_overflows_even_a_level_down) {
  CHECK(rec("\\text{aaa }\\textbf{bbb}\\text{ ccc ddd eee fff ggg hhh}", InputMode::math, 60).width <= 60);
  CHECK(rec("Slope $\\hat{\\beta}_1 = \\sum_{i=1}^{n} x_i^2$", InputMode::mixed, 150).width <= 150);
  CHECK(rec("Hello \\textbf{big bold} world, and more words", InputMode::mixed, 100).width <= 100);
}

TEST(docmode_ulems_uline_and_sout_wrap_with_their_text_their_rules_straight) {
  const std::string longText = "words that run on long enough to need several lines in a narrow box";
  for (const char* cmd : {"uline", "sout"}) {
    const auto l = rec(std::string("A \\") + cmd + "{" + longText + "} end", InputMode::mixed, 150);
    CHECK(l.width <= 150);
    const auto rules = check::of(l, DrawRecord::LINE);
    const auto text = check::of(l, DrawRecord::TEXT);
    // One rule height per line.
    std::set<long> lines;
    for (const auto& t : text) lines.insert(std::lround(t.y));
    for (const long y : lines) {
      float lineY = 0;
      for (const auto& t : text) {
        if (std::lround(t.y) == y) {
          lineY = t.y;
          break;
        }
      }
      std::vector<float> at;
      for (const auto& r : rules) {
        if (std::fabs(r.y - lineY) < 12) at.push_back(r.y);
      }
      if (at.empty()) continue;
      CHECK_EQ(check::distinct(at, 2), std::size_t(1));
    }
    CHECK(lines.size() > 2);
  }
  // \underline is LaTeX's box: it is not broken, whatever the width.
  const auto underlined = rec("\\underline{" + longText + "}", InputMode::mixed, 150);
  CHECK_EQ(check::distinct(ysOf(check::of(underlined, DrawRecord::TEXT)), 0), std::size_t(1));
  // Nor is a fraction.
  const auto frac = rec("\\frac{\\text{" + longText + "}}{2}", InputMode::math, 150);
  CHECK_EQ(check::distinct(ysOf(check::of(frac, DrawRecord::TEXT)), 0), std::size_t(1));
}

TEST(docmode_relsizes_textscale_and_relscale_set_a_size_that_wraps) {
  const auto sizes = [](const std::string& tex) {
    return check::uniqueSorted(check::column(check::ink(rec(tex, InputMode::mixed)), &DrawRecord::font_size), 3);
  };
  CHECK(sizes("a \\textscale{1.5}{b}") == std::vector<double>({20, 30}));
  CHECK(sizes("a {\\relscale{0.5} b}") == std::vector<double>({10, 20}));
  std::string longText;
  for (int i = 0; i < 20; i++) longText += (i ? " " : "") + std::string("word");
  CHECK(rec("A \\textscale{1.5}{" + longText + "}", InputMode::mixed, 150).width <= 150);
  CHECK(check::warns(rec("\\relscale{big} x", InputMode::mixed), "not a positive number"));
}

TEST(docmode_raggedleft_flushright_and_flushleft_align_a_documents_lines) {
  const std::string longText = "A paragraph long enough to wrap onto several lines at this width here";
  // Where each line starts. Right-aligned starts twice as far in as centred.
  const auto lefts = [](const std::string& tex) {
    return lineStarts(check::of(rec(tex, InputMode::document, 200), DrawRecord::TEXT));
  };
  const auto centred = lefts("\\centering " + longText);
  CHECK(centred.size() > 1);
  CHECK(check::maximum(centred) > 5);
  std::vector<float> twice;
  for (const float c : centred) twice.push_back(2 * c);
  for (const std::string& tex : {"\\raggedleft " + longText, "\\begin{flushright}" + longText + "\\end{flushright}"}) {
    CHECK(closeAll(lefts(tex), twice, 0.02f * check::maximum(twice)));
  }
  CHECK(closeAll(lefts("\\begin{flushleft}" + longText + "\\end{flushleft}"), std::vector<float>(centred.size(), 0)));
  // A label is placed by hjust: nothing aligns its lines.
  CHECK(check::sameInk(rec("\\raggedleft " + longText, InputMode::mixed), rec(longText, InputMode::mixed)));
}

TEST(docmode_x_columns_share_what_the_table_needs_and_a_column_spec_aligns_its_lines) {
  const std::string tab =
    "\\begin{tabular}{lXX} 1 & short & a long description of the first thing measured in the study \\\\ \\end{tabular}";
  const auto r = check::of(rec(tab, InputMode::mixed, 250), DrawRecord::TEXT);
  // The short column keeps its width; the long one takes the rest.
  const float top = check::minimum(ysOf(r));
  float least = 1e9f, greatest = -1e9f;
  for (const auto& t : r) {
    if (t.y > top) least = std::min(least, t.x);
    greatest = std::max(greatest, t.x);
  }
  // The third column starts at 126 here, 10 more than when the columns were
  // padded by 1em and not by LaTeX's 12pt (1.2em): 2 at the edge and 4 at each
  // of two gaps, at this size of 20. Were
  // the two X columns to share equally it would start at about 154.
  CHECK(least < 140);
  CHECK(greatest <= 250);
  const std::string cell = " a cell of words that wraps \\\\ \\end{tabular}";
  const auto starts = [&](const std::string& spec) {
    return lineStarts(check::of(rec("\\begin{tabular}{" + spec + "}" + cell, InputMode::mixed), DrawRecord::TEXT));
  };
  const auto centred = starts(R"(>{\centering\arraybackslash}p{100pt})");
  const auto right = starts(R"(>{\raggedleft\arraybackslash}p{100pt})");
  const auto plain = starts("p{100pt}");
  CHECK_EQ(centred.size(), plain.size());
  CHECK_EQ(right.size(), plain.size());
  for (std::size_t i = 0; i < plain.size(); i++) {
    CHECK(centred[i] > plain[i]);
    CHECK(right[i] > centred[i]);
  }
}

TEST(docmode_text_mbox_and_textsuperscript_keep_the_style_around_them) {
  const auto r = check::of(rec("\\textbf{a $\\text{b}$ \\mbox{c} H\\textsuperscript{2}}", InputMode::mixed), DrawRecord::TEXT);
  CHECK(r.size() > 3);
  for (const auto& t : r) CHECK((t.font_style & 2) != 0);
  // Nor does a style nested in the same style take it away after it.
  const auto nested = check::of(rec("\\textbf{a \\textbf{b} c}", InputMode::mixed), DrawRecord::TEXT);
  CHECK(nested.size() > 0);
  CHECK((nested.back().font_style & 2) != 0);
  // \underline and \phantom of text are text, in its font.
  for (const auto& t : rec("\\textbf{\\underline{Hg}}", InputMode::mixed).records) {
    CHECK(t.type == DrawRecord::LINE || t.type == DrawRecord::TEXT);
  }
  CHECK_EQ(rec("a\\phantom{Hg}b", InputMode::mixed).width, rec("aHgb", InputMode::mixed).width);
}

TEST(docmode_a_definition_in_math_between_two_dollars_is_inline_math) {
  // With the \newcommand expanded away this is `$` `$`, not `$$`.
  CHECK(rec("$\\newcommand{\\R}{\\mathbb{R}}$ $\\R$", InputMode::document).warnings.empty());
  CHECK(rec("$$x$$ and \\[y\\]", InputMode::document).warnings.empty());
}

TEST(docmode_a_box_stays_whole_in_a_broken_line_and_keeps_a_height_set_by_hand) {
  // \scalebox and \colorbox are boxes: a line breaks around them.
  std::string longText;
  for (int i = 0; i < 20; i++) longText += (i ? " " : "") + std::string("abc");
  const auto lines = [](const std::string& tex) { return rows(runs(tex, InputMode::document, 200)); };
  CHECK_EQ(lines("Lead \\scalebox{0.9}{" + longText + "} tail"), std::size_t(3));
  CHECK_EQ(lines("Lead \\colorbox{yellow}{" + longText + "} tail"), std::size_t(3));
  CHECK(lines("Lead \\large " + longText + " tail") > 3);
  // A rebuilt row keeps the height \smash or \raisebox gave it.
  std::string words;
  for (int i = 0; i < 30; i++) words += (i ? " " : "") + std::string("word");
  const auto height = [&](const std::string& tall) {
    check::Options opt(InputMode::document, 200);
    return check::render("Lead " + words + " " + tall + " " + words, opt).height;
  };
  // Against a box as wide and no taller than a line, so that both break alike.
  const auto flat = [&](const std::string& tall) {
    const int width = check::render(tall, Options(InputMode::mixed)).width;
    return height("\\rule{" + std::to_string(width) + "px}{0pt}");  // the width is a count of pixels
  };
  for (const char* tall : {"\\smash{\\Huge Tall}", "\\smash{\\textcolor{red}{\\Huge Tall}}",
                           "\\raisebox{0pt}[0pt][0pt]{\\Huge Tall}", "\\smash{\\rule{1pt}{40pt}\\textcolor{red}{a b}}"}) {
    CHECK_EQ(std::string(tall) + " -> " + std::to_string(height(tall)),
             std::string(tall) + " -> " + std::to_string(flat(tall)));
  }
  CHECK(height("{\\Huge Tall}") > flat("{\\Huge Tall}"));
}

// --- what a pasted paper uses ---

TEST(docmode_em_is_italic_as_emph_is) {
  const auto em = check::of(rec("a {\\em b} c"), DrawRecord::TEXT);
  const auto emph = check::of(rec("a \\emph{b} c"), DrawRecord::TEXT);
  CHECK_EQ(show(em), show(emph));
  CHECK_EQ(em.size(), emph.size());
  for (const auto& t : em) {
    if (t.text == "b") CHECK((t.font_style & 4) != 0);
  }
}

TEST(docmode_natbibs_citations_draw_what_natbib_draws_for_an_unknown_key) {
  const auto drawn = [](const std::string& tex) {
    std::string out;
    for (const auto& t : check::texts(rec(tex))) out += t;
    return out;
  };
  CHECK_EQ(drawn("\\citep{a}"), drawn("\\cite{a}"));
  CHECK_EQ(drawn("\\citep[p.~5]{a}"), drawn("\\cite[p.~5]{a}"));
  CHECK_EQ(drawn("\\citep[see][ch.~2]{a,b}"), std::string("[see ?, ?, ch. 2]"));
  CHECK_EQ(drawn("\\citet{a}"), std::string("(author?) [?]"));
  CHECK_EQ(drawn("\\citealp{a}"), std::string("?"));
  CHECK(check::warns(rec("\\citet{k1}"), "citation `k1' is undefined: drawn as (author?) [?]"));
}

// pdflatex, two runs: \cite{a} is [1] whether the bibliography comes before or
// after it; \cite{a,b} is [1, 2] (\@citex puts ", " between the keys).
TEST(docmode_cite_draws_the_number_of_its_bibitem_before_or_after_it) {
  const auto drawn = [](const std::string& tex) {
    std::string out;
    for (const auto& t : check::texts(rec(tex))) out += t;
    return out;
  };
  const std::string bib = "\n\n\\begin{thebibliography}{9}\\bibitem{a} A.\\bibitem{b} B.\\end{thebibliography}";
  CHECK(drawn("See \\cite{a}." + bib).find("See [1].") != std::string::npos);
  CHECK(drawn(bib + "\n\nSee \\cite{b}.").find("See [2].") != std::string::npos);
  CHECK(drawn("\\cite{a,b}" + bib).find("[1, 2]") != std::string::npos);
  CHECK(drawn("\\citep[see][ch.~2]{a}" + bib).find("[see 1, ch. 2]") != std::string::npos);
  CHECK_EQ(drawn("\\citealp{b}" + bib).rfind("2", 0), std::size_t(0));
  CHECK(!check::warns(rec("\\cite{a,b}" + bib), "citation"));
}

// \bibitem[Knuth 1984]{k}: marked [Knuth 1984], and \cite{k} draws the same.
// The mark is the list's label column, drawn as characters: "[Knuth 1984]" is
// eleven of them where the unlabelled "[1]" is three.
TEST(docmode_a_labelled_bibitem_is_marked_and_cited_by_its_label) {
  const std::string bib = "\\begin{thebibliography}{9}\\bibitem[Knuth 1984]{k} K.\\end{thebibliography}";
  const auto marks = rec(bib);
  CHECK_EQ(check::count(marks, DrawRecord::GLYPH) + check::count(marks, DrawRecord::PATH), 11);
  std::string s;
  for (const auto& t : check::texts(rec("\\cite{k}\n\n" + bib))) s += t;
  CHECK(s.find("[Knuth 1984]") != std::string::npos);
}

TEST(docmode_a_key_no_bibitem_names_warns_once_per_key) {
  const auto l = rec("\\cite{a,nokey}\n\n\\begin{thebibliography}{9}\\bibitem{a} A.\\end{thebibliography}");
  CHECK(check::warns(l, "citation `nokey' is undefined: drawn as [?]"));
  CHECK(!check::warns(l, "citation `a'"));
}

// A citation in a table cell or a list item is lowered by a Lowerer of its own,
// and is found as well.
TEST(docmode_a_cite_in_a_table_cell_or_a_list_item_resolves) {
  const std::string bib = "\n\n\\begin{thebibliography}{9}\\bibitem{a} A.\\end{thebibliography}";
  const auto drawn = [](const std::string& tex) {
    std::string out;
    for (const auto& t : check::texts(rec(tex))) out += t;
    return out;
  };
  CHECK(drawn("\\begin{tabular}{l}x \\cite{a}\\end{tabular}" + bib).find("x [1]") != std::string::npos);
  CHECK(drawn("\\begin{itemize}\\item y \\cite{a}\\end{itemize}" + bib).find("y [1]") != std::string::npos);
  CHECK(check::warns(rec("\\begin{tabular}{l}\\cite{nokey}\\end{tabular}"), "citation `nokey' is undefined"));
}

// \parbox is a minipage written inline: the same ink, with and without the
// optional position, height and inner position.
TEST(docmode_parbox_draws_what_a_minipage_of_that_width_draws) {
  CHECK(check::sameInk(rec("\\parbox{3cm}{a b c d e f}"),
                       rec("\\begin{minipage}{3cm}a b c d e f\\end{minipage}")));
  CHECK(check::sameInk(rec("\\parbox[t]{3cm}{a b c d e f}"),
                       rec("\\begin{minipage}[t]{3cm}a b c d e f\\end{minipage}")));
  CHECK(check::sameInk(rec("\\parbox[b][2cm][t]{3cm}{a b c d e f}"),
                       rec("\\begin{minipage}[b][2cm][t]{3cm}a b c d e f\\end{minipage}")));
  CHECK(rec("\\parbox{3cm}{a}").warnings.empty());
}

// Inline: in a table cell and in a frame, and beside text on a line.
TEST(docmode_parbox_stands_in_a_cell_a_frame_and_a_line) {
  CHECK(rec("\\begin{tabular}{l}\\parbox{3cm}{a b c}\\end{tabular}").warnings.empty());
  CHECK(rec("\\fbox{\\parbox{3cm}{a b c}}").warnings.empty());
  const auto l = rec("x \\parbox{3cm}{a} y");
  CHECK(l.warnings.empty());
  CHECK_EQ(textsOf(runs("x \\parbox{3cm}{a} y")),
           textsOf(runs("x \\begin{minipage}{3cm}a\\end{minipage} y")));
}

// The text a document draws, its runs one after the other.
static std::string drawn(const std::string& tex) {
  std::string out;
  for (const auto& t : check::texts(rec(tex))) out += t;
  return out;
}

// pdflatex: \newcounter{c}\setcounter{c}{3}\stepcounter{c} then
// \arabic{c} \roman{c} \Roman{c} \alph{c} \Alph{c} gives "4 iv IV d D";
// \fnsymbol{2} is a dagger.
TEST(docmode_counters_are_set_stepped_and_shown_in_every_style) {
  const std::string c = "\\newcounter{c}\\setcounter{c}{3}\\stepcounter{c}";
  CHECK_EQ(drawn(c + "\\arabic{c} \\roman{c} \\Roman{c} \\alph{c} \\Alph{c}"), std::string("4 iv IV d D"));
  CHECK_EQ(drawn(c + "\\thec"), std::string("4"));
  CHECK_EQ(drawn("\\newcounter{c}\\addtocounter{c}{5}\\addtocounter{c}{-2}\\arabic{c}"), std::string("3"));
  CHECK_EQ(drawn("\\newcounter{c}\\setcounter{c}{2}\\fnsymbol{c}"), std::string("†"));
  CHECK(rec(c + "\\arabic{c}").warnings.empty());
}

TEST(docmode_value_reads_another_counter_and_a_renewed_the_changes_how_a_counter_shows) {
  CHECK_EQ(drawn("\\newcounter{a}\\newcounter{b}\\setcounter{a}{7}\\setcounter{b}{\\value{a}}\\arabic{b}"),
           std::string("7"));
  CHECK_EQ(drawn("\\newcounter{c}\\renewcommand{\\thec}{\\Alph{c}}\\stepcounter{c}\\thec"), std::string("A"));
}

// \newcounter{s}[c]: stepping c sets s to zero, as in LaTeX.
TEST(docmode_a_counter_within_another_starts_again_when_that_steps) {
  CHECK_EQ(drawn("\\newcounter{c}\\newcounter{s}[c]\\stepcounter{c}\\stepcounter{s}\\stepcounter{s}"
                 "\\arabic{c}.\\arabic{s} \\stepcounter{c}\\arabic{c}.\\arabic{s}"),
           std::string("1.2 2.0"));
}

// \refstepcounter{c} steps it and the next \label names it: \ref draws the
// number the counter had then, not the one it ends with.
TEST(docmode_refstepcounter_makes_the_label_name_the_counter_at_that_point) {
  CHECK_EQ(drawn("\\newcounter{c}\\setcounter{c}{4}\\refstepcounter{c}\\label{x}"
                 "\\stepcounter{c}\\stepcounter{c}See \\ref{x}, now \\arabic{c}"),
           std::string("See 5, now 7"));
  CHECK_EQ(drawn("see \\ref{x}.\\newcounter{c}\\refstepcounter{c}\\label{x}"), std::string("see 1."));
}

// The distance from the start of the last-but-one text run to the start of the
// last, at a text size of 10 (1em = 10): "a", a space, "b".
static float gapBetween(const std::string& tex) {
  const auto xs = xsOf(runs(tex, InputMode::document, 0, 10));
  return xs.at(xs.size() - 1) - xs.at(xs.size() - 2);
}

// The lengths LaTeX's article class sets at 10pt, in em so that they follow the
// text size: \parindent 15pt, \tabcolsep 6pt, \arraycolsep 5pt, \fboxsep 3pt,
// \baselineskip 12pt, \columnsep 10pt. A length is read as \linewidth is: in
// the dimension of a command.
TEST(docmode_a_length_stands_where_a_dimension_does) {
  const float plain = gapBetween("a\\hspace{0em}b");
  CHECK(close(gapBetween("a\\hspace{\\parindent}b") - plain, 15.f));
  CHECK(close(gapBetween("a\\hspace{2\\tabcolsep}b") - plain, 12.f));
  CHECK(close(gapBetween("a\\hspace{\\arraycolsep}b") - plain, 5.f));
  CHECK(close(gapBetween("a\\hspace{\\baselineskip}b") - plain, 12.f));
  // A sign in front is a factor of -1, as it is for \linewidth.
  CHECK(close(gapBetween("a\\hspace{-\\parindent}b"), gapBetween("a\\hspace{-1.5em}b")));
}

TEST(docmode_setlength_addtolength_and_newlength_change_a_length) {
  const float plain = gapBetween("a\\hspace{0em}b");
  CHECK(close(gapBetween("\\newlength{\\gap}\\setlength{\\gap}{3em}a\\hspace{\\gap}b") - plain, 30.f));
  CHECK(close(gapBetween("\\newlength{\\gap}\\setlength{\\gap}{3em}a\\hspace{2\\gap}b") - plain, 60.f));
  CHECK(close(gapBetween("\\setlength{\\parindent}{1em}\\addtolength{\\parindent}{2em}a\\hspace{\\parindent}b") - plain, 30.f));
  // One length from another.
  CHECK(close(gapBetween("\\newlength{\\g}\\setlength{\\g}{\\tabcolsep}a\\hspace{\\g}b") - plain, 6.f));
  // TeX's glue: only the natural part is used.
  CHECK(close(gapBetween("\\setlength{\\parskip}{1em plus 2em minus 1em}a\\hspace{\\parskip}b") - plain, 10.f));
}

// A group ends what a length was set to in it, as in LaTeX.
TEST(docmode_a_length_set_in_a_group_is_what_it_was_after_the_group) {
  const float plain = gapBetween("a\\hspace{0em}b");
  CHECK(close(gapBetween("{\\setlength{\\parindent}{0em}}a\\hspace{\\parindent}b") - plain, 15.f));
  CHECK(close(gapBetween("\\begin{center}\\setlength{\\parindent}{0em}x\\end{center}a\\hspace{\\parindent}b") - plain, 15.f));
  CHECK(close(gapBetween("\\setlength{\\parindent}{0em}a\\hspace{\\parindent}b") - plain, 0.f));
}

TEST(docmode_a_length_that_is_not_defined_or_not_a_length_warns) {
  CHECK(check::warns(rec("\\setlength{\\nosuch}{1em}"), "length `\\nosuch' is not defined"));
  CHECK(check::warns(rec("\\setlength{\\parindent}{many}"), "`many' is not a length"));
  CHECK(check::warns(rec("\\addtolength{\\parindent}{3pt}"), "cannot add 3pt to 1.5em"));
  CHECK(rec("\\newlength{\\x}\\setlength{\\x}{3pt}").warnings.empty());
}

// \parindent and \parskip: pandoc's template sets both, \parindent 0pt and
// \parskip 6pt plus 2pt minus 1pt. A first line is indented \parindent (15pt;
// 1.5em) and a paragraph break adds \parskip (0 in article).
TEST(docmode_parindent_and_parskip_are_the_lengths_of_a_paragraph) {
  const auto firstX = [](const std::string& tex) {
    return xsOf(runs(tex, InputMode::document, 300, 10)).at(0);
  };
  const auto baselines = [](const std::string& tex) { return ysOf(runs(tex, InputMode::document, 300, 10)); };
  CHECK(close(firstX("a\n\nb"), 15.f));
  CHECK(close(firstX("\\setlength{\\parindent}{0em}a\n\nb"), 0.f));
  CHECK(close(firstX("\\setlength{\\parindent}{2em}a\n\nb"), 20.f));
  const auto plain = baselines("a\n\nb");
  const auto spaced = baselines("\\setlength{\\parskip}{1em}a\n\nb");
  CHECK_EQ(plain.size(), std::size_t(2));
  CHECK_EQ(spaced.size(), std::size_t(2));
  CHECK(close((spaced[1] - spaced[0]) - (plain[1] - plain[0]), 10.f));
  // Glue: only the natural length.
  CHECK(close(firstX("\\setlength{\\parindent}{0pt}\\setlength{\\parskip}{6pt plus 2pt minus 1pt}a\n\nb"), 0.f));
}

// pdflatex: \fbox adds \fboxsep + \fboxrule (3pt + 0.4pt) on every side of its
// text, \colorbox just \fboxsep (3pt), and \fcolorbox both, as \fbox does. At a
// size of 100 the lengths are 0.3em + 0.04em = 34 and 0.3em = 30. The engine
// had 0.5em and its own rule: 11bp where LaTeX has 6.8 at 10pt.
TEST(docmode_fbox_colorbox_and_boxed_pad_by_fboxsep) {
  const auto dims = [](const std::string& tex) { return rec(tex, InputMode::math, 0, 100); };
  const auto a = dims("a");
  // A render's height is the whole box, its depth what is below the baseline.
  const auto grows = [&](const std::string& tex, float side) {
    const auto l = dims(tex);
    return std::fabs((l.width - a.width) - 2 * side) <= 1.5f && std::fabs((l.height - a.height) - 2 * side) <= 1.5f &&
           std::fabs((l.depth - a.depth) - side) <= 1.5f;
  };
  CHECK(grows("\\fbox{a}", 34.f));
  CHECK(grows("\\colorbox{red}{a}", 30.f));
  CHECK(grows("\\fcolorbox{blue}{red}{a}", 34.f));
  CHECK(grows("\\setlength{\\fboxsep}{1em}\\fbox{a}", 104.f));
  CHECK(grows("\\setlength{\\fboxsep}{0em}\\colorbox{red}{a}", 0.f));
  // \colorbox has no frame, only its colour.
  CHECK_EQ(check::count(rec("\\colorbox{red}{a}", InputMode::math), DrawRecord::RECT), 0);
}

// enumitem's label templates (`\roman*`) and the number `\roman{12}` are not
// counters' names: neither says there is no counter.
TEST(docmode_roman_of_a_template_or_a_number_is_not_a_counter) {
  CHECK(!check::warns(rec("[\\roman*)]", InputMode::math), "no counter"));
  CHECK(!check::warns(rec("[\\Roman*.]", InputMode::math), "no counter"));
  CHECK_EQ(drawn("\\roman{12}"), std::string("xii"));
  CHECK(!check::warns(rec("\\begin{enumerate}[label=(\\roman*)]\\item a\\end{enumerate}"), "no counter"));
}

TEST(docmode_a_counter_that_is_not_defined_warns_and_nothing_changes) {
  CHECK(check::warns(rec("\\stepcounter{nosuch}"), "no counter `nosuch'"));
  CHECK(check::warns(rec("\\setcounter{nosuch}{3}"), "no counter `nosuch'"));
  CHECK(check::warns(rec("\\arabic{nosuch}"), "no counter `nosuch'"));
  CHECK(check::warns(rec("\\newcounter{c}\\newcounter{c}"), "counter `c' is already defined"));
  // The counters LaTeX has stay settable.
  CHECK(rec("\\setcounter{equation}{3}\\setcounter{section}{2}").warnings.empty());
}

TEST(docmode_specialrule_is_a_rule_of_the_thickness_it_names) {
  const auto l = rec("\\begin{tabular}{l}a \\\\ \\specialrule{3pt}{0pt}{0pt} b\\end{tabular}");
  CHECK_EQ(check::texts(l), Strings({"a", "b"}));  // its arguments are read
  const auto lines = check::of(l, DrawRecord::LINE);
  CHECK_EQ(lines.size(), std::size_t(1));
  CHECK(close(lines[0].line_width, 3 * 20 / 10.f, 1e-4f));  // 3pt, at 20px
}

TEST(docmode_boldmath_makes_the_math_of_its_group_bold_and_only_the_math) {
  const auto glyphs = [](const std::string& tex) {
    std::vector<int> ids;
    for (const auto& g : check::of(rec(tex), DrawRecord::GLYPH)) ids.push_back(g.glyph_id);
    return ids;
  };
  CHECK(glyphs("{\\boldmath $x + 2$}") != glyphs("$x + 2$"));
  // It ends with its group.
  const auto after = glyphs("{\\boldmath $x$} $x$");
  CHECK_EQ(after.size(), std::size_t(2));
  CHECK_EQ(after[1], glyphs("$x$")[0]);
  const auto styles = [](const std::string& tex) {
    return check::column(check::of(rec(tex), DrawRecord::TEXT), &DrawRecord::font_style);
  };
  CHECK(styles("{\\boldmath words}") == styles("words"));
}

TEST(docmode_an_abstract_has_articles_heading_and_a_bibliography_its_labels) {
  const auto a = runs("\\begin{abstract}Body text.\\end{abstract}");
  CHECK_EQ(textsOf(a), Strings({"Abstract", "Body text."}));
  CHECK(a[0].x > a[1].x);  // centred over the text
  const std::string bib =
    "\\begin{thebibliography}{9}\n\\bibitem{one} A. Author. \\newblock {\\em Title}, 2016.\n"
    "\\bibitem{two} B. Author.\n\\end{thebibliography}";
  const auto b = runs(bib);
  CHECK_EQ(b[0].text, std::string("References"));
  for (const auto& t : b) {
    CHECK(t.text.find("one") == std::string::npos && t.text.find("two") == std::string::npos);  // the keys point at nothing
  }
  const auto labels = [](const std::string& tex) {
    std::vector<int> ids;
    for (const auto& g : check::of(rec(tex), DrawRecord::GLYPH)) ids.push_back(g.glyph_id);
    return ids;
  };
  CHECK(labels(bib) == labels("\\begin{enumerate}[{[}\\arabic*{]}]\\item a \\item b\\end{enumerate}"));
}

TEST(docmode_a_minipage_sets_its_body_to_its_width_placed_as_latex_places_it) {
  std::string longText;
  for (int i = 0; i < 12; i++) longText += (i ? " " : "") + std::string("word");
  for (const char* pos : {"t", "c", "b"}) {
    const auto r = check::of(rec(std::string("A \\begin{minipage}[") + pos + "]{50pt}" + longText + "\\end{minipage} B"),
                             DrawRecord::TEXT);
    const auto words = named(r, "word");
    const auto lines = check::uniqueSorted(ysOf(words), 2);
    CHECK(lines.size() > 3);  // it wraps...
    CHECK(check::maximum(xsOf(words)) - check::minimum(xsOf(words)) <= 100);  // ...inside its width
    // ...with no indent.
    float topX = 1e9f;
    for (const auto& w : words) {
      if (w.y == check::minimum(ysOf(words))) topX = std::min(topX, w.x);
    }
    CHECK(close(topX, check::minimum(xsOf(words)), 0.01f));
    const float a = first(ysOf(named(r, "A ")));
    if (std::string(pos) == "c") {
      CHECK(a > lines.front());
      CHECK(a < lines.back());
    } else {
      CHECK(close(a, static_cast<float>(std::string(pos) == "t" ? lines.front() : lines.back()), 0.01f));
    }
  }
  // \centering centres each line in the minipage's width, not the page's.
  const auto centred = check::of(rec("\\begin{minipage}{200pt}\\centering ab\\end{minipage}"), DrawRecord::TEXT);
  const auto plain = check::of(rec("\\begin{minipage}{200pt}ab\\end{minipage}"), DrawRecord::TEXT);
  CHECK(centred[0].x > plain[0].x + 50);
  // 0.5\textwidth is half of max_width.
  const auto two = runs("\\begin{minipage}{0.5\\textwidth}\\centering L\\end{minipage}%\n"
                        "\\begin{minipage}{0.5\\textwidth}\\centering R\\end{minipage}",
                        InputMode::document, 300);
  CHECK(std::fabs((first(xsOf(named(two, "R"))) - first(xsOf(named(two, "L")))) - 150) < 5);
}

TEST(docmode_a_minipages_body_is_paragraphs_as_the_text_around_it_is) {
  // A blank line starts a paragraph in it, as it does outside one.
  CHECK_EQ(rows(runs("\\begin{minipage}{200pt}First para.\n\nSecond para.\\end{minipage}"), 2), std::size_t(2));
  // Line ends around its body are nothing.
  CHECK_EQ(show(runs("\\begin{minipage}{200pt}\n  Text  \n\\end{minipage}")),
           show(runs("\\begin{minipage}{200pt}Text\\end{minipage}")));
  CHECK_EQ(show(runs("\\begin{minipage}{200pt}\\centering\n  Text  \n\\end{minipage}")),
           show(runs("\\begin{minipage}{200pt}\\centering Text\\end{minipage}")));
  // [b] meets the last line, even when the breaker broke it.
  std::string tenWords;
  for (int i = 0; i < 10; i++) tenWords += (i ? " " : "") + std::string("word");
  const auto r = runs("A \\begin{minipage}[b]{100pt}\\centering Short\\\\" + tenWords + "\\end{minipage} B");
  const auto words = named(r, "word");
  CHECK(check::distinct(ysOf(words), 2) > 2);
  CHECK(close(first(ysOf(named(r, "A "))), check::maximum(ysOf(words))));
  CHECK(close(first(ysOf(named(r, " B"))), check::maximum(ysOf(words))));
}

TEST(docmode_a_heading_that_wraps_hangs_its_title_from_the_number_as_latex_does) {
  const std::string title = "A rather long section title that has to wrap";
  const auto r = runs("\\section{" + title + "}", InputMode::document, 200);
  const auto number = named(r, "1");
  Records words;
  for (const auto& t : r) {
    if (t.text != "1") words.push_back(t);
  }
  CHECK(check::distinct(ysOf(words), 0) > 2);
  // Every line of the title starts where its first line does.
  const auto starts = lineStarts(words);
  CHECK_EQ(check::distinct(starts, 2), std::size_t(1));
  CHECK(check::minimum(starts) > number[0].x);
  // One that fits is set on one line.
  CHECK_EQ(show(runs("\\section{Short}", InputMode::document, 200)), show(runs("\\section{Short}", InputMode::document, 5000)));
}

TEST(docmode_a_minipage_taller_than_its_body_puts_the_room_where_it_is_told) {
  const auto at = [](const char* inner) {
    const auto r = runs(std::string("A \\begin{minipage}[t][80pt][") + inner + "]{60pt}one\\end{minipage} B");
    return std::make_pair(first(ysOf(named(r, "A "))), first(ysOf(named(r, "one"))));
  };
  const auto top = at("t"), mid = at("c"), bottom = at("b");
  // The room goes below (t), around (c) or above (b) the body.
  CHECK(close(top.first, top.second));
  CHECK(top.second < mid.second);
  CHECK(mid.second < bottom.second);
  CHECK(std::fabs((bottom.second - top.second) - 2 * (mid.second - top.second)) < 0.5f);
}

TEST(docmode_textwidth_and_linewidth_are_the_text_width_as_in_latex) {
  const auto w = [](const std::string& tex, float width = 0) { return rec(tex, InputMode::math, width).width; };
  CHECK(std::fabs(w("\\rule{0.5\\textwidth}{1pt}", 200) - 100) <= 1);
  CHECK(std::fabs(w("\\rule{\\linewidth}{1pt}", 200) - 200) <= 1);
  // With no page to measure, article's 345pt.
  CHECK(std::fabs(w("\\rule{\\columnwidth}{1pt}") - 345 * 20 / 10.f) <= 1);  // 345 TeX points, at 20px
}

namespace {

// A document at the size and the line height of R's default (1.2), the measure in px. The stub font is 0.55em a
// character, 0.8em above the baseline and 0.2em below.
Records lineRuns(const std::string& tex, float width, float size = 20) {
  Options opt = typeface(InputMode::document, width, size);
  opt.lineSpace = 0.2f * size;
  Records r = check::of(check::render(tex, opt), DrawRecord::TEXT);
  std::stable_sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
  return r;
}

// The distances between the baselines of the text runs named in order.
std::vector<float> pitches(const Records& r, const std::vector<std::string>& names) {
  std::vector<float> ys;
  for (const auto& n : names) {
    for (const auto& run : r) {
      if (run.text == n) {
        ys.push_back(run.y);
        break;
      }
    }
  }
  std::vector<float> out;
  for (std::size_t i = 1; i < ys.size(); i++) out.push_back(ys[i] - ys[i - 1]);
  return out;
}

}  // namespace

// pdflatex, article at 10pt: the lines of a paragraph, and of two paragraphs (\parskip is 0), are 12pt apart, a
// \baselineskip of 1.2em, whatever the font's own height is (11bp before, 9.2bp between paragraphs).
TEST(docmode_lines_are_a_baselineskip_apart_as_in_tex) {
  const float size = 20;
  const auto r = lineRuns("aaaaaaaaa bbbbbbbbb ccccccccc\n\nddd", 110, size);
  const auto p = pitches(r, {"aaaaaaaaa", "bbbbbbbbb", "ccccccccc", "ddd"});
  CHECK_EQ(p.size(), std::size_t(3));
  for (const float d : p) CHECK(std::fabs(d - 1.2f * size) < 0.5f);
}

// pdflatex, article at 10pt: the mark of a footnote is a raised number after the word, and the note is set after the body (a page has
// its foot, a grob only its end): below the last line, in \footnotesize (0.8), numbered as the counter is; a second note is 1 on. The
// lines of a note are \footnotesize's baselineskip apart, 0.8 of the body's (9.5pt where the body has 12).
TEST(docmode_a_footnote_is_a_mark_in_the_text_and_a_note_after_the_body) {
  const float size = 20;
  const auto r = lineRuns(R"(Aaa\footnote{Nnn} bbb.\footnote{Mmm}

Last)", 400, size);
  // The text, the marks, then the notes, each below the one before.
  const auto find = [&](const std::string& t, std::size_t nth = 0) -> const DrawRecord* {
    for (const auto& run : r) {
      if (run.text == t && nth-- == 0) return &run;
    }
    return nullptr;
  };
  const auto* body = find("Aaa");
  const auto* last = find("Last");
  const auto* n1 = find("Nnn");
  const auto* n2 = find("Mmm");
  CHECK(body != nullptr && last != nullptr && n1 != nullptr && n2 != nullptr);
  if (body == nullptr || last == nullptr || n1 == nullptr || n2 == nullptr) return;
  CHECK(last->y > body->y);
  CHECK(n1->y > last->y + 0.9f * size);  // \skip\footins and the rule lie between
  CHECK(n2->y > n1->y);
  // \footnotesize is 0.8 of the body, and the marks are raised, smaller still.
  CHECK(std::fabs(n1->font_size / body->font_size - 0.8f) < 0.01f);
  const auto* mark1 = find("1", 0);
  const auto* mark2 = find("2", 0);
  CHECK(mark1 != nullptr && mark2 != nullptr);
  if (mark1 != nullptr) {
    CHECK(mark1->y < body->y);  // raised
    CHECK(mark1->x > body->x);  // after the word
  }
  // The same notes drawn as marks: 1 and 2, in the text and in front of each note.
  CHECK(find("1", 1) != nullptr && find("2", 1) != nullptr);
  // A second paragraph's note continues the count; \footnotemark and \footnotetext make one note by two commands.
  const auto two = lineRuns(R"(A\footnotemark b\footnotetext{Tt})", 400, size);
  CHECK(!pitches(two, {"A", "Tt"}).empty());
  const auto marks = [&](const Records& rs) {
    std::string s;
    for (const auto& run : rs) s += run.text + "|";
    return s;
  };
  CHECK(marks(two).find("1|") != std::string::npos);
  // \renewcommand{\thefootnote}: a symbol, as LaTeX's \fnsymbol.
  const auto sym = lineRuns(R"(\renewcommand{\thefootnote}{\fnsymbol{footnote}}A\footnote{Nnn})", 400, size);
  CHECK(marks(sym).find("*|") != std::string::npos);
}

// pdflatex: a display below a full line is 22pt on from it and the line after is 22pt on (\baselineskip and the 10pt
// \abovedisplayskip, \belowdisplayskip); below a short line, with the short skips (0 and 6pt), 12 and 18;
// a display that follows a blank line is not set below a line of its paragraph, so the short skips too -- five of
// them are 18pt apart; an align is `\halign to \displaywidth`, so its skips are the long ones after any line of text.
TEST(docmode_display_skips_are_texs_long_after_a_full_line_and_short_after_a_short_one) {
  const float size = 20, line = 1.2f * size, em = size;
  const auto full = lineRuns("aaaaaaaaaaaaaaaaaa\n\\[ \\text{P} \\]\nbbb", 220, size);
  const auto a = pitches(full, {"aaaaaaaaaaaaaaaaaa", "P", "bbb"});
  CHECK_EQ(a.size(), std::size_t(2));
  if (a.size() < 2) return;
  CHECK(std::fabs(a[0] - (line + em)) < 0.5f);
  CHECK(std::fabs(a[1] - (line + em)) < 0.5f);
  const auto shortLine = lineRuns("ab\n\\[ \\text{Q} \\]\nbbb", 220, size);
  const auto b = pitches(shortLine, {"ab", "Q", "bbb"});
  CHECK_EQ(b.size(), std::size_t(2));
  if (b.size() < 2) return;
  CHECK(std::fabs(b[0] - line) < 0.5f);
  CHECK(std::fabs(b[1] - (line + 0.6f * em)) < 0.5f);
  // A blank line ends the paragraph the display would have interrupted.
  const auto fresh = lineRuns("aaaaaaaaaaaaaaaaaa\n\n\\[ \\text{R} \\]", 220, size);
  CHECK(std::fabs(pitches(fresh, {"aaaaaaaaaaaaaaaaaa", "R"})[0] - line) < 0.5f);
  const auto five = lineRuns("\\[ \\text{S} \\] \\[ \\text{T} \\] \\[ \\text{U} \\]", 220, size);
  const auto c = pitches(five, {"S", "T", "U"});
  CHECK_EQ(c.size(), std::size_t(2));
  if (c.size() < 2) return;
  CHECK(std::fabs(c[0] - (line + 0.6f * em)) < 0.5f);
  CHECK(std::fabs(c[1] - (line + 0.6f * em)) < 0.5f);
  const auto align = lineRuns("ab\n\\begin{align} \\text{V} &= 1 \\end{align}\nbbb", 220, size);
  const auto d = pitches(align, {"ab", "V", "bbb"});
  CHECK_EQ(d.size(), std::size_t(2));
  if (d.size() < 2) return;
  if (std::fabs(d[1] - (line + em)) >= 1.f) check::fail(__FILE__, __LINE__, "DEBUG d = " + std::to_string(d[0]) + ", " + std::to_string(d[1]) + " want " + std::to_string(line + em));
  CHECK(std::fabs(d[0] - (line + em)) < 1.f);
  CHECK(std::fabs(d[1] - (line + em)) < 1.f);
}

// soul: \hl is the engine's background colour (yellow unless \sethlcolor says), \ul and \st its underline and strike-out.
TEST(docmode_soul_highlights_underlines_and_strikes) {
  const std::string soul = "\\usepackage{soul}";
  const auto hl = rec(soul + "\\hl{a} b");
  CHECK(check::drawsColour(hl, 0xFFFFFF00u));
  CHECK(check::drawsColour(rec(soul + "\\sethlcolor{red}\\hl{a} b"), 0xFFFF0000u));
  CHECK(check::sameInk(rec(soul + "\\ul{ab}"), rec(soul + "\\uline{ab}")));
  CHECK(check::sameInk(rec(soul + "\\hl{ab}"), rec(soul + "\\bgcolor{yellow}{ab}")));
  // \st is the engine's own, with or without the package.
  CHECK(!check::warns(rec("\\st{ab}"), "st"));
  // Without the package the name is not known.
  CHECK(check::warns(rec("\\hl{a}"), "hl"));
}

// framed: a frame of 0.4pt with 9pt of padding (\FrameRule, \FrameSep = 3\fboxsep) as wide as the line, shaded the same in
// shadecolor with no frame; both are the engine's tcolorbox.
TEST(docmode_framed_and_shaded_are_boxes_as_wide_as_the_line) {
  const std::string framed = "\\usepackage{framed}";
  CHECK(check::sameInk(rec(framed + "\\begin{framed}Hello\\end{framed}", InputMode::document, 400),
                       rec(framed + "\\begin{tcolorbox}[colback=white, colframe=black, boxrule=0.4pt, sharp corners, boxsep=0pt, "
                           "left=9pt, right=9pt, top=9pt, bottom=9pt]Hello\\end{tcolorbox}",
                           InputMode::document, 400)));
  const auto f = rec(framed + "\\begin{framed}Hello\\end{framed}", InputMode::document, 400);
  const auto rects = check::of(f, DrawRecord::FILL_RECT);
  CHECK(rects.size() >= 2);
  if (rects.size() >= 2) {
    CHECK(close(rects[0].width, 400.f, 0.01f));
    CHECK(close(rects[0].width - rects[1].width, 2 * 0.04f * 20, 0.01f));   // the frame, 0.4pt = 0.04em each side
  }
  const auto s = rec(framed + "\\definecolor{shadecolor}{rgb}{0.9,0.9,0.9}\\begin{shaded}Hello\\end{shaded}", InputMode::document, 400);
  CHECK(check::drawsColour(s, 0xFFE6E6E6u) || check::drawsColour(s, 0xFFE5E5E5u));   // 0.9 of 255, rounded either way
  CHECK(!check::warns(s, "undefined"));
}

// mdframed: a 0.4pt black line, white inside, 10pt at the sides and 0.4 baselineskip above and below the text, square
// corners (mdframed.sty); its own keys are tcolorbox's that draw the same thing, and others are said.
TEST(docmode_mdframed_is_a_box_with_its_own_keys) {
  const std::string md = "\\usepackage{mdframed}";
  const auto base = rec(md + "\\begin{mdframed}Hello\\end{mdframed}", InputMode::document, 400);
  CHECK(!check::of(base, DrawRecord::FILL_RECT).empty());
  CHECK(check::of(base, DrawRecord::FILL_ROUND_RECT).empty());
  const float x0 = first(xsOf(runs(md + "\\begin{mdframed}Hello\\end{mdframed}", InputMode::document, 400)));
  CHECK(close(x0, (0.04f + 1.0f) * 20, 0.05f));                       // the line and 10pt
  const float x1 = first(xsOf(runs(md + "\\begin{mdframed}[innerleftmargin=30pt]Hello\\end{mdframed}", InputMode::document, 400)));
  CHECK(close(x1 - x0, 2.f * 20, 0.05f));
  const auto colours = rec(md + "\\begin{mdframed}[linecolor=red, backgroundcolor=yellow]x\\end{mdframed}", InputMode::document, 400);
  CHECK(check::drawsColour(colours, 0xFFFF0000u));
  CHECK(check::drawsColour(colours, 0xFFFFFF00u));
  CHECK(!check::of(rec(md + "\\begin{mdframed}[roundcorner=5pt]x\\end{mdframed}"), DrawRecord::FILL_ROUND_RECT).empty());
  CHECK(check::warns(rec(md + "\\begin{mdframed}[skipabove=3pt]x\\end{mdframed}"), "mdframed: the key `skipabove' is not drawn"));
  const auto titled = rec(md + "\\begin{mdframed}[frametitle=Head]Body\\end{mdframed}", InputMode::document, 400);
  CHECK_EQ(textsOf(runs(md + "\\begin{mdframed}[frametitle=Head]Body\\end{mdframed}")), Strings({"Head", "Body"}));
  (void)titled;
}



// wrapfig: wrapfigure and wraptable are figure and table; the lines, placement and width are read and left out.
TEST(docmode_wrapfigure_is_a_figure_and_wraptable_a_table) {
  const std::string w = "\\usepackage{wrapfig}";
  CHECK(check::sameInk(rec(w + "\\begin{wrapfigure}{r}{5cm}X\\caption{C}\\end{wrapfigure}"),
                       rec(w + "\\begin{figure}X\\caption{C}\\end{figure}")));
  CHECK(check::sameInk(rec(w + "\\begin{wrapfigure}[10]{l}{0.4\\textwidth}X\\caption{C}\\end{wrapfigure}"),
                       rec(w + "\\begin{figure}X\\caption{C}\\end{figure}")));
  CHECK(check::sameInk(rec(w + "\\begin{wraptable}{r}{5cm}X\\caption{C}\\end{wraptable}"),
                       rec(w + "\\begin{table}X\\caption{C}\\end{table}")));
}

static std::string noSpaces(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

// The y of the first run holding `word`.
static float yOfWord(const std::string& tex, const std::string& word, float width = 0) {
  for (const auto& r : runs(tex, InputMode::document, width)) {
    if (r.text.find(word) != std::string::npos) return r.y;
  }
  return -1e9f;
}



// Found probing item 12: the lines of a listing were set solid, one glyph height apart (6bp at 10pt where pdflatex has 12),
// and a blank line took no room. Every line is a baselineskip (1.2em) from the next, a blank one included.
TEST(docmode_the_lines_of_a_verbatim_block_are_a_baselineskip_apart_blank_lines_too) {
  // The pitch is the document's own: two paragraphs' lines.
  const float line = yOfWord("A\n\nB", "B") - yOfWord("A\n\nB", "A");
  CHECK(line > 0);
  for (const char* env : {"verbatim", "lstlisting", "Verbatim"}) {
    const std::string b = std::string("\\begin{") + env + "}\nx\nHxg\n\ny\n\\end{" + env + "}";
    const float x = yOfWord(b, "x"), h = yOfWord(b, "Hxg"), y = yOfWord(b, "y");
    CHECK(close(h - x, line, 0.5f));
    CHECK(close(y - h, 2 * line, 0.5f));   // the blank line between them
  }
}

// listings and fancyvrb take a tab to the next multiple of 8 columns, plain verbatim as one space.
TEST(docmode_a_tab_in_a_listing_is_spaces_to_the_next_stop_of_eight) {
  CHECK_PAIRS_WITH((Pairs{
    {"\\begin{lstlisting}\n\tx\n\\end{lstlisting}", "\\begin{lstlisting}\n        x\n\\end{lstlisting}"},
    {"\\begin{lstlisting}\nab\tx\n\\end{lstlisting}", "\\begin{lstlisting}\nab      x\n\\end{lstlisting}"},
    {"\\begin{Verbatim}\n\tx\n\\end{Verbatim}", "\\begin{Verbatim}\n        x\n\\end{Verbatim}"},
    {"\\begin{verbatim}\n\tx\n\\end{verbatim}", "\\begin{verbatim}\n x\n\\end{verbatim}"}}),
                   typeface(InputMode::document, 300));
}

// fancyvrb's tabsize (and listings'): a tab goes to the next multiple of it; the default stays 8, and plain verbatim's
// tab stays one space.
TEST(docmode_tabsize_sets_the_stops_of_a_listing_and_of_verbatim_from_fancyvrb) {
  CHECK_PAIRS_WITH((Pairs{
    {"\\begin{Verbatim}[tabsize=4]\n\tx\n\\end{Verbatim}", "\\begin{Verbatim}\n    x\n\\end{Verbatim}"},
    {"\\begin{Verbatim}[tabsize=4]\nab\tx\n\\end{Verbatim}", "\\begin{Verbatim}\nab  x\n\\end{Verbatim}"},
    {"\\begin{Verbatim}[fontsize=\\small, tabsize=2]\nab\tx\n\\end{Verbatim}", "\\begin{Verbatim}\nab  x\n\\end{Verbatim}"},
    {"\\begin{lstlisting}[language=R,tabsize=3]\n\tx\n\\end{lstlisting}", "\\begin{lstlisting}\n   x\n\\end{lstlisting}"},
    {"\\begin{Verbatim}[tabsize=0]\n\tx\n\\end{Verbatim}", "\\begin{Verbatim}\n        x\n\\end{Verbatim}"},
    {"\\begin{minted}[tabsize=4]{r}\n\tx\n\\end{minted}", "\\begin{minted}{r}\n    x\n\\end{minted}"}}),
                   typeface(InputMode::document, 300));
}

// alltt, and fancyvrb's commandchars: the code is set as typed -- its spaces, its `#`, `%` and `_` -- but a command in it
// acts, with the macros the input has defined. This is what knitr writes (alltt with \hl... commands) and what pandoc
// writes (Highlighting is a Verbatim with commandchars=\\\{\}), and what a markdown code block is drawn as.
TEST(docmode_alltt_sets_its_code_as_typed_and_its_commands_act) {
  const std::string tex = "\\begin{alltt}\nx <- 1 # 50%   a_b\n  \\textcolor{red}{f}(\\textbf{y}) \\{ \\}\n\\end{alltt}";
  const auto l = rec(tex, InputMode::document, 300);
  CHECK_EQ(l.warnings, Strings{});
  const Records r = runs(tex, InputMode::document, 300);
  std::string text;
  for (const auto& x : r) text += x.text;
  CHECK_EQ(text, std::string("x <- 1 # 50%   a_b  f(y) { }"));
  for (const auto& x : r) CHECK((x.font_style & 128) != 0);   // the typewriter face
  const Records f = named(r, "f"), y = named(r, "y");
  CHECK_EQ(f.size(), std::size_t(1));
  CHECK_EQ(y.size(), std::size_t(1));
  if (f.size() == 1 && y.size() == 1) {
    CHECK_EQ(f[0].col, 0xFFFF0000u);
    CHECK((y[0].font_style & 2) != 0);   // bold
    // The second line is a baselineskip below the first, and its two spaces (the stub's 0.55 em each) come before the f.
    CHECK(f[0].y > r.front().y);
    CHECK(close(f[0].x, 22.f, 0.01f));
  }
}

TEST(docmode_alltt_is_verbatim_with_the_commands_of_its_code_acting) {
  CHECK_PAIRS_WITH((Pairs{
    {"\\begin{alltt}\nab\n\n  cd\n\\end{alltt}", "\\begin{verbatim}\nab\n\n  cd\n\\end{verbatim}"},
    {"\\begin{alltt}\n\tx\n\\end{alltt}", "\\begin{Verbatim}\n        x\n\\end{Verbatim}"},
    {"\\begin{alltt}\n\\textcolor{red}{a} b\n\\end{alltt}",
     "\\begin{Verbatim}[commandchars=\\\\\\{\\}]\n\\textcolor{red}{a} b\n\\end{Verbatim}"},
    // Another trio of characters: | starts a command, < opens and > closes a group; the backslash and braces are text.
    {"\\begin{alltt}\n\\textcolor{red}{a} b\n\\end{alltt}",
     "\\begin{Verbatim}[commandchars=|<>]\n|textcolor<red><a> b\n\\end{Verbatim}"}}),
                   typeface(InputMode::document, 300));
  const Records r = runs("\\begin{Verbatim}[commandchars=|<>]\n\\x{|textbf<b>} c\n\\end{Verbatim}", InputMode::document, 300);
  CHECK_EQ(joinText(r), std::string("\\x{b} c"));
  CHECK_EQ(named(r, "b").size(), std::size_t(1));
  if (named(r, "b").size() == 1) CHECK((named(r, "b")[0].font_style & 2) != 0);
}

// The input's own macros act in the code, as they do in LaTeX: pandoc's \KeywordTok and knitr's \hlkwd are the document's
// \newcommands.
TEST(docmode_a_macro_the_input_defined_acts_in_alltt) {
  const std::string tex =
    "\\newcommand{\\KeywordTok}[1]{\\textcolor[rgb]{0,0,1}{\\textbf{#1}}}\n"
    "\\begin{alltt}\n\\KeywordTok{if} (x) \\KeywordTok{return}\n\\end{alltt}";
  const auto l = rec(tex, InputMode::document, 300);
  CHECK(l.warnings.empty());
  const Records r = runs(tex, InputMode::document, 300);
  CHECK_EQ(joinText(r), std::string("if (x) return"));
  CHECK_EQ(named(r, "if").size(), std::size_t(1));
  if (named(r, "if").size() != 1) return;
  CHECK(named(r, "if")[0].col == 0xFF0000FFu);
  CHECK((named(r, "if")[0].font_style & 2) != 0);
  // The same through an environment the input made of it, as pandoc's \newenvironment{Shaded} is.
  CHECK(rec("\\newenvironment{Shaded}{}{}\\begin{Shaded}\\begin{alltt}\nx\n\\end{alltt}\\end{Shaded}", InputMode::document, 300)
          .warnings.empty());
}

// What pandoc writes for a highlighted chunk, with its preamble: Highlighting is fancyvrb's Verbatim made over with
// commandchars, \NormalTok and the other tokens are \newcommands, Shaded wraps it. (Texts as the pandoc template has them,
// the colours shortened.)
TEST(docmode_a_defined_verbatim_environment_is_a_verbatim_with_the_options_it_was_given) {
  const std::string preamble =
    "\\newenvironment{Shaded}{}{}\n"
    "\\DefineVerbatimEnvironment{Highlighting}{Verbatim}{commandchars=\\\\\\{\\},tabsize=4}\n"
    "\\newcommand{\\KeywordTok}[1]{\\textcolor[rgb]{0,0,1}{\\textbf{#1}}}\n"
    "\\newcommand{\\NormalTok}[1]{#1}\n";
  const std::string tex = preamble +
    "\\begin{Shaded}\n\\begin{Highlighting}[]\n\\KeywordTok{if} \\NormalTok{(x }\\SpecialCharTok{\\textless{}-}\\NormalTok{ 1) \\{}\n"
    "\\NormalTok{\t  y}\n\\NormalTok{\\}}\n\\end{Highlighting}\n\\end{Shaded}";
  const auto l = rec(tex, InputMode::document, 300);
  // \SpecialCharTok is not defined here: the only warning is for it.
  CHECK_EQ(l.warnings.size(), std::size_t(1));
  CHECK(check::warns(l, "SpecialCharTok"));
  const Records r = runs(tex, InputMode::document, 300);
  CHECK_EQ(named(r, "if").size(), std::size_t(1));
  if (named(r, "if").size() == 1) {
    CHECK_EQ(named(r, "if")[0].col, 0xFF0000FFu);
    CHECK((named(r, "if")[0].font_style & 2) != 0);
  }
  for (const auto& x : r) CHECK((x.font_style & 128) != 0);
  // Without the unknown command the code is quiet, its tab goes to the stop of 4 given by the definition, and it is set
  // as the Verbatim it is made of.
  const std::string clean = preamble +
    "\\begin{Highlighting}[]\n\\KeywordTok{if} \\NormalTok{x}\n\t  y\n\\end{Highlighting}";
  CHECK(rec(clean, InputMode::document, 300).warnings.empty());
  CHECK_PAIRS_WITH((Pairs{
    {clean, "\\begin{Verbatim}[commandchars=\\\\\\{\\}]\n\\textcolor[rgb]{0,0,1}{\\textbf{if}} x\n      y\n\\end{Verbatim}"}}),
                   typeface(InputMode::document, 300));
  // \RecustomVerbatimEnvironment changes an environment that is already there.
  CHECK_PAIRS_WITH((Pairs{
    {"\\RecustomVerbatimEnvironment{Verbatim}{Verbatim}{tabsize=2}\\begin{Verbatim}\n\tx\n\\end{Verbatim}",
     "\\begin{Verbatim}\n  x\n\\end{Verbatim}"}}),
                   typeface(InputMode::document, 300));
  CHECK(check::warns(rec("\\DefineVerbatimEnvironment{Foo}{tabular}{}x", InputMode::document, 300), "can be made over"));
}

// A group left open on a line is closed at its end, with one warning; a closing brace with nothing open is a character;
// `Verbatim*` shows its spaces; three characters are needed; and nothing here reads past its own \end.
TEST(docmode_the_code_of_alltt_recovers_from_unbalanced_groups) {
  const auto open = rec("\\begin{alltt}\n\\textbf{a\nb\n\\end{alltt}", InputMode::document, 300);
  CHECK(check::warns(open, "left open"));
  CHECK_EQ(open.warnings.size(), std::size_t(1));
  CHECK_EQ(joinText(runs("\\begin{alltt}\n\\textbf{a\nb\n\\end{alltt}", InputMode::document, 300)), std::string("ab"));
  CHECK_EQ(joinText(runs("\\begin{alltt}\na } b\n\\end{alltt}", InputMode::document, 300)), std::string("a } b"));
  CHECK_EQ(joinText(runs("\\begin{Verbatim*}[commandchars=\\\\\\{\\}]\na \\textbf{b c}\n\\end{Verbatim*}", InputMode::document, 300)),
           std::string("a\xe2\x90\xa3" "b\xe2\x90\xa3" "c"));
  CHECK(check::warns(rec("\\begin{Verbatim}[commandchars=ab]\nx\n\\end{Verbatim}", InputMode::document, 300), "three characters"));
  // The body ends at its own \end, and what follows it is read as usual.
  CHECK_EQ(joinText(runs("\\begin{alltt}\nx\n\\end{alltt}\nafter", InputMode::document, 300)), std::string("xafter"));
  CHECK(rec("\\begin{alltt}\\end{alltt}", InputMode::document, 300).warnings.empty());
  CHECK(check::warns(rec("\\begin{alltt}\nx", InputMode::document, 300), "missing \\end{alltt}"));
}

// Found probing the above: the text the parser puts back for the code of an alltt was recorded with the argument it stood
// in (a footnote's, which is read again from its text), so the note was the code followed by `\gmcodeline{..}\end{alltt}`
// drawn red, one glyph a letter. And a tabsize past the range of an int was undefined.
TEST(docmode_alltt_in_an_argument_that_is_read_again_is_the_code_as_typed) {
  const std::string tex = "text\\footnote{\\begin{alltt}x \\textbf{y}\\end{alltt}} more";
  const auto l = rec(tex, InputMode::document, 300);
  CHECK_EQ(l.warnings, Strings{});
  const Records r = runs(tex, InputMode::document, 300);
  CHECK_EQ(joinText(r), std::string("1textmore1x y"));   // the raised mark first; the note at the foot, with its mark
  CHECK_EQ(check::of(l, DrawRecord::TEXT).size(), r.size());
  CHECK(rec("\\fbox{\\begin{alltt}x \\textbf{y}\\end{alltt}}", InputMode::document, 300).warnings.empty());
  CHECK(rec("\\newcommand{\\code}[1]{\\begin{alltt}#1\\end{alltt}}\\code{x}", InputMode::document, 300).warnings.empty());
  const float wide = rec("\\begin{Verbatim}\n" + std::string(64, ' ') + "x\n\\end{Verbatim}", InputMode::document, 300).width;
  CHECK(close(rec("\\begin{Verbatim}[tabsize=99999999999999999999]\n\tx\n\\end{Verbatim}", InputMode::document, 300).width, wide, 0.01f));
}

// article's quote and quotation are lists with no label whose margins are \leftmargin, 2.5em at the first level, on both
// sides (pdflatex: the text of a quote at 25pt from the margin, 10pt type); a quotation also sets in the first line of
// each paragraph by 1.5em (\listparindent). A verse sets its lines at `\\`. Text here is 20 units a size and a stub
// character 11 wide, so 2.5em is 50 and 1.5em 30.
TEST(docmode_quote_and_quotation_are_set_between_margins_of_two_and_a_half_em) {
  const Records plain = runs("\\noindent aaaaa", InputMode::document, 300);
  CHECK_EQ(plain.size(), std::size_t(1));
  const float margin = first(xsOf(runs("\\begin{quote}aaaaa\\end{quote}", InputMode::document, 300)));
  CHECK(close(margin - first(xsOf(plain)), 50.f, 0.01f));
  CHECK(close(first(xsOf(runs("\\begin{quotation}\\noindent aaaaa\\end{quotation}", InputMode::document, 300))) - first(xsOf(plain)),
              50.f, 0.01f));
  // Narrower by 2.5em a side: 200 units, so three five-letter words (3 * 55 + 2 * 11 = 187) and no fourth.
  const Records wrapped =
    runs("\\begin{quote}aaaaa bbbbb ccccc ddddd eeeee\\end{quote}", InputMode::document, 300);
  CHECK_EQ(textsOf(wrapped), Strings({"aaaaa", "bbbbb", "ccccc", "ddddd", "eeeee"}));
  CHECK(ysOf(wrapped)[3] > ysOf(wrapped)[0]);
  CHECK(close(ysOf(wrapped)[0], ysOf(wrapped)[2], 0.01f));
  CHECK(close(xsOf(wrapped)[3], margin, 0.01f));
  // A quote's paragraphs are flush; a quotation's first lines are in by 1.5em, the first one too.
  const Records quote = runs("\\begin{quote}aaa\n\nbbb\\end{quote}", InputMode::document, 300);
  CHECK(close(xsOf(quote)[0], xsOf(quote)[1], 0.01f));
  const Records quotation = runs("\\begin{quotation}aaa\n\nbbb\\end{quotation}", InputMode::document, 300);
  CHECK(close(xsOf(quotation)[0], margin + 30.f, 0.01f));
  CHECK(close(xsOf(quotation)[1], margin + 30.f, 0.01f));
  // Set apart from the paragraphs around it, with the text after it on a line of its own.
  const Records around = runs("before\n\n\\begin{quote}in\\end{quote}\n\nafter", InputMode::document, 300);
  CHECK_EQ(textsOf(around), Strings({"before", "in", "after"}));
  CHECK(ysOf(around)[1] > ysOf(around)[0] && ysOf(around)[2] > ysOf(around)[1]);
  // pdflatex (article, 10pt): the baselines either side of a quote are 22pt from it, 12 in a paragraph: \topsep 8pt and
  // \partopsep 2pt, which is 1em; it is 1em here too, above and below.
  const float pitch = ysOf(runs("A\n\nB", InputMode::document, 300))[1] - ysOf(runs("A\n\nB", InputMode::document, 300))[0];
  CHECK(close(ysOf(around)[1] - ysOf(around)[0], pitch + 20.f, 0.05f));
  CHECK(close(ysOf(around)[2] - ysOf(around)[1], pitch + 20.f, 0.05f));
  CHECK(rec("\\begin{quote}x\\end{quote}\\begin{quotation}y\\end{quotation}\\begin{verse}z\\end{verse}", InputMode::document, 300)
          .warnings.empty());
}

TEST(docmode_verse_sets_its_lines_at_the_line_breaks_between_the_margins) {
  const Records v = runs("\\begin{verse}aaa \\\\ bbb \\\\ ccc\n\nddd\\end{verse}", InputMode::document, 300);
  CHECK_EQ(textsOf(v), Strings({"aaa", "bbb", "ccc", "ddd"}));
  CHECK(ysOf(v)[1] > ysOf(v)[0] && ysOf(v)[2] > ysOf(v)[1] && ysOf(v)[3] > ysOf(v)[2]);
  CHECK(close(xsOf(v)[0], 50.f, 0.01f));
  // pdflatex (article, 10pt, margin 72bp): the first line of each `\\` line is at 96 (2.5em) and one that wraps goes on at
  // 111 (4em): the left margin is 4em and a paragraph starts 1.5em out of it. Every `\\` line and stanza starts out.
  CHECK(close(xsOf(v)[1], 50.f, 0.01f));
  CHECK(close(xsOf(v)[3], 50.f, 0.01f));
  const Records w = runs("\\begin{verse}aaaaa bbbbb ccccc ddddd eeeee fffff ggggg\\\\ hhhhh\\end{verse}", InputMode::document, 300);
  CHECK_EQ(textsOf(w), Strings({"aaaaa", "bbbbb", "ccccc", "ddddd", "eeeee", "fffff", "ggggg", "hhhhh"}));
  // Three words fit the first line (200 wide: 300 less 6.5em, and the 1.5em it starts out), two the lines after it.
  CHECK(close(xsOf(w)[0], 50.f, 0.01f));
  CHECK(close(xsOf(w)[3], 80.f, 0.01f) && close(ysOf(w)[3], ysOf(w)[4], 0.01f));
  CHECK(ysOf(w)[3] > ysOf(w)[0]);
  CHECK(close(xsOf(w)[5], 80.f, 0.01f) && ysOf(w)[5] > ysOf(w)[3]);
  CHECK(close(xsOf(w)[7], 50.f, 0.01f));   // after the `\\`
}

// The comment package: what is in its environment is not read -- an `\end{document}` or an unbalanced brace there
// is no more than text -- and draws nothing.
TEST(docmode_a_comment_environment_is_skipped_whatever_it_holds) {
  const std::string tex = "a\\begin{comment}\n\\end{document} { \\undefined $\n\\end{comment}b";
  CHECK(rec(tex, InputMode::document, 300).warnings.empty());
  CHECK_EQ(allText(runs(tex, InputMode::document, 300)), std::string("ab"));
  const std::string whole =
    "\\documentclass{article}\\begin{document}x\\begin{comment}\\end{document}\\end{comment}y\\end{document}z";
  CHECK_EQ(allText(runs(whole, InputMode::document, 300)), std::string("xy"));
  CHECK(check::warns(rec("\\begin{comment}\nabc", InputMode::document, 300), "missing \\end{comment}"));
  CHECK_EQ(allText(runs("\\begin{titlepage}\\begin{center}T\\end{center}\\end{titlepage}after", InputMode::document, 300)),
           std::string("Tafter"));
  CHECK(rec("\\begin{titlepage}\\begin{center}T\\end{center}\\end{titlepage}", InputMode::document, 300).warnings.empty());
}

// pdflatex: `\vspace` in the text of a paragraph is a vadjust, so its glue is below the line holding it: X, AAA BBB and CCC
// stand where they do with the \vspace on a line of its own after that paragraph.
TEST(docmode_a_vspace_in_a_paragraph_is_below_its_line) {
  const std::string inside = "X\n\nAAA\\vspace{20pt} BBB\n\nCCC", after = "X\n\nAAA BBB\n\n\\vspace{20pt}\n\nCCC";
  CHECK(close(yOfWord(inside, "AAA") - yOfWord(inside, "X"), yOfWord(after, "AAA") - yOfWord(after, "X"), 0.1f));
  CHECK(close(yOfWord(inside, "CCC") - yOfWord(inside, "AAA"), yOfWord(after, "CCC") - yOfWord(after, "AAA BBB"), 0.1f));
  const float line = yOfWord("A\n\nB", "B") - yOfWord("A\n\nB", "A");
  CHECK(yOfWord(inside, "CCC") - yOfWord(inside, "AAA") > line + 19.f);   // the 20pt of glue is there, below AAA
  CHECK(yOfWord(inside, "AAA") - yOfWord(inside, "X") < line + 1.f);      // and not above it
  // At the start of a paragraph it is above it, as TeX puts it.
  const std::string before = "X\n\n\\vspace{20pt}AAA\n\nCCC";
  CHECK(yOfWord(before, "AAA") - yOfWord(before, "X") > line + 19.f);
}

// Glue is its natural length here (a grob's lines do not stretch), the amounts LaTeX names are its own, and anything else
// that is not a length is said: `1cm plus 5pt` was 1pt, `20sp` was 65536 times too long, `abc` nothing, silently.
TEST(docmode_vspace_reads_its_natural_length_and_says_what_is_not_a_length) {
  const auto gap = [](const std::string& arg) {
    const std::string t = "X\n\n\\vspace{" + arg + "}\n\nCCC";
    return yOfWord(t, "CCC") - yOfWord(t, "X");
  };
  CHECK(close(gap("1cm plus 5pt"), gap("1cm"), 0.01f));
  CHECK(close(gap("1em plus .5em minus .2em"), gap("1em"), 0.01f));
  CHECK(close(gap("\\baselineskip plus 2pt"), gap("\\baselineskip"), 0.01f));
  CHECK(close(gap("\\bigskipamount"), gap("12pt"), 0.01f));
  CHECK(close(gap("\\medskipamount"), gap("6pt"), 0.01f));
  CHECK(close(gap("20sp"), gap("0pt"), 0.01f));
  CHECK(close(gap("65536sp"), gap("1pt"), 0.01f));
  CHECK(check::warns(rec("X\n\n\\vspace{abc}\n\nCCC"), "\\vspace: `abc' is not a length"));
  CHECK(!check::warns(rec("X\n\n\\vspace{1cm plus 5pt}\n\nCCC"), "not a length"));
}

// A label in the caption's own text (`\caption{Text\label{fig}}`, the usual way) names the float: the counter used to step
// after the text was read, so the label had no number (`??`, and nothing for \autoref).
TEST(docmode_a_label_inside_a_caption_names_its_float) {
  const Strings inside = check::lines(rec("\\begin{figure}F\\caption{c\\label{cf}}\\end{figure}See \\ref{cf}, \\autoref{cf}."));
  const Strings after = check::lines(rec("\\begin{figure}F\\caption{c}\\label{cf}\\end{figure}See \\ref{cf}, \\autoref{cf}."));
  CHECK_EQ(inside, after);
  CHECK_EQ(noSpaces(inside.back()), std::string("See1,Figure1."));
  CHECK(!check::warns(rec("\\begin{table}T\\caption{c\\label{ct}}\\end{table}\\cref{ct}"), "undefined"));
  CHECK_EQ(noSpaces(check::lines(rec("\\begin{table}T\\caption{c\\label{ct}}\\end{table}\\cref{ct}")).back()), std::string("table1"));
}

// A \label in an enumerated item names the item's number (\theenumi), as pdflatex's does; it waited for a row number that no
// list ever gave, and \ref found nothing ("??"). A label in a table cell is the float's or section's, not lost either.

TEST(docmode_a_label_in_a_list_item_or_table_cell_is_kept) {
  const Strings items = check::lines(rec("\\begin{enumerate}\\item\\label{i1} a\\item b\\label{i2}\\end{enumerate}See \\ref{i1}, \\ref{i2}."));
  CHECK_EQ(noSpaces(items.back()), std::string("See1,2."));
  CHECK(!check::warns(rec("\\begin{enumerate}\\item\\label{i1} a\\end{enumerate}\\ref{i1}\\cref{i1}"), "undefined"));
  const Strings cell = check::lines(rec("\\section{S}\\begin{tabular}{l}a\\label{c1}\\end{tabular}\n\nSee \\ref{c1}."));
  CHECK_EQ(noSpaces(cell.back()), std::string("See1."));
  // An equation's own label is still its number.
  const Strings eq = check::lines(rec("\\begin{equation}a\\label{e1}\\end{equation}See \\ref{e1}."));
  CHECK_EQ(noSpaces(eq.back()), std::string("See1."));
  const Strings al = check::lines(rec("\\begin{align}a&=b\\label{e1}\\\\ c&=d\\label{e2}\\end{align}See \\ref{e1}, \\ref{e2}."));
  CHECK_EQ(noSpaces(al.back()), std::string("See1,2."));
}

// `H1 & H2 \\ \endhead`: the `\\` ends the last row of the head, so the marker after it is no row of its own (pdflatex: the
// body follows the head at once); it was a blank row, a baselineskip tall, in the head, the first head and the foot alike.
TEST(docmode_a_longtable_endhead_after_a_row_end_makes_no_blank_row) {
  CHECK_PAIRS_WITH((Pairs{
    {"\\begin{longtable}{ll}H1&H2\\\\\\endhead a&b\\\\ c&d\\\\\\end{longtable}",
     "\\begin{longtable}{ll}H1&H2\\endhead a&b\\\\ c&d\\\\\\end{longtable}"},
    {"\\begin{longtable}{ll}H1&H2\\\\\\endfirsthead a&b\\\\ c&d\\\\\\end{longtable}",
     "\\begin{longtable}{ll}H1&H2\\endfirsthead a&b\\\\ c&d\\\\\\end{longtable}"},
    {"\\begin{longtable}{ll}a&b\\\\ c&d\\\\ F1&F2\\\\\\endfoot\\end{longtable}",
     "\\begin{longtable}{ll}a&b\\\\ c&d\\\\ F1&F2\\endfoot\\end{longtable}"},
    {"\\begin{longtable}{l}\\hline H\\\\\\hline\\endfirsthead a\\\\ c\\\\\\end{longtable}",
     "\\begin{longtable}{l}\\hline H\\\\ \\hline a\\\\ c\\\\\\end{longtable}"}}),
                   typeface(InputMode::document, 300));
}

// array.sty's \tabularnewline is \\ in a cell that cannot say it (it was unknown, and drawn as its name).
TEST(docmode_tabularnewline_ends_a_row_as_a_double_backslash_does) {
  CHECK(check::sameInk(rec("\\begin{tabular}{l}a\\tabularnewline b\\end{tabular}"), rec("\\begin{tabular}{l}a\\\\ b\\end{tabular}")));
  CHECK(!check::warns(rec("\\begin{tabular}{l}a\\tabularnewline b\\end{tabular}"), "tabularnewline"));
}

// mdframed's title has 5pt of its own above and below (frametitleaboveskip, frametitlebelowskip), so the title is not
// cramped against the line and the box is taller by the title line and 10pt: 1em, at 10pt.
TEST(docmode_an_mdframed_title_has_room_above_and_below) {
  const std::string md = "\\usepackage{mdframed}";
  const auto height = [&](const std::string& opt) {
    const auto rects = check::of(rec(md + "\\begin{mdframed}" + opt + "Body\\end{mdframed}", InputMode::document, 400), DrawRecord::FILL_RECT);
    return rects.empty() ? 0.f : rects[0].height;
  };
  const float plain = height(""), titled = height("[frametitle=Head]");
  CHECK(titled - plain >= 20.f + 20.f - 1.f);   // a text line (1em = 20) and 2 * 0.5em
}

// Found probing items 1-11 (an independent pass, 2026-10-17). A counter that is its own parent, or two that are each other's, was
// unbounded recursion when one stepped: the R process died. LaTeX takes the loop; nothing here may crash.
TEST(docmode_counters_that_are_each_others_parents_do_not_recurse_forever) {
  CHECK(!check::warns(rec("\\newcounter{a}[a]\\stepcounter{a}\\arabic{a}"), "recurs"));
  const auto mutual = rec("\\newcounter{a}[b]\\newcounter{b}[a]\\stepcounter{a}\\stepcounter{b}\\thea\\theb");
  CHECK(mutual.width >= 0);
}

// TeX ignores the space after a command that draws nothing at the start of a line (vertical mode): `\label`, `\setlength` and
// the rest used to leave it, a visible space (5px at 20) before the text, and an indent was not the only thing before it.
TEST(docmode_a_space_after_a_command_that_draws_nothing_at_a_paragraph_start_is_dropped) {
  const auto x = [](const std::string& tex, const std::string& word) {
    for (const auto& r : runs(tex)) {
      if (r.text.find(word) != std::string::npos) return r.x;
    }
    return -1e9f;
  };
  CHECK(close(x("\\section{S}\n\\label{sec}\nText here", "Text"), x("\\section{S}\nText here", "Text"), 0.01f));
  CHECK(close(x("A\n\n\\label{x}\nText", "Text"), x("A\n\nText", "Text"), 0.01f));
  CHECK(close(x("\\setlength{\\parindent}{0pt}\n\\setlength{\\parskip}{6pt}\nA", "A"), x("\\setlength{\\parindent}{0pt}A", "A"), 0.01f));
  CHECK(close(x("\\definecolor{c}{rgb}{1,0,0}\n\\colorlet{d}{c}\nA", "A"), x("A", "A"), 0.01f));
}

// multicols with two paragraphs of several lines was one column (the body's rows were one leaf, not cut between paragraphs):
// pdflatex puts 18 words, a blank line and 18 words into two columns of equal height.
TEST(docmode_multicols_with_several_paragraphs_fills_its_columns) {
  const std::string words =
    "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen";
  const auto two = rec("\\begin{multicols}{2}" + words + "\n\n" + words + "\\end{multicols}", InputMode::document, 300);
  const auto one = rec(words + "\n\n" + words, InputMode::document, 140);   // the same text in one column
  CHECK(static_cast<float>(two.height) < 0.65f * static_cast<float>(one.height));
  CHECK(static_cast<float>(two.height) > 0.35f * static_cast<float>(one.height));
  // Text, a display, text.
  const auto shown = rec("\\begin{multicols}{2}" + words + "\\[x\\]" + words + "\\end{multicols}", InputMode::document, 300);
  const auto flat = rec(words + "\\[x\\]" + words, InputMode::document, 140);
  CHECK(static_cast<float>(shown.height) < 0.7f * static_cast<float>(flat.height));
}

// xcolor's optional model: \textcolor[rgb]{.2,.4,.6}{x} is 0.2, 0.4, 0.6 of full; pdflatex writes `.2 .4 .6 rg`. It was read as
// the colour `[` and drew "rgb] 0.2,0.4,0.6 x" in black. rgb is 0-1, RGB 0-255, HTML six digits, gray 0-1, cmyk.
TEST(docmode_a_colour_in_a_model_is_the_colour_it_names) {
  const auto colour = [](const std::string& tex, unsigned argb) { return check::drawsColour(rec(tex), argb); };
  CHECK(colour("\\textcolor[rgb]{0.2,0.4,0.6}{x}", 0xFF336699u));
  CHECK(colour("\\textcolor[RGB]{51,102,153}{x}", 0xFF336699u));
  CHECK(colour("\\textcolor[HTML]{336699}{x}", 0xFF336699u));
  CHECK(colour("{\\color[HTML]{ff0000} x}", 0xFFFF0000u));
  CHECK(colour("\\textcolor[gray]{0.5}{x}", 0xFF808080u));
  CHECK(colour("\\textcolor[cmyk]{1,0,0,0}{x}", 0xFF00FFFFu));
  CHECK(colour("\\textcolor[named]{red}{x}", 0xFFFF0000u));
  CHECK(colour("\\colorbox[rgb]{1,0,0}{a}", 0xFFFF0000u));
  CHECK(colour("\\fcolorbox[HTML]{FF0000}{00FF00}{a}", 0xFF00FF00u));
  CHECK(colour("\\begin{tabular}{l}\\rowcolor[HTML]{EFEFEF} a\\end{tabular}", 0xFFEFEFEFu));
  CHECK(colour("\\begin{tabular}{l}\\cellcolor[rgb]{0,0,1} a\\end{tabular}", 0xFF0000FFu));
  CHECK(check::warns(rec("\\textcolor[foo]{1,2}{x}"), "the colour model `foo' is not known"));
  CHECK(check::warns(rec("\\textcolor[rgb]{1,2}{x}"), "wrong number of components"));
  CHECK(!check::warns(rec("\\textcolor{red}{x}"), "colour"));
}

// `\arrayrulecolor` in the middle of a table changes the rules after it, not all of them (a rule took the last value when it
// was drawn): pdflatex draws the first two red and the last blue.
TEST(docmode_arrayrulecolor_changes_the_rules_after_it) {
  const auto l = rec("\\begin{tabular}{l}\\arrayrulecolor{red}\\hline a\\\\\\hline c\\\\\\arrayrulecolor{blue}\\hline\\end{tabular}");
  CHECK(check::drawsColour(l, 0xFFFF0000u));
  CHECK(check::drawsColour(l, 0xFF0000FFu));
  // A table after it starts with the rules' own colour again.
  const auto after = rec("\\begin{tabular}{l}\\hline a\\\\\\hline\\end{tabular}");
  CHECK(!check::drawsColour(after, 0xFFFF0000u) && !check::drawsColour(after, 0xFF0000FFu));
}

// `equation` is an ordinary display, not an alignment: after a short line it has the short skips, as `\[ \]` does (pdflatex:
// 12 and 30pt to the next line, where align has 22 and 44). Its number does not change that (TeX centres the formula alone).
TEST(docmode_an_equation_after_a_short_line_has_the_short_display_skips) {
  const auto gap = [](const std::string& display) {
    const std::string t = "A\n" + display + "\nAfter";
    return yOfWord(t, "After", 300) - yOfWord(t, "A", 300);
  };
  const float plain = gap("\\[ a=b \\]");
  CHECK(close(gap("\\begin{equation} a=b \\end{equation}"), plain, 0.5f));
  CHECK(close(gap("\\begin{equation*} a=b \\end{equation*}"), plain, 0.5f));
  CHECK(close(gap("\\[ a=b \\tag{x} \\]"), plain, 0.5f));
  CHECK(gap("\\begin{align} a&=b \\end{align}") > plain + 5.f);   // an alignment starts at the margin: the long skips
}

// LaTeX's default labels change with the level: 1., (a), i., A.; a bullet, an en dash, an asterisk, a centred dot.
TEST(docmode_nested_lists_have_the_labels_of_their_level) {
  // Four levels of `kind`, each opened with the options given for it.
  const auto nest = [](const std::string& kind, const std::string& o2, const std::string& o3, const std::string& o4) {
    const std::string end = "\\end{" + kind + "}";
    return "\\begin{" + kind + "}\\item a\\begin{" + kind + "}" + o2 + "\\item b\\begin{" + kind + "}" + o3 + "\\item c\\begin{" + kind +
           "}" + o4 + "\\item d" + end + end + end + end;
  };
  // The defaults are the labels written out: 1., (a), i., A.
  CHECK(check::sameInk(rec(nest("enumerate", "", "", "")), rec(nest("enumerate", "[(\\alph*)]", "[\\roman*.]", "[\\Alph*.]"))));
  // And a bullet, a bold en dash (latex.ltx: `\labelitemii` is `\normalfont\bfseries\textendash`), an asterisk, a centred dot.
  CHECK(check::sameInk(rec(nest("itemize", "", "", "")), rec(nest("itemize", "[\\text{\\bfseries\\textendash}]", "[\\ast]", "[\\cdot]"))));
  CHECK(!check::sameInk(rec(nest("itemize", "", "", "")), rec(nest("itemize", "[\\text{\\textendash}]", "[\\ast]", "[\\cdot]"))));
  // The second level is not the first: with every level as the first the picture is another.
  CHECK(!check::sameInk(rec(nest("enumerate", "", "", "")), rec(nest("enumerate", "[\\arabic*.]", "[\\arabic*.]", "[\\arabic*.]"))));
}

// makecell's cell is a tabular of `@{}c@{}`: no \tabcolsep inside it (12bp at 10pt too wide, pdflatex 44.9 against 56.9), and
// \shortstack is the same stack with its last line on the baseline.
TEST(docmode_makecell_and_shortstack_have_no_padding_of_their_own) {
  CHECK_PAIRS_WITH((Pairs{
    {"\\begin{tabular}{cc}\\makecell{a\\\\bb}&x\\end{tabular}",
     "\\begin{tabular}{cc}\\begin{tabular}{@{}c@{}}a\\\\bb\\end{tabular}&x\\end{tabular}"},
    {"\\begin{tabular}{cc}\\makecell[l]{a\\\\bb}&x\\end{tabular}",
     "\\begin{tabular}{cc}\\begin{tabular}[c]{@{}l@{}}a\\\\bb\\end{tabular}&x\\end{tabular}"},
    {"\\begin{tabular}{cc}\\shortstack{a\\\\bb}&x\\end{tabular}",
     "\\begin{tabular}{cc}\\begin{tabular}[b]{@{}c@{}}a\\\\bb\\end{tabular}&x\\end{tabular}"}}),
                   typeface(InputMode::document, 300));
}


// The figure and table counters are there to set (`\setcounter{figure}{4}` makes the next caption Figure 5), and \nocite draws
// nothing.
TEST(docmode_the_float_counters_exist_and_nocite_is_silent) {
  const auto caption = [](const std::string& tex) {
    for (const auto& line : check::lines(rec(tex))) {
      if (line.find("Figure") != std::string::npos) return noSpaces(line);
    }
    return std::string();
  };
  CHECK_EQ(caption("\\setcounter{figure}{4}\\begin{figure}F\\caption{c}\\end{figure}"), std::string("Figure5:c"));
  CHECK_EQ(caption("\\begin{figure}F\\caption{c}\\end{figure}"), std::string("Figure1:c"));
  CHECK(!check::warns(rec("\\setcounter{figure}{4}\\thefigure \\thetable"), "counter"));
  CHECK(check::of(rec("\\nocite{a}\\nocite{*}"), DrawRecord::TEXT).empty());
}

// latex.ltx: \settowidth, \settoheight, \settodepth and \settototalheight put the size of what is set in a box into a length.
// Here a run is 0.55 em a character (the test's text measurer), so \settowidth{\gw}{ab} then \hspace{\gw} is as wide as `ab`.
TEST(docmode_settowidth_sets_a_length_to_the_width_of_its_text) {
  const auto width = [](const std::string& tex) { return check::render(tex, InputMode::mixed).width; };
  CHECK_EQ(width("\\newlength{\\gw}\\settowidth{\\gw}{ab}\\hspace{\\gw}x"), width("abx"));
  CHECK_EQ(width("\\newlength{\\gw}\\settowidth{\\gw}{abc}\\hspace{\\gw}x"), width("abcx"));
  CHECK_EQ(width("\\newdimen\\gd \\settowidth\\gd{$x^2$}\\hspace{\\gd}"), width("$x^2$"));
  // a name that is not a length is said
  CHECK(check::warns(check::render("\\settowidth{\\nolength}{ab}", InputMode::mixed), "is not a length"));
}

// tex.web 762: \left( ... \right) is as tall as twice the larger reach of its contents above and below the axis (at least 901/1000 of
// that), not as tall as the contents: x^2 reaches 5.6pt above the axis, so its parentheses are the next size (LuaLaTeX: 8.28pt high
// and 2.97 deep, where the plain parenthesis is 7.50 and 2.50), while those round `x` stay the plain size.
TEST(docmode_left_right_delimiters_are_sized_from_the_reach_about_the_axis) {
  const auto set = [](const std::string& tex) { return check::render(tex, InputMode::math); };
  CHECK(set("\\left(x^2\\right)").height > set("(x^2)").height);
  CHECK(set("\\left(x^2\\right)").height > set("\\left(x\\right)").height);
  CHECK_EQ(set("\\left(x\\right)").height, set("(x)").height);
}

// amsmath: \smallmatrix is `\vcenter`ed, and a \vcenter is centred on the axis of the style around it, not of the script style its cells
// are in (LuaLaTeX: 8.18pt above the baseline and 3.18 below, ours was 7.43 and 3.91): the same centre as a matrix of the same rows.
TEST(docmode_a_smallmatrix_is_centred_on_the_axis_of_its_surroundings) {
  check::Options opt;
  opt.textSize = 100;
  const auto set = [&](const std::string& tex) { return check::render(tex, opt); };
  const auto small = set("\\begin{smallmatrix}a&b\\\\c&d\\end{smallmatrix}");
  const auto plain = set("\\begin{matrix}a&b\\\\c&d\\end{matrix}");
  // (height is the whole box; baseline the share of it above the baseline: the reach above less the reach below is twice the axis)
  const auto skew = [](const check::Layout& l) { return l.height * l.baseline - (l.height - l.height * l.baseline); };
  CHECK(std::fabs(skew(small) - skew(plain)) < 1.5f);
}

// tex.web 1151: a \mathchoice is replaced by the list it picks before the spaces are set, so a relation in it is spaced as one:
// `a \centernot\longrightarrow b` (centernot's \mathpalette) is 5.6pt wider than an Ord of the same arrow in LuaLaTeX.
TEST(docmode_a_mathchoice_is_spaced_as_the_list_it_picks) {
  const auto set = [](const std::string& tex) { return check::render(tex, InputMode::math); };
  CHECK(check::sameInk(set("a\\mathchoice{\\mathrel{=}}{\\mathrel{=}}{\\mathrel{=}}{\\mathrel{=}}b"), set("a=b")));
}
