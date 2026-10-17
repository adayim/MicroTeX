// Tables: columns, spans and rules, the LaTeX that R's table packages write
// (kable, kableExtra, xtable, gt, tinytable: test/fixtures/tables holds what
// they emit), and tabularray's tblr (engine.h).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

#ifndef MICROTEX_TEST_FIXTURES
#error "MICROTEX_TEST_FIXTURES must name the directory holding the fixtures"
#endif

using check::Options;
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

check::Layout rec(const std::string& tex, float width = 300, float size = 10, InputMode mode = InputMode::document) {
  return check::render(tex, typeface(mode, width, size));
}

bool quiet(const std::string& tex, float width = 300, float size = 10, InputMode mode = InputMode::document) {
  return rec(tex, width, size, mode).warnings.empty();
}

// The horizontal extent of the rules of a layout.
float ruleSpan(const std::string& tex, float width = 300) {
  float lo = 1e9f, hi = -1e9f;
  for (const auto& l : check::of(rec(tex, width), DrawRecord::LINE)) {
    lo = std::min({lo, l.x1, l.x2});
    hi = std::max({hi, l.x1, l.x2});
  }
  return hi - lo;
}

// The text of a layout, line by line, top to bottom.
Strings linesOf(const std::string& tex, float width = 300) { return check::lines(rec(tex, width)); }

