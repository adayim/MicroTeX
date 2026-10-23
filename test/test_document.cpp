// The core-LaTeX text a pasted document uses -- \verb and verbatim,
// description lists, small caps, theorems with proofs -- the document
// commands a grob drops or draws as their nearest equivalent, and equation
// numbers. Numbers come from the stub text measurer (engine.h): these look
// at what is drawn, in what order, and in which face.

#include <algorithm>
#include <cmath>
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

// font_style: bit 1 bold, bit 2 italic, bit 128 the typewriter face.
bool isMono(const DrawRecord& r) { return (r.font_style & 128) != 0; }
bool isBold(const DrawRecord& r) { return (r.font_style & 2) != 0; }
bool isItalic(const DrawRecord& r) { return (r.font_style & 4) != 0; }

Options typeface(InputMode mode = InputMode::document, float width = 300) {
  Options opt(mode, width);
  opt.path = false;
  return opt;
}

check::Layout rec(const std::string& tex, InputMode mode = InputMode::document, float width = 300) {
  return check::render(tex, typeface(mode, width));
}

// The lines of text of a layout, top to bottom, each its text records left
// to right. A space is a gap between records, not a record, so the lines
// have none; `ns()` takes them out of what a test expects.
Strings linesOf(const std::string& tex, InputMode mode = InputMode::document, float width = 300) {
  return check::lines(rec(tex, mode, width));
}

std::string ns(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

Strings ns(Strings v) {
  for (auto& s : v) s = ns(s);
  return v;
}

Records textRecords(const std::string& tex, InputMode mode = InputMode::document, float width = 300) {
  return check::of(rec(tex, mode, width), DrawRecord::TEXT);
}

std::string joined(const Records& rs) {
  std::string out;
  for (const auto& r : rs) out += r.text;
  return out;
}

bool quiet(const std::string& tex, InputMode mode = InputMode::document, float width = 300) {
  return rec(tex, mode, width).warnings.empty();
}

// Where the last of some text records ends: the stub measures a character as 11.
float endOf(const Records& rs) {
  float end = -1e9f;
  for (const auto& r : rs) {
    std::size_t characters = 0;
    for (const unsigned char c : r.text) characters += (c & 0xC0) != 0x80 ? 1 : 0;
    end = std::max(end, r.x + 11.0f * static_cast<float>(characters));
  }
  return end;
}

bool has(const Strings& all, const std::string& s) {
  for (const auto& x : all) {
    if (x == s) return true;
  }
  return false;
}

}  // namespace

// --- \verb, verbatim, descriptions, small caps, theorems -------------------

TEST(document_verb_sets_its_text_as_written_in_the_typewriter_face) {
  const std::string tex = R"(see \verb|a_b & 50% \x{y}| here)";
  CHECK(quiet(tex));
  Records mono, rest;
  for (const auto& r : textRecords(tex)) (isMono(r) ? mono : rest).push_back(r);
  CHECK_EQ(joined(mono), std::string(R"(a_b & 50% \x{y})"));
  CHECK(rest.size() > 0);
  // Any character can delimit it, a brace included.
  CHECK_EQ(linesOf(R"(\verb+a|b+)"), Strings({"a|b"}));
  CHECK_EQ(linesOf(R"(\verb{a}b{)"), Strings({"a}b"}));
}

TEST(document_verb_star_shows_its_spaces) {
  CHECK_EQ(linesOf(R"(\verb*|a b|)"), Strings({"a\u2423b"}));
}

TEST(document_verb_works_in_a_label) {
  CHECK_EQ(linesOf(R"(call \verb|f(x)| now)", InputMode::mixed), Strings({ns("call f(x) now")}));
}

TEST(document_a_verb_that_does_not_end_on_its_line_warns_and_takes_the_line) {
  const auto l = rec("\\verb|abc\nnext line");
  CHECK(check::warns(l, "verb"));
  bool seen = false;
  for (const auto& line : check::lines(l)) seen = seen || line.find("nextline") != std::string::npos;
  CHECK(seen);
}

TEST(document_verbatim_keeps_every_line_its_indentation_and_its_specials) {
  const std::string tex = "\\begin{verbatim}\nx <- 1 # 50%\n  f(x_1) & {y}\n\\end{verbatim}\nafter";
  CHECK(quiet(tex));
  const auto lines = linesOf(tex);
  // Each line is one run, its indentation in it.
  CHECK_EQ(lines, Strings({"x <- 1 # 50%", "  f(x_1) & {y}", "after"}));
  int verbatim = 0;
  for (const auto& r : textRecords(tex)) {
    if (r.text != "after") {
      CHECK(isMono(r));
      verbatim++;
    }
  }
  CHECK_EQ(verbatim, 2);
}

TEST(document_verbatim_reads_a_backslash_command_as_text_and_ends_at_its_own_end) {
  const std::string tex = "\\begin{verbatim}\n\\begin{itemize} \\end{itemize}\n\\end{verbatim}";
  CHECK_EQ(linesOf(tex), Strings({R"(\begin{itemize} \end{itemize})"}));
  CHECK(quiet(tex));
}

