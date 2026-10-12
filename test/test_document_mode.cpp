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
  CHECK(close(at("\\kern100bp " + tex) - at(tex), 50, 1e-4f));
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
  CHECK_EQ(textsOf(f), Strings({"Before", "Cap", "cell", "after."}));
  CHECK_EQ(rows(f), std::size_t(4));
  // In a label a list is part of the line.
  const auto m = runs("Before \\begin{itemize}\\item one\\end{itemize}", InputMode::mixed);
  CHECK(first(xsOf(named(m, "one"))) > static_cast<float>(rec("Before", InputMode::mixed).width));
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
  CHECK(least < 120);
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
    return height("\\rule{" + std::to_string(width) + "bp}{0pt}");
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

TEST(docmode_specialrule_is_a_rule_of_the_thickness_it_names) {
  const auto l = rec("\\begin{tabular}{l}a \\\\ \\specialrule{3pt}{0pt}{0pt} b\\end{tabular}");
  CHECK_EQ(check::texts(l), Strings({"a", "b"}));  // its arguments are read
  const auto lines = check::of(l, DrawRecord::LINE);
  CHECK_EQ(lines.size(), std::size_t(1));
  CHECK(close(lines[0].line_width, 3 * 72 / 72.27f, 1e-4f));
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
    const auto r = check::of(rec(std::string("A \\begin{minipage}[") + pos + "]{100pt}" + longText + "\\end{minipage} B"),
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
  CHECK(std::fabs(w("\\rule{\\columnwidth}{1pt}") - 345 * 72 / 72.27f) <= 1);
}