std::string slurp(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

// --- columns and spans ------------------------------------------------------

TEST(tables_a_cline_past_the_last_column_stays_inside_the_table) {
  // \cline{2-2} on a one-column table clamps to the last column.
  const auto l = check::render("\\begin{array}{l}a\\\\ \\cline{2-2} b\\end{array}");
  const auto rules = check::of(l, DrawRecord::LINE);
  CHECK_EQ(rules.size(), std::size_t(1));
  CHECK(rules[0].x2 <= static_cast<float>(l.width) + 0.01f);
}

TEST(tables_an_empty_multirow_and_numbers_past_any_range_draw_not_crash) {
  // Found by the engine fuzzer: an empty \multirow cell, and numbers past
  // an int's range (\cline, \multirow's count).
  const auto glyphs = [](const std::string& tex) {
    return check::count(check::render(tex, typeface(InputMode::math)), DrawRecord::GLYPH);
  };
  CHECK_EQ(glyphs("\\begin{array}{ll}\\multirow{2}{*}{} & b \\\\ c & d\\end{array}"), 3);
  CHECK_EQ(glyphs("\\begin{array}{ll}\\multirow{99999999999}{*}{x} & b \\\\ c & d\\end{array}"), 4);
  CHECK_EQ(check::count(check::render("\\begin{array}{ll} a & b \\\\ \\cline{1e400-2} c & d\\end{array}"), DrawRecord::LINE), 1);
}

TEST(tables_p_wraps_a_cell_to_a_fixed_measure) {
  const std::string longText = "\\text{the quick brown fox jumps over the lazy dog again and again}";
  const auto dims = [&](const std::string& spec) {
    check::Options opt;
    opt.textSize = 16;
    return check::render("\\begin{tabular}{" + spec + "}" + longText + "\\end{tabular}", opt);
  };
  const auto free = dims("l");
  const auto fixed = dims("p{3cm}");
  // Narrower, and taller because it now occupies several lines.
  CHECK(fixed.width < free.width / 2);
  CHECK(fixed.height > free.height * 2);
  // The width tracks the request rather than the content.
  const int w2 = dims("p{2cm}").width, w4 = dims("p{4cm}").width, w6 = dims("p{6cm}").width;
  CHECK(w2 < w4);
  CHECK(w4 < w6);
  // Equal steps in the request give equal steps in the result.
  CHECK(std::abs((w4 - w2) - (w6 - w4)) <= 2);
  // m{} and b{} differ from p{} only in vertical alignment, which the row
  // builder already handles, so they parse to the same measure.
  CHECK_EQ(dims("m{3cm}").width, dims("p{3cm}").width);
  CHECK_EQ(dims("b{3cm}").width, dims("p{3cm}").width);
}

TEST(tables_a_malformed_p_width_leaves_the_column_content_sized) {
  check::Options opt;
  opt.textSize = 16;
  const int ref = check::render("\\begin{tabular}{l}\\text{a}\\end{tabular}", opt).width;
  for (const char* spec : {"p{}", "p{nonsense}", "p", "p{0pt}"}) {
    CHECK_EQ(check::render(std::string("\\begin{tabular}{") + spec + "}\\text{a}\\end{tabular}", opt).width, ref);
  }
}

TEST(tables_p_does_not_disturb_ordinary_tables_or_leak_between_parses) {
  check::Options opt;
  opt.textSize = 16;
  const auto plain = [&] {
    return check::render("\\begin{tabular}{|c|c|}\\hline \\text{a}&\\text{b}\\\\\\hline\\end{tabular}", opt).width;
  };
  const int before = plain();
  check::render("\\begin{tabular}{p{3cm}}\\text{wrap me please}\\end{tabular}", opt);
  // A fixed width must not survive into the next parse.
  CHECK_EQ(plain(), before);
}

TEST(tables_a_tabular_met_in_text_has_text_cells_as_in_latex) {
  const std::string tab = "\\begin{tabular}{lr} Term & Estimate \\\\ \\hline Intercept & $\\beta_1$ \\end{tabular}";
  const Options mixed(InputMode::mixed);
  // A cell's words are one run of text, the spaces at either end of it
  // dropped; its math is math, between $...$.
  CHECK_EQ(check::texts(check::render(tab, mixed)), Strings({"Term", "Estimate", "Intercept"}));
  CHECK(check::count(check::render(tab, typeface(InputMode::mixed)), DrawRecord::GLYPH) > 0);
  CHECK_EQ(check::texts(check::render(tab, Options(InputMode::document))), Strings({"Term", "Estimate", "Intercept"}));
  // What \multicolumn spans is a cell too.
  CHECK_EQ(check::texts(check::render("\\begin{tabular}{ll}\\multicolumn{2}{c}{Both columns}\\end{tabular}", mixed)),
           Strings({"Both columns"}));
  // Met in math, a tabular is an array, as it always was.
  CHECK_EQ(check::texts(check::render("\\begin{tabular}{l}ab\\end{tabular}")).size(), std::size_t(0));
}

TEST(tables_a_multirow_reaching_past_the_tables_edge_ends_there) {
  // Its span was written past the row heights (a crash).
  const auto glyphs = [](const std::string& tex) {
    return check::count(check::render(tex, typeface(InputMode::math)), DrawRecord::GLYPH);
  };
  CHECK_EQ(glyphs("\\begin{array}{cc} a & b \\\\ \\multirow{3}{*}{\\frac{x}{y}} & c \\end{array}"), 5);
  CHECK_EQ(glyphs("\\begin{array}{cc} \\multirow{-3}{*}{\\frac{x}{y}} & c \\end{array}"), 3);
}

TEST(tables_a_column_type_defined_in_terms_of_itself_or_a_count_past_any_table_ends) {
  CHECK(check::warns(check::render("\\newcolumntype{Y}{Y}\\begin{array}{Y}a\\end{array}"), "expands without end"));
  CHECK(check::warns(check::render("\\begin{array}{*{9999999999}{c}}a\\end{array}"), "expands without end"));
  CHECK(check::render("\\begin{array}{cc}\\hdotsfor{10000000}\\\\a&b\\end{array}").records.size() < 100000);
}

TEST(tables_multicolumn_1_spans_its_own_column_only) {
  const auto r = check::of(rec("\\begin{tabular}{ll}\\multicolumn{1}{c}{Head} & Two \\\\ aaaa & bbbb\\end{tabular}", 0,
                               20, InputMode::mixed),
                           DrawRecord::TEXT);
  float two = -1, bbbb = -2;
  for (const auto& t : r) {
    if (t.text == "Two") two = t.x;
    if (t.text == "bbbb") bbbb = t.x;
  }
  CHECK_EQ(two, bbbb);
}

TEST(tables_a_tabulars_position_is_not_its_columns_and_tabular_star_drops_its_width) {
  // One row: [t] puts its baseline on the text's, the centred one on the axis, which is 0.01em from it with TeX's
  // strut (0.7 and 0.3 of 1.2em about an axis of 0.25em).
  const auto top = rec("\\begin{tabular}[t]{ll} a & b \\end{tabular}", 0, 20, InputMode::mixed);
  const auto mid = rec("\\begin{tabular}{ll} a & b \\end{tabular}", 0, 20, InputMode::mixed);
  CHECK_EQ(check::texts(top), check::texts(mid));
  CHECK(std::fabs(check::of(top, microtex::DrawRecord::TEXT).front().y - check::of(mid, microtex::DrawRecord::TEXT).front().y) <
        0.05f * 20);
  const auto l = rec("\\begin{tabular*}{\\textwidth}{@{\\extracolsep{\\fill}}ll}a&b\\end{tabular*}", 0, 20, InputMode::mixed);
  CHECK(l.warnings.empty());
  CHECK_EQ(check::texts(l), Strings({"a", "b"}));
}

// --- the LaTeX that R's table packages write ----------------------------------

TEST(tables_every_table_the_packages_write_is_read_without_a_warning) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MICROTEX_TEST_FIXTURES) / "tables")) {
    if (entry.path().extension() == ".tex") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  CHECK(files.size() > 25);
  for (const auto& f : files) {
    const auto l = rec(slurp(f));
    for (const auto& w : l.warnings) check::fail(__FILE__, __LINE__, f.filename().string() + ": " + w);
    // Something is drawn, and not much wider than the measure it was given
    // (a table at the start of a paragraph is indented, as in LaTeX).
    CHECK(l.records.size() > 5);
    CHECK(l.width <= 300 + 20);
  }
}