TEST(document_verbatim_star_shows_spaces_and_an_unclosed_verbatim_warns) {
  CHECK_EQ(linesOf("\\begin{verbatim*}\na b\n\\end{verbatim*}"), Strings({"a\u2423b"}));
  CHECK(check::warns(rec("\\begin{verbatim}\nabc"), "verbatim"));
}

TEST(document_a_whole_document_finds_its_body_around_a_verbatim_end_document) {
  const std::string tex =
    "\\documentclass{article}\\begin{document}\n\\begin{verbatim}\n\\end{document}\n\\end{verbatim}\n"
    "kept \\end{document} dropped";
  CHECK_EQ(linesOf(tex), Strings({"\\end{document}", "kept"}));
}

// listings, minted and fancyvrb: their code is set as verbatim is, and their options (a look this package does not
// draw) are read and left out, with no warning: a pasted `[language=R]` is not a problem. A document that
// \usepackage{...}s one of them keeps its settings (\lstset...) as no-ops.
TEST(document_listings_minted_and_fancyvrb_set_their_code_as_verbatim_does) {
  const std::string body = "x <- c(1, 2) # 50% & a_b\n  f(\\x{y})";
  const Strings expected = linesOf("\\begin{verbatim}\n" + body + "\n\\end{verbatim}");
  CHECK_EQ(expected, Strings({"x <- c(1, 2) # 50% & a_b", "  f(\\x{y})"}));
  for (const char* head : {"\\begin{lstlisting}", "\\begin{lstlisting}[language=R, basicstyle=\\small, caption={a, b}]",
                           "\\begin{minted}{r}", "\\begin{minted}[linenos, frame=single]{python}",
                           "\\begin{Verbatim}", "\\begin{Verbatim}[fontsize=\\small]"}) {
    const std::string name = std::string(head).substr(7, std::string(head).find_first_of("}") - 7);
    const std::string tex = std::string(head) + "\n" + body + "\n\\end{" + name + "}";
    CHECK_EQ(linesOf(tex), expected);
    CHECK(quiet(tex));
    for (const auto& r : textRecords(tex)) CHECK(isMono(r));
  }
  CHECK_EQ(linesOf("\\begin{Verbatim*}\na b\n\\end{Verbatim*}"), Strings({"a\u2423b"}));
  // The rest of the \begin line is no code (spaces after the options), and an option holds a `]` in braces.
  CHECK_EQ(linesOf("\\begin{lstlisting}[caption={a]b}]  \nx\n\\end{lstlisting}"), Strings({"x"}));
  // An option is no code: a body that opens with a bracket on the next line is code.
  CHECK_EQ(linesOf("\\begin{lstlisting}\n[1] 5\n\\end{lstlisting}"), Strings({"[1] 5"}));
  // Unclosed options and a missing language warn once and the code is still there.
  CHECK(check::warns(rec("\\begin{lstlisting}[a=b\nx\n\\end{lstlisting}"), "is not closed on its line"));
  CHECK(check::warns(rec("\\begin{minted}\nx\n\\end{minted}"), "required argument"));
}

TEST(document_the_inline_code_of_listings_minted_and_fancyvrb_is_set_as_verb_is) {
  CHECK_EQ(linesOf(R"(\lstinline|a_b & 50%|)"), linesOf(R"(\verb|a_b & 50%|)"));
  CHECK_EQ(linesOf(R"(\lstinline[language=R]!x_1!)"), linesOf(R"(\verb!x_1!)"));
  CHECK_EQ(linesOf(R"(\mintinline{r}|x_1 # c|)"), linesOf(R"(\verb|x_1 # c|)"));
  CHECK_EQ(linesOf(R"(\mintinline[bgcolor=white]{python}|x_1|)"), linesOf(R"(\verb|x_1|)"));
  CHECK_EQ(linesOf(R"(\Verb|a & b|)"), linesOf(R"(\verb|a & b|)"));
  // Braces delimit the code of \lstinline and \mintinline, and a brace inside them is part of it.
  CHECK_EQ(linesOf(R"(\lstinline{a_b})"), linesOf(R"(\verb|a_b|)"));
  CHECK_EQ(linesOf(R"(\lstinline{f(x){y}})"), linesOf(R"(\verb|f(x){y}|)"));
  CHECK_EQ(linesOf(R"(\mintinline{r}{x_1})"), linesOf(R"(\verb|x_1|)"));
  CHECK(quiet(R"(see \lstinline{a_b & 50%} and \mintinline{r}{x # y} here)"));
  for (const auto& r : textRecords(R"(\lstinline|abc|)")) CHECK(isMono(r));
}