TEST(tables_a_tables_cells_are_where_its_rows_and_columns_put_them) {
  // kable(): three rows of four cells, a header, and rules.
  const auto l = rec(slurp(std::filesystem::path(MICROTEX_TEST_FIXTURES) / "tables" / "kable-booktabs.tex"));
  const auto words = check::texts(l);
  for (const char* want : {"Mazda", "Datsun", "Hornet"}) {
    CHECK(std::find_if(words.begin(), words.end(), [&](const auto& w) { return w.find(want) != std::string::npos; }) !=
          words.end());
  }
  // Rules above and below the header and below the body.
  CHECK_EQ(check::count(l, DrawRecord::LINE), 3);
}

TEST(tables_addlinespace_leaves_the_space_it_is_given_and_half_an_em_without) {
  const auto h = [](const std::string& sep) {
    return rec("\\begin{tabular}{l}a\\\\" + sep + " b\\end{tabular}", 0, 20).height;
  };
  CHECK(std::abs((h("\\addlinespace[10pt]") - h("")) - 10) <= 1.5);
  CHECK(std::abs((h("\\addlinespace") - h("")) - 10) <= 1.5);
  CHECK(quiet("\\begin{tabular}{l}a\\\\\\addlinespace b\\end{tabular}"));
}

TEST(tables_fontsize_sets_the_size_over_10pt_to_the_end_of_its_group) {
  const auto fs = [](const std::string& tex) {
    return check::uniqueSorted(check::column(check::ink(rec(tex, 0, 10, InputMode::mixed)), &DrawRecord::font_size), 3);
  };
  CHECK(fs("{\\fontsize{20}{24}\\selectfont a}") == std::vector<double>({20}));
  CHECK(fs("a{\\fontsize{20}{24}\\selectfont b}c") == std::vector<double>({10, 20}));
  // kableExtra's group, without braces.
  CHECK(fs("a\\begingroup\\fontsize{20}{24}\\selectfont b\\endgroup c") == std::vector<double>({10, 20}));
  // gt writes the size as a length.
  CHECK(fs("{\\fontsize{20.0pt}{24.0pt}\\selectfont a}") == std::vector<double>({20}));
}

TEST(tables_a_table_given_the_line_width_stays_that_wide_inside_a_size_declaration) {
  CHECK(std::fabs(ruleSpan("\\fontsize{20}{24}\\selectfont\\begin{tabular*}{\\linewidth}{@{\\extracolsep{\\fill}}lr}\\hline a & b\\\\\\hline\\end{tabular*}") - 300) <= 0.5f);
  CHECK(std::fabs(ruleSpan("{\\large\\begin{tabularx}{\\linewidth}{XX}\\hline a & b\\\\\\hline\\end{tabularx}}") - 300) <= 0.5f);
}

TEST(tables_tabular_star_spreads_its_columns_across_its_width_tabularx_its_x_columns) {
  const std::string star = "\\begin{tabular*}{\\linewidth}{@{\\extracolsep{\\fill}}lr}\\hline a & b\\\\\\hline\\end{tabular*}";
  CHECK(std::fabs(ruleSpan(star, 300) - 300) <= 0.5f);
  // Without a measure it is as wide as it is.
  const std::string natural = "\\begin{tabular}{lr}\\hline a & b\\\\\\hline\\end{tabular}";
  // (The star form has no padding at its left edge, where the plain one has.)
  const float starWidth = ruleSpan(star, 0), naturalWidth = ruleSpan(natural, 0);
  CHECK(starWidth <= naturalWidth && naturalWidth - starWidth <= 0.2f * naturalWidth);
  CHECK(std::fabs(ruleSpan("\\begin{tabularx}{\\linewidth}{lX}\\hline a & b\\\\\\hline\\end{tabularx}", 300) - 300) <= 0.5f);
  // An X column is as wide as its share, and what is in it is set at the right.
  const auto r = check::of(
    rec("\\begin{tabularx}{\\linewidth}{>{\\raggedleft\\arraybackslash}X}\\hline a\\\\\\hline\\end{tabularx}"),
    DrawRecord::TEXT);
  CHECK(r.size() == 1);
  CHECK(r[0].x > 250);
}

TEST(tables_a_longtable_sets_its_head_and_foot_once_and_its_caption_above) {
  const std::string tex =
    "\\begin{longtable}{lr}\n\\caption{Cap}\\\\\n\\hline A & B \\endfirsthead\n\\hline C & D \\endhead\n"
    "\\hline E & F \\endfoot\n\\hline G & H \\endlastfoot\n1 & 2 \\\\ 3 & 4 \\\\\n\\end{longtable}";
  CHECK_EQ(linesOf(tex), Strings({"Cap", "AB", "12", "34", "GH"}));
  // No first head, no last foot: the head and the foot.
  const std::string plain = "\\begin{longtable}{lr}\\hline C & D \\endhead\\hline E & F \\endfoot 1 & 2 \\\\ 3 & 4 \\\\\\end{longtable}";
  CHECK_EQ(linesOf(plain), Strings({"CD", "12", "34", "EF"}));
  CHECK(quiet(plain));
  // The caption is centred across the columns.
  const auto r = check::of(rec("\\begin{longtable}{lr}\\caption{A wide caption here}\\\\ a & b\\end{longtable}"), DrawRecord::TEXT);
  const float top = check::minimum(check::column(r, &DrawRecord::y));
  float capX = 1e9f, restX = 1e9f;
  for (const auto& t : r) {
    if (t.y == top) capX = std::min(capX, t.x);
    else restX = std::min(restX, t.x);
  }
  CHECK(capX < restX + 1);
}

TEST(tables_a_longtable_whose_columns_are_repeated_with_a_group_missing_is_recovered_from) {
  // *{n}{cols} without its groups once threw a std::out_of_range from the
  // column count, and the table was drawn as its name with that for a reason.
  for (const char* spec : {"*", "*{3}", "*{3}{c", "*{99999}{*{99999}{*{99999}{*{99999}{c}}}}"}) {
    const std::string tex = std::string("\\begin{longtable}{") + spec + "}\\caption{Cap}\\\\ a \\end{longtable}";
    for (const auto& w : rec(tex).warnings) {
      CHECK(w.find("basic_string") == std::string::npos && w.find("substr") == std::string::npos &&
            w.find("out_of_range") == std::string::npos);
    }
  }
  // With one group missing it is a table of one column, and it is drawn.
  for (const char* spec : {"*", "*{3}"}) {
    const auto lines = linesOf(std::string("\\begin{longtable}{") + spec + "}\\caption{Cap}\\\\ a \\end{longtable}");
    CHECK(std::find(lines.begin(), lines.end(), "Cap") != lines.end());
  }
}

TEST(tables_makecell_hhline_and_the_font_switches_are_read) {
  CHECK_EQ(linesOf("\\begin{tabular}{l}\\makecell{a\\\\b}\\end{tabular}"), Strings({"a", "b"}));
  CHECK(quiet("\\begin{tabular}{ll}a & b\\\\\\hhline{--} c & d\\end{tabular}"));
  // The weight and slant it is set in; the roman bit \textbf adds is no part of it.
  const auto style = [](const std::string& tex) {
    std::set<int> out;
    for (const auto& r : rec(tex, 0, 10, InputMode::mixed).records) out.insert(r.font_style & ~1);
    return out;
  };
  CHECK(style("{\\bfseries a}") == style("\\textbf{a}"));
  CHECK(style("{\\itshape a}") == style("\\textit{a}"));
  CHECK(style("{\\ttfamily a}") == style("\\texttt{a}"));
}