TEST(document_an_end_document_inside_a_listing_is_text_and_the_settings_are_no_ops) {
  const std::string tex =
    "\\documentclass{article}\\usepackage{listings}\\lstset{language=R, basicstyle=\\ttfamily}\\begin{document}\n"
    "\\begin{lstlisting}\n\\end{document}\n\\end{lstlisting}\nkept \\end{document} dropped";
  CHECK_EQ(linesOf(tex), Strings({"\\end{document}", "kept"}));
  CHECK(quiet(tex));
  CHECK(quiet("\\usepackage{minted}\\usemintedstyle{monokai}\\setminted{fontsize=\\small}x"));
  CHECK(quiet("\\usepackage{fancyvrb}\\fvset{fontsize=\\small}x"));
  CHECK(quiet("\\usepackage{listings}\\lstdefinestyle{s}{basicstyle=\\small}\\lstdefinelanguage[x]{y}{morekeywords={a}}x"));
  // A listing is set apart from its paragraph, as a list is, and works in a list item and with no body at all.
  CHECK_EQ(linesOf("Before\n\\begin{lstlisting}\nin\n\\end{lstlisting}\nafter"), Strings({"Before", "in", "after"}));
  CHECK_EQ(linesOf("\\begin{lstlisting}\n\\end{lstlisting}").size(), std::size_t(1));
  CHECK(check::warns(rec("\\begin{lstlisting}\nabc"), "missing \\end{lstlisting}"));
}

TEST(document_description_sets_a_bold_label_hanging_before_each_item) {
  const std::string tex = "\\begin{description}\\item[Alpha] first\\item[Beta] second\\end{description}";
  CHECK(quiet(tex));
  const auto r = textRecords(tex);
  Records labels, texts;
  for (const auto& x : r) {
    if (x.text == "Alpha" || x.text == "Beta") labels.push_back(x);
    if (x.text == "first" || x.text == "second") texts.push_back(x);
  }
  CHECK_EQ(labels.size(), std::size_t(2));
  CHECK_EQ(texts.size(), std::size_t(2));
  CHECK(isBold(labels[0]) && isBold(labels[1]));
  // The two labels share a left edge, and their texts another.
  CHECK_EQ(labels[0].x, labels[1].x);
  CHECK_EQ(texts[0].x, texts[1].x);
  CHECK(texts[0].x > labels[0].x + 10);
  CHECK_EQ(linesOf(tex), Strings({"Alphafirst", "Betasecond"}));
}

TEST(document_an_item_label_is_the_marker_of_an_itemize_or_enumerate_item) {
  const auto l = rec("\\begin{enumerate}\\item[(a)] one\\item next\\end{enumerate}");
  const auto marks = check::of(l, DrawRecord::GLYPH);
  CHECK(marks.size() > 0);
  const auto ys = check::column(marks, &DrawRecord::y);
  const float top = check::minimum(ys), bottom = check::maximum(ys);
  std::vector<DrawRecord> first, second;
  for (const auto& m : marks) (std::round(m.y) == std::round(top) ? first : second).push_back(m);
  CHECK(std::round(top) != std::round(bottom));
  // "(a)" for the labelled item; the next one is the first counted, "1.",
  // as in a list with no label at all.
  CHECK_EQ(first.size(), std::size_t(3));
  const auto plain = check::of(rec(R"(\begin{enumerate}\item next\end{enumerate})"), DrawRecord::GLYPH);
  const auto byX = [](std::vector<DrawRecord> v) {
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    std::vector<int> ids;
    for (const auto& r : v) ids.push_back(r.glyph_id);
    return ids;
  };
  CHECK_EQ(byX(second), byX(plain));
  // No bracket is drawn, and a label in itemize is the item's marker.
  const auto star = rec("\\begin{itemize}\\item[$\\star$] one\\item two\\end{itemize}");
  // (a math font without the star draws it as text, as a row of math does with any character the font lacks)
  bool starAsText = false;
  for (const auto& t : check::of(star, DrawRecord::TEXT)) starAsText = starAsText || t.text == "\xe2\x8b\x86";
  CHECK(check::of(star, DrawRecord::GLYPH).size() == 2 || starAsText);
}

TEST(document_textsc_draws_capitals_the_lowercase_ones_smaller) {
  const auto r = textRecords("\\textsc{Hello World}", InputMode::mixed);
  CHECK_EQ(joined(r), std::string("HELLOWORLD"));
  CHECK_EQ(check::uniqueSorted(check::column(r, &DrawRecord::font_size), 0), std::vector<double>({16, 20}));
  // H and W are the big ones.
  for (const auto& x : r) {
    if (x.text == "H") CHECK_EQ(std::round(x.font_size), 20.0f);
    if (x.text == "ELLO") CHECK_EQ(std::round(x.font_size), 16.0f);
  }
  // The declaration does the same, to the end of its group.
  const auto r2 = textRecords("{\\scshape Hello} plain", InputMode::mixed);
  CHECK_EQ(joined(r2), std::string("HELLOplain"));
  for (const auto& x : r2) {
    if (x.text == "plain") CHECK_EQ(std::round(x.font_size), 20.0f);
  }
}