TEST(tables_a_columns_font_switch_and_suffix_apply_to_its_cells) {
  const auto fonts = [](const std::string& spec) {
    return check::column(check::of(rec("\\begin{tabular}{" + spec + "}a & b\\end{tabular}"), DrawRecord::TEXT), &DrawRecord::font_style);
  };
  const auto plainFonts = fonts("lr");
  CHECK_EQ(std::set<int>(plainFonts.begin(), plainFonts.end()).size(), std::size_t(1));
  const auto bold = fonts(">{\\bfseries}lr");
  CHECK_EQ(bold.size(), std::size_t(2));
  CHECK(bold[0] != bold[1]);
  // <{...} follows each cell of its column.
  CHECK(rec("\\begin{tabular}{l<{x}}a\\end{tabular}", 0).width > rec("\\begin{tabular}{l}a\\end{tabular}", 0).width);
  CHECK(quiet("\\begin{tabular}{l!{|}r}a & b\\end{tabular}"));
}

TEST(tables_a_colour_defined_with_capitals_is_found_by_the_name_it_was_given) {
  CHECK(check::drawsColour(rec("\\definecolor{cFFFF00}{HTML}{FFFF00}\\colorbox{cFFFF00}{x}", 300, 10, InputMode::mixed),
                           0xFFFFFF00u));
}

TEST(tables_caption_star_is_a_caption_with_no_star) {
  CHECK_EQ(linesOf("\\begin{table}\\caption*{Cap}\\begin{tabular}{l}a\\end{tabular}\\end{table}"), Strings({"Cap", "a"}));
  // ... and the numbered one has its "Table 1: ".
  CHECK_EQ(linesOf("\\begin{table}\\caption{Cap}\\begin{tabular}{l}a\\end{tabular}\\end{table}"), Strings({"Table1:Cap", "a"}));
}

// --- tabularray, as tinytable writes it ---------------------------------------

namespace {

std::string tblr(const std::string& spec, const std::string& body, const std::string& outer = "") {
  return std::string("\\begin{tblr}") + (outer.empty() ? "" : "[" + outer + "]") + "{" + spec + "}\n" + body + "\\end{tblr}";
}

}  // namespace

TEST(tblr_is_a_table_of_the_columns_its_spec_names) {
  const std::string tex = tblr("colspec={Q[]Q[r]}", "a & b \\\\ c & d \\\\");
  CHECK(quiet(tex));
  CHECK_EQ(linesOf(tex), Strings({"ab", "cd"}));
  // The second column is right-aligned: its cells end together (a character
  // is 5.5 wide at this size).
  float endB = -1, endD = -2;
  for (const auto& t : check::of(rec(tex), DrawRecord::TEXT)) {
    if (t.text == "b") endB = t.x + 5.5f;
    if (t.text == "d") endD = t.x + 5.5f;
  }
  CHECK(std::fabs(endB - endD) <= 0.5f);
}

TEST(tblr_hlines_and_vlines_are_its_rules) {
  const std::string body = "a & b \\\\ c & d \\\\";
  const auto lines = [&](const std::string& spec) {
    return check::of(rec(tblr("colspec={Q[]Q[]}," + spec, body)), DrawRecord::LINE);
  };
  CHECK_EQ(lines("").size(), std::size_t(0));
  // Above the first row, below the first, and below the last.
  CHECK_EQ(lines("hline{1,2,3}={0.05em}").size(), std::size_t(3));
  CHECK_EQ(lines("hlines={}").size(), std::size_t(3));
  // One across two columns only.
  const auto one = lines("hline{2}={1}{solid}");
  CHECK_EQ(one.size(), std::size_t(1));
  const auto full = lines("hline{2}={solid}");
  CHECK_EQ(full.size(), std::size_t(1));
  CHECK(one[0].x2 - one[0].x1 < full[0].x2 - full[0].x1);
  // Vertical rules between and around the columns.
  CHECK_EQ(lines("vlines={}").size(), std::size_t(3 * 2));
  // Thickness is taken from the rule.
  CHECK(lines("hline{1}={0.2em}")[0].line_width > lines("hline{1}={0.01em}")[0].line_width);
}

TEST(tblr_cells_take_the_colours_fonts_alignment_and_spans_it_sets) {
  const std::string tex = tblr(
    "colspec={Q[]Q[]Q[]},cell{1}{2}={c=2}{halign=c},row{2}={}{font=\\bfseries, bg=red},column{1}={}{fg=blue}",
    "Head & Span & \\\\ x & y & z \\\\");
  CHECK(quiet(tex));
  const auto l = rec(tex);
  // The spanned cell sits across the last two columns, centred.
  float span = -1e9f, y = -1e9f;
  for (const auto& t : check::of(l, DrawRecord::TEXT)) {
    if (t.text == "Span") span = t.x;
    if (t.text == "y") y = t.x;
  }
  CHECK(std::fabs((span + 8) - (y + 3)) <= 15);
  // The second row is bold, on red.
  std::set<int> styles;
  for (const auto& r : check::of(l, DrawRecord::TEXT)) styles.insert(r.font_style);
  CHECK(styles.size() > 1);
  CHECK(check::count(l, DrawRecord::FILL_RECT) > 0 || check::drawsColour(l, check::kRed));
  // A first column in blue.
  CHECK(check::drawsColour(l, 0xFF0000FFu));
}

TEST(tblr_a_talltblr_has_its_caption_above_it_and_what_is_not_understood_is_said) {
  const std::string tex = "\\begin{talltblr}[caption={Cars}]{colspec={Q[]Q[]}}\na & b \\\\\n\\end{talltblr}";
  CHECK_EQ(linesOf(tex)[0], std::string("Cars"));
  const auto w = rec(tblr("colspec={Q[]}, rowsep=3pt, cell{1}{1}={r=2}{}", "a \\\\ b \\\\"));
  CHECK(check::warns(w, "rowsep"));
  CHECK(check::warns(w, "row span"));
}

// kableExtra's striped rows are \rowcolor{gray!10}: pdflatex fills them with
// `.95 g`, 242 of 255. They were black.
TEST(tables_a_mixed_row_colour_fills_the_row_in_that_colour) {
  const auto l = rec("\\begin{tabular}{l}\\rowcolor{gray!10} a\\end{tabular}");
  CHECK(check::drawsColour(l, 0xFFF2F2F2u));
  for (const auto& r : check::of(l, DrawRecord::FILL_RECT)) CHECK(r.col != 0xFF000000u);
  const auto bad = rec("\\begin{tabular}{l}\\rowcolor{nosuchcolour} a\\end{tabular}");
  CHECK(check::warns(bad, "undefined colour `nosuchcolour'"));
  CHECK(check::of(bad, DrawRecord::FILL_RECT).empty());
}

namespace {
bool near(float a, float b) { return std::fabs(a - b) <= 1e-3f; }

std::vector<float> textXs(const std::string& tex) {
  return check::column(check::of(rec(tex, 300, 10), DrawRecord::TEXT), &DrawRecord::x);
}
}  // namespace

// pdflatex, tabular{ll} with a and b: the second column's text starts
// width(a) + 2*\tabcolsep after the first's, and the first starts \tabcolsep
// in: 12pt and 6pt at 10pt; the engine had 1em (10pt) for both. The stub
// measurer's "a" is 0.55em, 5.5 at a size of 10.
TEST(tables_a_tabular_pads_its_columns_by_tabcolsep) {
  const auto plain = textXs("a");
  const auto t = textXs("\\begin{tabular}{ll}a&b\\end{tabular}");
  CHECK_EQ(t.size(), std::size_t(2));
  CHECK(near(t[0] - plain[0], 6.f));
  CHECK(near(t[1] - t[0], 5.5f + 12.f));
  const auto wide = textXs("\\setlength{\\tabcolsep}{1em}\\begin{tabular}{ll}a&b\\end{tabular}");
  CHECK(near(wide[0] - plain[0], 10.f));
  CHECK(near(wide[1] - wide[0], 5.5f + 20.f));
  // The group ends it.
  const auto after = textXs("{\\setlength{\\tabcolsep}{1em}}\\begin{tabular}{ll}a&b\\end{tabular}");
  CHECK(near(after[1] - after[0], 5.5f + 12.f));
}