TEST(document_textsc_capitalises_the_text_of_a_command_not_the_names_it_takes) {
  // The colour is still red: its name was not made RED and lost.
  const auto r = textRecords("\\textsc{\\textcolor{red}{abc}}", InputMode::mixed);
  CHECK_EQ(joined(r), std::string("ABC"));
  for (const auto& x : r) CHECK_EQ(x.col, check::kRed);
  // A reference keeps its key, so it still finds its label.
  const std::string tex = "\\begin{equation}\\label{eq:a} x \\end{equation}\\textsc{see \\ref{eq:a}}";
  const auto t = check::texts(rec(tex));
  CHECK(has(t, "1"));
  CHECK(!has(t, "??"));
  // What a nested command takes as text is still set in capitals.
  CHECK_EQ(joined(textRecords("\\textsc{abc \\textbf{def}}", InputMode::mixed)), std::string("ABCDEF"));
}

namespace {
const std::string thm = "\\newtheorem{thm}{Theorem}\n";
}

TEST(document_a_theorem_is_numbered_titled_in_bold_and_set_in_italics) {
  const std::string tex = thm + "\\begin{thm}It holds.\\end{thm}\n\\begin{thm}And again.\\end{thm}";
  CHECK(quiet(tex));
  CHECK_EQ(linesOf(tex), ns(Strings({"Theorem 1.It holds.", "Theorem 2.And again."})));
  for (const auto& r : textRecords(tex)) {
    if (r.text.rfind("Theorem", 0) == 0) CHECK(isBold(r));
    if (r.text.find("holds") != std::string::npos) {
      CHECK(!isBold(r));
      CHECK(isItalic(r));
    }
  }
}

TEST(document_a_theorem_takes_a_note_and_newtheorem_star_is_unnumbered) {
  const std::string tex = thm + "\\newtheorem*{rem}{Remark}\n"
                                "\\begin{thm}[Euler]Body.\\end{thm}\n\\begin{rem}Plain.\\end{rem}";
  CHECK_EQ(linesOf(tex), ns(Strings({"Theorem 1 (Euler).Body.", "Remark.Plain."})));
}

TEST(document_theorems_share_a_counter_or_restart_within_a_section) {
  const std::string shared =
    thm + "\\newtheorem{lem}[thm]{Lemma}\n\\begin{thm}A\\end{thm}\\begin{lem}B\\end{lem}";
  CHECK_EQ(linesOf(shared), ns(Strings({"Theorem 1.A", "Lemma 2.B"})));
  const std::string within =
    "\\newtheorem{thm}{Theorem}[section]\n"
    "\\section{One}\\begin{thm}A\\end{thm}\\begin{thm}B\\end{thm}\n"
    "\\section{Two}\\begin{thm}C\\end{thm}";
  const auto lines = linesOf(within);
  for (const auto& want : ns(Strings({"Theorem 1.1.A", "Theorem 1.2.B", "Theorem 2.1.C"}))) {
    CHECK(has(lines, want));
  }
}

TEST(document_a_theorem_can_be_labelled_and_referred_to) {
  const std::string tex = thm + "\\begin{thm}\\label{t:a}A\\end{thm}\nBy Theorem~\\ref{t:a} and then.";
  CHECK(quiet(tex));
  CHECK(has(linesOf(tex), ns("By Theorem 1 and then.")));
}

TEST(document_a_proof_ends_with_a_box_and_an_environment_never_defined_warns) {
  const std::string tex = "\\begin{proof}Trivial.\\end{proof}";
  const auto lines = linesOf(tex);
  CHECK(lines[0].rfind("Proof.Trivial.", 0) == 0);
  const auto l = rec(tex);
  // the box is a glyph of the math font, or text where the font has none (the test's has no square)
  auto box = check::of(l, DrawRecord::GLYPH);
  float rightmost = -1e9f;
  for (const auto& t : check::of(l, DrawRecord::TEXT)) {
    if (t.text == "\xe2\x97\xbb" || t.text == "\xe2\x96\xa1") box.push_back(t); else rightmost = std::max(rightmost, t.x);
  }
  if (box.size() != 1) check::fail(__FILE__, __LINE__, "DEBUG " + check::describe(l));
  CHECK_EQ(box.size(), std::size_t(1));
  if (box.empty()) return;
  CHECK(box[0].x > rightmost);
  CHECK(quiet(tex));
  for (const auto& t : check::of(l, DrawRecord::TEXT)) {
    if (t.text == "Trivial.") CHECK(!isItalic(t));  // upright
  }
  CHECK(check::warns(rec("\\begin{nothm}x\\end{nothm}"), "nothm"));
}

TEST(document_a_proof_wraps_to_the_text_width_as_any_paragraph_does) {
  std::string longText;
  for (int i = 0; i < 8; i++) longText += (i ? " " : "") + std::string("some words that need room");
  const auto r = textRecords("\\begin{proof}" + longText + "\\end{proof}", InputMode::document, 200);
  CHECK(check::distinct(check::column(r, &DrawRecord::y), 0) > 1);
  CHECK(check::maximum(check::column(r, &DrawRecord::x)) <= 200 + 30);
}

TEST(document_a_font_switch_lasts_across_a_paragraph_break) {
  for (const auto& r : textRecords("x {\\itshape one\\par two} three")) {
    if (r.text == "one" || r.text == "two") CHECK(isItalic(r));
    if (r.text == "x" || r.text == "three") CHECK(!isItalic(r));
  }
}

// --- the document commands a grob drops or draws as the nearest ------------

namespace {

void drawsAs(const std::string& input, const std::string& equivalent, InputMode mode = InputMode::mixed) {
  CHECK_PAIRS_WITH((Pairs{{input, equivalent}}), Options(mode));
}

}  // namespace

TEST(document_the_preamble_title_metadata_and_alignment_declarations_draw_nothing) {
  drawsAs(
    "\\documentclass[12pt]{article}\\usepackage[utf8]{inputenc}"
    "\\usepackage{amsmath}\\begin{document}$x^2$\\end{document}",
    "$x^2$");
  // \title, \author and \date are kept for \maketitle and draw nothing themselves.
  drawsAs("\\title{Foo}\\author{Bar}\\date{Baz}\\label{eq:1}content", "content");
  drawsAs("\\bibliographystyle{plain}content", "content");
}

TEST(document_a_whole_files_preamble_is_read_for_its_definitions_and_not_drawn) {
  // Nothing in it is drawn or warned about, its definitions hold, and what
  // follows \end{document} is ignored.
  const std::string file =
    "\\documentclass{article}\n"
    "\\PassOptionsToPackage{numbers, compress}{natbib}\n"
    "\\RequirePackage[final]{nips}\\hypersetup{colorlinks=true}\n"
    "stray preamble text \\unknowncommand{x}\n"
    "\\newcommand{\\R}{\\mathbb{R}}\n"
    "\\definecolor{brand}{RGB}{200,30,30}\n"
    "\\begin{document}\n"
    "Body $\\R$ in \\textcolor{brand}{colour}.\n"
    "\\end{document}\n"
    "Notes after the end.";
  const std::string body =
    "\\newcommand{\\R}{\\mathbb{R}}\\definecolor{brand}{RGB}{200,30,30}"
    "Body $\\R$ in \\textcolor{brand}{colour}.";
  for (const InputMode mode : {InputMode::document, InputMode::mixed}) {
    const auto g = check::render(file, Options(mode));
    CHECK(g.warnings.empty());
    const auto b = check::render(body, Options(mode));
    CHECK_EQ(check::texts(g), check::texts(b));
    const auto colours = [](const check::Layout& l) {
      std::vector<unsigned> out;
      for (const auto& r : check::ink(l)) out.push_back(r.col);
      return out;
    };
    CHECK_EQ(colours(g), colours(b));
    // The preamble's \definecolor took effect (xcolor's RGB model, 0-255).
    for (const auto& r : check::of(g, DrawRecord::TEXT)) {
      if (r.text == "colour") CHECK_EQ(r.col, 0xFFC81E1Eu);
    }
  }
  // xcolor's HTML model too.
  for (const auto& r : check::of(check::render("\\definecolor{c}{HTML}{C81E1E}\\textcolor{c}{x}",
                                               Options(InputMode::mixed)),
                                 DrawRecord::TEXT)) {
    if (r.text == "x") CHECK_EQ(r.col, 0xFFC81E1Eu);
  }
  // Without \begin{document} nothing is taken for a preamble.
  CHECK(check::warns(check::render("\\unknowncommand{x} text", Options(InputMode::document)), "unknown command"));
  drawsAs("\\graphicspath{{figs/}}\\DeclareGraphicsExtensions{.png}x", "x");
  drawsAs("% leading comment\n$x^2$", "$x^2$");
  for (const char* cmd : {"centering", "raggedright", "raggedleft", "flushleft", "flushright", "noindent", "relax"}) {
    drawsAs(std::string("\\") + cmd + " x", "x");
  }
}

TEST(document_nothing_begun_in_the_preamble_reaches_into_the_body) {
  const auto file = [](const std::string& pre) {
    return "\\documentclass{article}\n" + pre + "\n\\begin{document}Hello \\emph{world}\\end{document}";
  };
  for (const InputMode mode : {InputMode::document, InputMode::mixed}) {
    const Options opt(mode);
    const auto plain = check::render(file(""), opt);
    CHECK_EQ(check::texts(plain), Strings({"Hello ", "world"}));
    for (const char* pre : {"\\large", "\\color{red}", "\\bfseries", "\\begin{foo}", "a \\over b",
                            "\\begin{itemize}\\item x", "\\newcommand{\\two}[2]{#1#2}\\two{x}",
                            "\\def\\upto#1.{#1}\\upto abc", "\\title"}) {
      const auto got = check::render(file(pre), opt);
      CHECK(got.warnings.empty());
      CHECK(check::sameInk(got, plain));
    }
  }
}

TEST(document_nothing_after_end_document_is_read) {
  const std::string body = "\\documentclass{article}\n\\begin{document}Hello\\end{document}\n";
  const Options opt(InputMode::document);
  const auto plain = check::render(body, opt);
  // Not taken into the body, not an error, not warned about.
  for (const char* post : {"a \\over b", "\\def\\a{\\a}\\a", "} { $ \\unknown", "\\begin{itemize}"}) {
    const auto got = check::render(body + post, opt);
    CHECK(got.warnings.empty());
    CHECK(check::sameInk(got, plain));
  }
}