// A math array is 2\arraycolsep apart (10pt: it already was) and \arraycolsep at the edges.
TEST(tables_an_array_pads_its_columns_by_arraycolsep) {
  const auto glyphXs = [](const std::string& tex) {
    return check::column(check::of(rec(tex, 300, 10, InputMode::math), DrawRecord::GLYPH), &DrawRecord::x);
  };
  const auto d = glyphXs("\\begin{array}{ll}a&b\\end{array}");
  const auto w = glyphXs("\\setlength{\\arraycolsep}{1em}\\begin{array}{ll}a&b\\end{array}");
  CHECK_EQ(d.size(), std::size_t(2));
  CHECK_EQ(w.size(), std::size_t(2));
  CHECK(near((w[1] - w[0]) - (d[1] - d[0]), 10.f));  // 2em apart against 1em
}

// kableExtra's footnote(threeparttable = TRUE) writes tablenotes with \item[a]:
// threeparttable sets a list with each label a superscript in a box of 0.5em,
// the text 1.5em in (pdflatex, in points: the label at 141, the text at 148,
// the left edge at 133).
TEST(tables_tablenotes_are_a_list_of_superscript_labelled_notes) {
  const std::string tex =
    "\\begin{threeparttable}\\begin{tabular}{l}x\\end{tabular}"
    "\\begin{tablenotes}\\item[a] First note\\item Second note\\item[b] Third note\\end{tablenotes}"
    "\\end{threeparttable}";
  const auto l = rec(tex, 300, 10);
  CHECK(l.warnings.empty());
  const auto at = [&](const std::string& word) {
    for (const auto& r : check::of(l, DrawRecord::TEXT)) {
      if (r.text == word) return r;
    }
    return DrawRecord();
  };
  // One column of text, 1.5em in at this size of 10, whether or not a note has a label...
  CHECK(near(at("First").x, 15.f));
  CHECK(near(at("Second").x, 15.f));
  CHECK(near(at("Third").x, 15.f));
  // ...and the labels in a column of their own, 0.8em in, raised.
  CHECK(near(at("a").x, 8.f));
  CHECK(near(at("b").x, 8.f));
  CHECK(at("a").y < at("First").y);
  // The notes stand under the table, in order.
  CHECK(at("x").y < at("First").y);
  CHECK(at("First").y < at("Second").y);
  CHECK(at("Second").y < at("Third").y);
}

// [para] runs the notes on in one paragraph, each with its label; a \small
// before the first item sets the notes small, as it does in LaTeX.
TEST(tables_tablenotes_para_runs_on_and_a_leading_small_sets_them) {
  const auto para = rec("\\begin{tablenotes}[para]\\item[a] One\\item[b] Two\\end{tablenotes}", 300, 10);
  CHECK(para.warnings.empty());
  // The text of both notes is on one baseline; the labels stand above it.
  const auto t = check::of(para, DrawRecord::TEXT);
  CHECK_EQ(t.size(), std::size_t(4));
  CHECK_EQ(t[1].text, std::string("One"));
  CHECK_EQ(t[3].text, std::string("Two"));
  CHECK(near(t[1].y, t[3].y));
  CHECK(t[1].x < t[3].x);
  CHECK(t[0].y < t[1].y);
  const auto plain = check::of(rec("\\begin{tablenotes}\\item Note\\end{tablenotes}", 300, 10), DrawRecord::TEXT);
  const auto small = check::of(rec("\\begin{tablenotes}\\small\\item Note\\end{tablenotes}", 300, 10), DrawRecord::TEXT);
  CHECK_EQ(plain.size(), std::size_t(1));
  CHECK_EQ(small.size(), std::size_t(1));
  CHECK(small[0].font_size < plain[0].font_size);
}

// tabular[t] puts the baseline on the first row's, [b] on the last row's, and
// none on the middle (the math axis), so "a" beside a two-row tabular is level
// with its first row, its last row, or between the two.
TEST(tables_a_tabular_with_a_position_sets_its_baseline_by_a_row) {
  const auto ys = [](const std::string& pos) {
    const auto l = rec("a \\begin{tabular}" + pos + "{l}x\\\\y\\end{tabular}", 300, 10);
    std::map<std::string, float> y;
    for (const auto& r : check::of(l, DrawRecord::TEXT)) y[r.text] = r.y;
    return y;
  };
  const auto top = ys("[t]");
  const auto bottom = ys("[b]");
  const auto middle = ys("");
  CHECK(near(top.at("a"), top.at("x")));
  CHECK(near(bottom.at("a"), bottom.at("y")));
  CHECK(middle.at("a") > middle.at("x"));
  CHECK(middle.at("a") < middle.at("y"));
}