TEST(document_floats_keep_their_contents) {
  drawsAs(
    "\\begin{table}[ht]\n\\centering\n\\begin{tabular}{rr} a & b \\\\ \\end{tabular}\n\\end{table}",
    "\\begin{tabular}{rr} a & b \\\\ \\end{tabular}");
  drawsAs("\\begin{figure*}[ht]x\\end{figure*}", "x");
  drawsAs("\\begin{table*}[ht]x\\end{table*}", "x");
}

// booktabs' rules are rules with its own widths and space (\heavyrulewidth .08em, \lightrulewidth .05em,
// \aboverulesep .4ex, \belowrulesep .65ex), which pdflatex measures (scratchpad/col/tabs.tex, 10pt, bp from the rule's
// centre or the baseline): top rule to first baseline 11.4, baseline to \midrule 5.7, \midrule to baseline 11.5, last
// baseline to bottom rule 5.6; \cmidrule has \midrule's.
TEST(document_booktabs_rules_are_rules_with_booktabs_space_round_them) {
  drawsAs(
    "\\begin{tabular}{lr}\\toprule x & y \\\\ \\midrule 1 & 2 \\\\ \\bottomrule\\end{tabular}",
    "\\begin{tabular}{lr}\\specialrule{0.08em}{0pt}{0.65ex} x & y \\\\ \\specialrule{0.05em}{0.4ex}{0.65ex} 1 & 2 \\\\ "
    "\\specialrule{0.08em}{0.4ex}{0pt}\\end{tabular}");
  // \cmidrule is as tall as \midrule (it spans columns, so it is not as wide).
  CHECK(std::fabs(rec("\\begin{tabular}{lr}x & y \\\\ \\cmidrule{1-2} 1 & 2\\end{tabular}").height -
                  rec("\\begin{tabular}{lr}x & y \\\\ \\midrule 1 & 2\\end{tabular}").height) < 0.01f);
  drawsAs("\\begin{tabular}{lr}\\toprule[2pt] x & y \\\\ \\bottomrule[2pt]\\end{tabular}",
          "\\begin{tabular}{lr}\\specialrule{2pt}{0pt}{0.65ex} x & y \\\\ \\specialrule{2pt}{0.4ex}{0pt}\\end{tabular}");
  // \specialrule's space is real: 1em above and 1em below make the table 2em taller.
  const auto h = [](const std::string& rule) {
    return rec("\\begin{tabular}{l}x \\\\ " + rule + " y\\end{tabular}").height;
  };
  CHECK(std::fabs((h("\\specialrule{0.05em}{1em}{1em}") - h("\\specialrule{0.05em}{0pt}{0pt}")) - 2 * 20.f) < 0.5f);
  // \cmidrule keeps its column range; width and trims are ignored.
  for (const char* rule : {"\\cmidrule{2-3}", "\\cmidrule(lr){2-3}", "\\cmidrule[2pt]{2-3}",
                           "\\cmidrule[2pt](lr){2-3}"}) {
    drawsAs(std::string("\\begin{tabular}{lrr}x & y & z \\\\ ") + rule + " 1 & 2 & 3\\end{tabular}",
            "\\begin{tabular}{lrr}x & y & z \\\\ \\cmidrule{2-3} 1 & 2 & 3\\end{tabular}");
  }
}

TEST(document_text_commands_a_grob_has_no_equivalent_for_are_drawn_as_the_nearest) {
  drawsAs("\\emph{stress}", "\\textit{stress}");
  drawsAs("\\emph{a $x_{i}$ b}", "\\textit{a $x_{i}$ b}");
  drawsAs("a\\par b", "a\\\\b");
  drawsAs("a\\newline b", "a\\\\b");
  // Fixed space. A space after a command word is dropped, as in TeX.
  drawsAs("a\\smallskip b", "a\\vspace{0.3em}b");
  drawsAs("a\\medskip b", "a\\vspace{0.6em}b");
  drawsAs("a\\bigskip b", "a\\vspace{1.2em}b");
  drawsAs("a\\vfill b", "a\\vspace{1em}b");
  drawsAs("a\\hfill b", "a\\quad b");
}

TEST(document_a_caption_is_a_line_of_text_where_it_is_written) {
  drawsAs("\\caption{Hello}", "\\text{Hello}\\\\");
  drawsAs("a \\caption{Hi} b", "a \\text{Hi}\\\\ b");
  drawsAs("\\caption[short]{Long}", "\\text{Long}\\\\");
  drawsAs("\\caption{Foo $x_{i}$}", "\\text{Foo $x_{i}$}\\\\");
}

TEST(document_a_caption_note_or_run_in_heading_is_set_once_per_line_it_runs_over) {
  // As \textbf{a\\b} is.
  drawsAs("\\caption{one\\\\two}", "\\text{one}\\\\\\text{two}\\\\");
  drawsAs("\\paragraph{One\\\\Two} text", "\\paragraph{One}\\\\\\paragraph{Two} text");
}

TEST(document_links_look_like_latexs_and_a_url_is_drawn_as_written) {
  const std::string col = "#0969DA";  // as hyperref with colorlinks: coloured, not underlined
  const auto colour = [&](const std::string& body) { return "\\textcolor{" + col + "}{" + body + "}"; };
  drawsAs("\\href{https://ex.org}{the text}", colour("the text"));
  drawsAs("\\href{u}{\\textbf{b}}", colour("\\textbf{b}"));
  // A URL in monospace.
  drawsAs("\\url{https://ex.org}", colour("\\texttt{https://ex.org}"));
  // A URL's specials are characters.
  drawsAs("\\url{a.io/x_y}", colour("\\texttt{a.io/x\\_y}"));
  drawsAs("\\url{a.io/x%20y} tail", colour("\\texttt{a.io/x\\%20y}") + " tail");
  drawsAs("\\url{a.io/~u#top}", colour("\\texttt{a.io/\\char126{}u\\#top}"));
  drawsAs("\\href{a.io/%7E#x}{t} tail", colour("t") + " tail");
  CHECK(check::warns(check::render("\\href{only-one-group}", Options(InputMode::mixed)), "missing argument for \\href"));
  // Nothing in a URL is a command.
  CHECK_EQ(check::texts(rec("\\url{a\\b{c}d}", InputMode::mixed)), Strings({"a\\b{c}d"}));
  // An \href's text is in the mode around it, and a line break in it ends
  // the line with the rest still a link.
  drawsAs("$\\href{u}{x^2}$", "$" + colour("x^2") + "$");
  const auto lines = check::of(rec("\\href{u}{a\nb}", InputMode::mixed), DrawRecord::TEXT);
  CHECK_EQ(lines.size(), std::size_t(2));
  CHECK_EQ(lines[0].col, 0xFF0969DAu);
  CHECK_EQ(lines[1].col, 0xFF0969DAu);
  CHECK(lines[0].y != lines[1].y);
}

TEST(document_a_starred_environment_is_its_plain_form_without_its_number) {
  drawsAs("\\begin{align*}a&=b\\end{align*}", "\\begin{align}a&=b\\notag\\end{align}");
  drawsAs("\\begin{alignat*}{1}a&=b\\end{alignat*}", "\\begin{alignat}{1}a&=b\\notag\\end{alignat}");
  drawsAs("\\begin{equation*}x\\end{equation*}", "\\begin{equation}x\\notag\\end{equation}");
  // tabular*'s width is ignored.
  drawsAs("\\begin{tabular*}{\\textwidth}{lcr}a&b&c\\end{tabular*}", "\\begin{tabular}{lcr}a&b&c\\end{tabular}");
}

// --- equation numbers -------------------------------------------------------

TEST(numbers_an_equation_is_numbered_at_the_right_margin_in_upright_text) {
  const std::string tex = R"(\begin{equation} a = b \end{equation})";
  CHECK_EQ(linesOf(tex), Strings({"(1)"}));
  const auto tag = textRecords(tex);
  // The number ends at the measure: its start and its width (11 a character).
  CHECK(std::fabs(endOf(tag) - 300) < 0.5);
  // Starred, it is not numbered.
  CHECK_EQ(linesOf(R"(\begin{equation*} a = b \end{equation*})"), Strings());
}

TEST(numbers_each_row_of_an_align_is_numbered_and_notag_leaves_one_out) {
  const std::string tex = R"(\begin{align} a &= b \\ c &= d \notag \\ e &= f \end{align})";
  CHECK_EQ(linesOf(tex), Strings({"(1)", "(2)"}));
  std::string nonumber = tex;
  nonumber.replace(nonumber.find("notag"), 5, "nonumber");
  CHECK_EQ(linesOf(nonumber), Strings({"(1)", "(2)"}));
  CHECK_EQ(linesOf(R"(\begin{align*} a &= b \\ c &= d \end{align*})"), Strings());
  // A trailing \\ does not start a numbered row.
  CHECK_EQ(linesOf(R"(\begin{align} a &= b \\ c &= d \\ \end{align})"), Strings({"(1)", "(2)"}));
}

TEST(numbers_gather_numbers_its_rows_and_multline_its_last_line_only) {
  CHECK_EQ(linesOf(R"(\begin{gather} a \\ b \\ c \end{gather})"), Strings({"(1)", "(2)", "(3)"}));
  const std::string tex = R"(\begin{multline} a \\ b \\ c \end{multline})";
  CHECK_EQ(linesOf(tex), Strings({"(1)"}));
  // On the last of its three lines.
  const auto l = rec(tex);
  const auto text = check::of(l, DrawRecord::TEXT);
  const auto glyphs = check::column(check::of(l, DrawRecord::GLYPH), &DrawRecord::y);
  CHECK(std::fabs(text[0].y - check::maximum(glyphs)) <= 1);
}

TEST(numbers_eqnarray_is_numbered_by_rows_too) {
  CHECK_EQ(linesOf(R"(\begin{eqnarray} a &=& b \\ c &=& d \end{eqnarray})"), Strings({"(1)", "(2)"}));
  CHECK_EQ(linesOf(R"(\begin{eqnarray*} a &=& b \end{eqnarray*})"), Strings());
}