// makecell's [vh]: the vertical letter (t, c or b) and the horizontal one (l,
// c or r), either alone or both -- makecell.sty's \mcell@tabular -- a tabular
// of one column set that way.
TEST(tables_makecell_takes_a_vertical_and_a_horizontal_letter) {
  const auto same = [](const std::string& cell, const std::string& tabular) {
    return check::sameInk(rec("\\begin{tabular}{l}" + cell + "\\end{tabular}", 300, 10),
                          rec("\\begin{tabular}{l}" + tabular + "\\end{tabular}", 300, 10));
  };
  CHECK(same("\\makecell[tl]{a\\\\bb}", "\\begin{tabular}[t]{l}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[br]{a\\\\bb}", "\\begin{tabular}[b]{r}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[cl]{a\\\\bb}", "\\begin{tabular}[c]{l}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[r]{a\\\\bb}", "\\begin{tabular}[c]{r}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[t]{a\\\\bb}", "\\begin{tabular}[t]{c}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell{a\\\\bb}", "\\begin{tabular}[c]{c}a\\\\bb\\end{tabular}"));
  CHECK(rec("\\begin{tabular}{l}\\makecell[tl]{a\\\\b}\\end{tabular}", 300, 10).warnings.empty());
  // \thead is a bold \makecell.
  CHECK(same("\\thead[l]{a\\\\bb}", "\\makecell[l]{\\bfseries a\\\\bb}"));
}

namespace {
// Where the decimal marker of each row's number stands in `\begin{tabular}{spec}`
// of rows `a & cell`. The cells are drawn a character at a time, 5.5 wide at a
// size of 10; a number with no marker has it where it would be, after its
// last character.
struct Markers {
  std::vector<float> x;
  Strings warnings;
};

Markers markersOf(const std::string& spec, const std::vector<std::string>& cells) {
  std::string tex = "\\begin{tabular}{" + spec + "}";
  for (const auto& cell : cells) tex += "a&" + cell + "\\\\";
  tex += "\\end{tabular}";
  const auto l = rec(tex, 300, 10);
  std::map<int, std::vector<DrawRecord>> rows;
  for (const auto& r : check::of(l, DrawRecord::TEXT)) {
    if (r.text != "a") rows[static_cast<int>(std::lround(r.y))].push_back(r);
  }
  Markers found;
  found.warnings = l.warnings;
  for (const auto& [y, row] : rows) {
    float marker = -1.f, end = 0.f;
    for (const auto& r : row) {
      if (r.text == "." || r.text == ",") marker = r.x;
      end = std::max(end, r.x + 5.5f);
    }
    found.x.push_back(marker >= 0.f ? marker : end);
  }
  return found;
}
}  // namespace

// siunitx's S column sets its numbers with their decimal markers in a column,
// as pdflatex does: the digits before it to the left, those after to the right.
TEST(tables_a_siunitx_s_column_lines_up_the_decimal_markers) {
  const auto plain = markersOf("lS", {"1.5", "12.25", "3", "-0.125"});
  CHECK(plain.warnings.empty());
  CHECK_EQ(plain.x.size(), std::size_t(4));
  for (const float x : plain.x) CHECK(near(x, plain.x.at(0)));
  // Its options are for siunitx: the column is read all the same.
  const auto options = markersOf("lS[table-format=2.3]", {"1.5", "12.25"});
  CHECK(options.warnings.empty());
  CHECK_EQ(options.x.size(), std::size_t(2));
  CHECK(near(options.x.at(0), options.x.at(1)));
  // A comma is a decimal marker too.
  const auto comma = markersOf("lS", {"1,5", "12,25"});
  CHECK(comma.warnings.empty());
  CHECK_EQ(comma.x.size(), std::size_t(2));
  CHECK(near(comma.x.at(0), comma.x.at(1)));
}

// The other text tables read it too.
TEST(tables_the_other_text_tables_pad_by_tabcolsep) {
  const auto gap = [](const std::string& tex) {
    const auto xs = textXs(tex);
    return xs.at(1) - xs.at(0);
  };
  const float base = gap("\\begin{tabular}{ll}a&b\\end{tabular}");
  CHECK(near(gap("\\begin{longtable}{ll}a&b\\end{longtable}"), base));
  CHECK(near(gap("\\setlength{\\tabcolsep}{0em}\\begin{longtable}{ll}a&b\\end{longtable}"), base - 12.f));
}