TEST(numbers_tag_sets_the_number_and_tag_star_sets_it_without_parentheses) {
  CHECK_EQ(linesOf(R"(\begin{equation} a = b \tag{A} \end{equation})"), Strings({"(A)"}));
  CHECK_EQ(linesOf(R"(\begin{equation} a = b \tag*{A} \end{equation})"), Strings({"A"}));
  // Also in a starred environment, which has no number of its own.
  CHECK_EQ(linesOf(R"(\begin{align*} a &= b \tag{3.1} \end{align*})"), Strings({"(3.1)"}));
  // A tagged row does not use up a number.
  CHECK_EQ(linesOf(R"(\begin{align} a &= b \tag{*} \\ c &= d \end{align})"), Strings({"(*)", "(1)"}));
  // In a display of its own.
  CHECK_EQ(linesOf(R"($$ a = b \tag{7} $$)"), Strings({"(7)"}));
  CHECK_EQ(linesOf(R"(\[ a = b \tag*{7} \])"), Strings({"7"}));
  // A display with nothing but its tag.
  CHECK_EQ(linesOf(R"(\[\tag{1}\])"), Strings({"(1)"}));
}

TEST(numbers_the_count_goes_on_from_one_display_to_the_next) {
  const std::string tex = R"(\begin{equation} a \end{equation})" "\n\nText\n\n" R"(\begin{align} b \\ c \end{align})";
  CHECK_EQ(linesOf(tex), Strings({"(1)", "Text", "(2)", "(3)"}));
  CHECK_EQ(linesOf(R"(\setcounter{equation}{4}\begin{equation} a \end{equation})"), Strings({"(5)"}));
}

TEST(numbers_ref_and_eqref_name_what_a_label_was_written_next_to_forward_or_back) {
  const std::string back = R"(\begin{equation}\label{e1} a = b \end{equation})" "\n\nSee " R"(\eqref{e1} and \ref{e1}.)";
  CHECK(quiet(back));
  CHECK_EQ(linesOf(back), Strings({"(1)", "See(1)and1."}));
  const std::string fwd = "See " R"(\eqref{e2})" "\n\n" R"(\begin{align} a &= b \\ c &= d \label{e2} \end{align})";
  CHECK(quiet(fwd));
  CHECK_EQ(linesOf(fwd), Strings({"See(2)", "(1)", "(2)"}));
  // A label inside the split of an equation is the equation's.
  const std::string spl = R"(\begin{equation}\begin{split} a &= b \label{s} \end{split}\end{equation})" "\n\n" R"(\ref{s})";
  CHECK(quiet(spl));
  CHECK_EQ(linesOf(spl), Strings({"(1)", "1"}));
  // A tag is what its label names.
  CHECK_EQ(linesOf(R"(\begin{equation} a \tag{A.1}\label{t} \end{equation} \ref{t})"), Strings({"(A.1)", "A.1"}));
}

TEST(numbers_an_undefined_reference_still_warns_and_draws_question_marks) {
  CHECK(check::warns(rec(R"(See \ref{nope}.)"), "reference `nope' is undefined"));
  CHECK_EQ(linesOf(R"(See \ref{nope}.)"), Strings({"See??."}));
  CHECK(check::warns(rec(R"(\eqref{nope})"), "drawn as (?" "?)"));
  // \pageref has no page.
  CHECK(check::warns(rec(R"(\pageref{x})"), "undefined"));
  // One that is defined later is not undefined.
  CHECK(quiet(R"(\ref{a} \section{S}\label{a})"));
}

TEST(numbers_a_label_after_a_heading_names_its_number) {
  const std::string tex = R"(\section{A}\label{a})" "\n\n" R"(\section{B}\label{b})" "\n\n" R"(See \ref{a}, \ref{b}.)";
  CHECK(quiet(tex));
  CHECK_EQ(linesOf(tex), Strings({"1A", "2B", "See1,2."}));
}

TEST(numbers_with_no_measure_the_number_follows_the_display_after_a_gap) {
  const auto l = rec(R"(\begin{equation} a = b \end{equation})", InputMode::document, 0);
  const auto tag = check::of(l, DrawRecord::TEXT);
  const auto glyphs = check::of(l, DrawRecord::GLYPH);
  CHECK(check::minimum(check::column(tag, &DrawRecord::x)) > check::maximum(check::column(glyphs, &DrawRecord::x)));
  CHECK(std::fabs(endOf(tag) - static_cast<float>(l.width)) <= 1);
}

TEST(numbers_a_labels_displays_are_numbered_too_as_texs_rule_is_the_same_everywhere) {
  CHECK_EQ(linesOf(R"(Before \begin{align} a \\ b \end{align})", InputMode::mixed), Strings({"Before", "(1)", "(2)"}));
  CHECK_EQ(linesOf(R"(\begin{align*} a \\ b \end{align*})", InputMode::mixed), Strings());
}
