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
    // (the test font's letters are 0.55em wide: stargazer's tables, which LaTeX sets about 330pt, are wider than the measure)
    if (l.width > 1.2f * 300) check::fail(__FILE__, __LINE__, f.filename().string() + " is " + std::to_string(l.width) + " wide");
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
  CHECK(std::abs((h("\\addlinespace[10pt]") - h("")) - 20) <= 1.5);  // 10pt at 20px
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

// save_tt()'s documented preamble defines the two commands tinytable uses as `cmd=` values. tabularray keeps such a
// value unexpanded; expanding it read `cmd=\underline{}` and an extra `}`.
TEST(tblr_the_preamble_tinytable_documents_does_not_expand_cmd_values) {
  const std::string preamble =
    "\\newcommand{\\tinytableTabularrayUnderline}[1]{\\underline{#1}}\n"
    "\\newcommand{\\tinytableTabularrayStrikeout}[1]{\\sout{#1}}\n";
  const std::string table =
    "\\begin{tblr}[   %% outer open\n]{ %% inner open\ncolspec={Q[]Q[]},\nrow{2}={}{cmd=\\tinytableTabularrayUnderline},\n"
    "row{3}={}{cmd=\\tinytableTabularrayUnderline\\tinytableTabularrayStrikeout, font=\\bfseries},\n}\n"
    "x & y \\\\\nu & v \\\\\np & q \\\\\n\\end{tblr}";
  const auto drawn = rec(preamble + table, 300, 10, InputMode::document);
  CHECK(drawn.warnings.empty());
  CHECK(!check::of(drawn, DrawRecord::FILL_RECT).empty() || !check::of(drawn, DrawRecord::LINE).empty());
  // Called with an argument, as a user's own cell, it is still the macro.
  CHECK(quiet(preamble + "\\begin{tabular}{l}\\tinytableTabularrayUnderline{under}\\end{tabular}"));
}

// \@makecaption centres a caption that fits the line, so one written before \centering (what tinytable's
// tabular theme writes) stands where one after it does.
TEST(tables_a_caption_in_a_float_is_centred_whether_or_not_centering_comes_first) {
  const std::string table = "\\begin{tabular}{l}x\\end{tabular}\n\\end{table}";
  const auto before = check::of(
    rec("\\begin{table}\n\\caption{Cap}\n\\centering\n" + table, 300, 10, InputMode::document), DrawRecord::TEXT);
  const auto after = check::of(
    rec("\\begin{table}\n\\centering\n\\caption{Cap}\n" + table, 300, 10, InputMode::document), DrawRecord::TEXT);
  CHECK(!before.empty() && !after.empty());
  CHECK(before[0].x > 100);
  CHECK(std::fabs(before[0].x - after[0].x) <= 1e-3f);
}

TEST(tables_a_longtable_sets_its_head_and_foot_once_and_its_caption_above) {
  const std::string tex =
    "\\begin{longtable}{lr}\n\\caption{Cap}\\\\\n\\hline A & B \\endfirsthead\n\\hline C & D \\endhead\n"
    "\\hline E & F \\endfoot\n\\hline G & H \\endlastfoot\n1 & 2 \\\\ 3 & 4 \\\\\n\\end{longtable}";
  CHECK_EQ(linesOf(tex), Strings({"Table1:Cap", "AB", "12", "34", "GH"}));
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
    CHECK(std::find(lines.begin(), lines.end(), "Table1:Cap") != lines.end());
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
  const std::string tex =
    "\\begin{talltblr}[caption={Cars}]{colspec={Q[]Q[]}}\naaaaaaaaaa & bbbbbbbbbb \\\\\n\\end{talltblr}";
  CHECK_EQ(linesOf(tex)[0], std::string("Table1:Cars"));  // as tabularray's caption-tag sets it
  // A caption wider than the table wraps at the table's width instead of widening it.
  const auto narrow = linesOf("\\begin{talltblr}[caption={Cars of the year}]{colspec={Q[]}}\na \\\\\n\\end{talltblr}");
  CHECK(narrow.size() > 2u);
  // The cells of a centred column span one column and were left out of the table's width when the caption was wrapped to it,
  // so a caption that fits wrapped after "Table" (LuaLaTeX: 34.06pt high with one line, ours 34.13; wrapped it would be 46).
  const std::string cells = "aaaaaaaaaaaaaaa & bbbbbbbbbbbbbbb \\\\\n\\end{talltblr}";
  CHECK_EQ(linesOf("\\begin{talltblr}[caption={Cars of}]{colspec={Q[]Q[]}, column{2}={}{halign=c}}\n" + cells).size(),
           linesOf("\\begin{talltblr}[caption={Cars of}]{colspec={Q[]Q[]}}\n" + cells).size());
  // A word that is wider than the table cannot be broken (LaTeX would hyphenate one it can): said, and not when the words fit.
  CHECK(check::warns(rec("\\begin{talltblr}[caption={Cars of the year}]{colspec={Q[]}}\na \\\\\n\\end{talltblr}"), "wider than the table"));
  CHECK(!check::warns(rec(tex), "wider than the table"));
  const auto w = rec(tblr("colspec={Q[]Q[]}, frobnicate=3pt, cell{1}{1}={c=2}{}", "a & b \\\\ c & d \\\\"));
  CHECK(check::warns(w, "frobnicate"));
  // A cell that spans columns and rows is drawn, once, and the cells it covers are not.
  const auto block = rec(tblr("colspec={Q[]Q[]Q[]}, cell{1}{1}={c=2, r=2}{bg=red}", "a & & z \\\\ p & q & y \\\\ m & n & x \\\\"), 300, 10,
                         InputMode::document);
  CHECK(block.warnings.empty());
  std::string drawn;
  for (const std::string& line : check::lines(block)) drawn += line;
  CHECK(drawn.find('a') != std::string::npos);
  CHECK(drawn.find('p') == std::string::npos && drawn.find('q') == std::string::npos);
  CHECK(drawn.find('m') != std::string::npos && drawn.find('z') != std::string::npos);
  // Its fill is one rectangle as wide as two columns and as tall as two rows.
  std::vector<DrawRecord> reds;
  for (const auto& f : check::of(block, DrawRecord::FILL_RECT)) {
    if (f.col == 0xFFFF0000u) reds.push_back(f);
  }
  CHECK_EQ(reds.size(), 1u);
  if (reds.size() == 1) {
    const auto single = rec(tblr("colspec={Q[]Q[]Q[]}, cell{1}{1}={}{bg=red}", "a & & z \\\\ p & q & y \\\\ m & n & x \\\\"), 300, 10,
                            InputMode::document);
    float oneWide = 0, oneTall = 0;
    for (const auto& f : check::of(single, DrawRecord::FILL_RECT)) {
      if (f.col == 0xFFFF0000u) { oneWide = f.width; oneTall = f.height; }
    }
    CHECK(reds[0].width > 1.8f * oneWide);
    CHECK(reds[0].height > 1.8f * oneTall);
  }
  // A row span alone is drawn, and says nothing; the cell under it is not drawn.
  const auto spanned = rec(tblr("colspec={Q[]Q[]}, cell{1}{1}={r=2}{}", "a & b \\\\ x & d \\\\"));
  CHECK(!check::warns(spanned, "row span"));
  std::string all;
  for (const std::string& line : check::lines(spanned)) all += line;
  CHECK(all.find('a') != std::string::npos);
  CHECK(all.find('x') == std::string::npos);
  // rowsep is read: it moves the rows, it does not warn.
  CHECK(!check::warns(rec(tblr("colspec={Q[]}, rowsep=3pt", "a \\\\ b \\\\")), "rowsep"));
}

// \fontsize reads its size as TeX does: em is the size around it, the other units are lengths, a bare number points.
TEST(fontsize_reads_em_and_absolute_units) {
  const auto width = [](const std::string& size) {
    return rec("{\\fontsize{" + size + "}{" + size + "}\\selectfont aaaa}", 300, 10, InputMode::mixed).width;
  };
  const float plain = rec("{aaaa}", 300, 10, InputMode::mixed).width;
  CHECK(std::fabs(width("2em") - width("20pt")) < 0.01f);
  CHECK(std::fabs(width("20") - width("20pt")) < 0.01f);
  CHECK(std::fabs(width("2em") - 2.f * plain) < 0.05f * plain);
  CHECK(width("0.5em") < plain);
  CHECK(std::fabs(width("1in") - width("72.27pt")) < 0.01f);
  CHECK(!rec("{\\fontsize{2ex}{2ex}\\selectfont a}", 300, 10, InputMode::mixed).warnings.empty());
}

// A colour set for a cell that spans columns is on that cell, and a cell after one is its own.
TEST(tblr_the_colours_of_a_spanning_cell_are_on_the_cell_and_on_no_other) {
  const auto count = [](const std::string& spec, std::uint32_t colour) {
    const auto r = rec(tblr("colspec={Q[]Q[]Q[]Q[]}, " + spec, "A & & B & \\\\ c & d & e & f \\\\"), 300, 10, InputMode::document);
    int n = 0;
    for (const auto& f : check::of(r, DrawRecord::FILL_RECT)) n += f.col == colour ? 1 : 0;
    return n;
  };
  const std::string spans = "cell{1}{1}={c=2}{bg=red}, cell{1}{3}={c=2}{bg=blue}";
  CHECK_EQ(count(spans, 0xFFFF0000u), 1);  // one rectangle for the cell that spans two columns, not one a column
  CHECK_EQ(count(spans, 0xFF0000FFu), 1);
  // The row's colour on a row whose cells span: each cell has it once.
  CHECK_EQ(count("cell{1}{1}={c=2}{}, cell{1}{3}={c=2}{}, row{1}={}{bg=red}", 0xFFFF0000u), 2);
  // Where the foreground is set, the text of the spanning cell is that colour.
  const auto fg = rec(tblr("colspec={Q[]Q[]Q[]Q[]}, cell{1}{3}={c=2}{fg=red}", "A & & B & \\\\ c & d & e & f \\\\"), 300, 10,
                      InputMode::document);
  CHECK(check::drawsColour(fg, 0xFFFF0000u));
}

// The cell a colour is for is found by its row and column, not by their digits run together: row 2 column 12 is
// not row 12 column 2.
TEST(tblr_a_colour_is_on_the_cell_it_names_in_a_table_of_twelve_columns_and_rows) {
  std::string spec = "colspec={Q[]Q[]Q[]Q[]Q[]Q[]Q[]Q[]Q[]Q[]Q[]Q[]}, cell{2}{12}={}{bg=red}";
  std::string body;
  for (int r = 0; r < 13; r++) {
    for (int c = 0; c < 12; c++) body += std::string(c > 0 ? " & " : "") + "x";
    body += " \\\\ ";
  }
  const auto r = rec(tblr(spec, body), 300, 10, InputMode::document);
  int n = 0;
  for (const auto& f : check::of(r, DrawRecord::FILL_RECT)) n += f.col == 0xFFFF0000u ? 1 : 0;
  CHECK_EQ(n, 1);
}

// A note wider than its table wraps at the table's width and hangs: its second line stands under the text of the
// first, 0.7em in (tabularray's \hangindent), not under the tag.
TEST(tblr_a_note_wider_than_the_table_wraps_and_hangs_under_its_text) {
  const std::string tex =
    "\\begin{talltblr}[note{a}={one two three four five six}]{colspec={Q[]}, hline{2}={}{0.05em}}\n"
    "aaaaaaaaaaaa \\\\\n\\end{talltblr}";
  const auto r = rec(tex, 300, 10, InputMode::document);
  CHECK(r.warnings.empty());
  float lastRule = -1e9f;
  for (const auto& l : check::of(r, DrawRecord::LINE)) lastRule = std::max(lastRule, std::max(l.y1, l.y2));
  // The note's lines: what is drawn below the table's last rule, line by line.
  std::vector<float> ys;
  for (const auto& t : check::of(r, DrawRecord::TEXT)) {
    if (t.y > lastRule && std::find_if(ys.begin(), ys.end(), [&](float y) { return std::fabs(y - t.y) < 3.f; }) == ys.end()) ys.push_back(t.y);
  }
  std::sort(ys.begin(), ys.end());
  CHECK(ys.size() >= 2);
  float tagX = 1e9f, laterX = 1e9f;
  for (const auto& t : check::of(r, DrawRecord::TEXT)) {
    if (t.y <= lastRule) continue;
    if (std::fabs(t.y - ys.front()) < 3.f) tagX = std::min(tagX, t.x); else laterX = std::min(laterX, t.x);
  }
  // The first line begins with the tag, in a box of 0.7em (7 at 10pt); the lines after it begin under the text.
  CHECK(std::fabs((laterX - tagX) - 7.f) < 1.f);
}

// TeX drops the space after a forced line break at the start of the next line, in a cell as anywhere.
TEST(tables_a_space_after_a_line_break_in_a_cell_does_not_start_the_next_line) {
  const auto startOfSecondLine = [](const std::string& body, bool tblrCell = false) {
    const auto r = rec(tblrCell ? "\\begin{tblr}{width=100pt, colspec={X[]}}\n{" + body + "} \\\\\n\\end{tblr}"
                                : "\\begin{tabular}{p{3cm}}" + body + "\\end{tabular}",
                       300, 10, InputMode::document);
    std::vector<float> ys;
    for (const auto& t : check::of(r, DrawRecord::TEXT)) {
      if (std::find_if(ys.begin(), ys.end(), [&](float y) { return std::fabs(y - t.y) < 1.f; }) == ys.end()) ys.push_back(t.y);
    }
    std::sort(ys.begin(), ys.end());
    float first = 1e9f, second = 1e9f;
    for (const auto& t : check::of(r, DrawRecord::TEXT)) {
      if (ys.size() < 2) break;
      if (std::fabs(t.y - ys[0]) < 1.f) first = std::min(first, t.x);
      if (std::fabs(t.y - ys[1]) < 1.f) second = std::min(second, t.x);
    }
    return std::make_pair(first, second);
  };
  const auto spaced = startOfSecondLine("Sed ut \\\\ perspiciatis");
  const auto tight = startOfSecondLine("Sed ut \\\\perspiciatis");
  CHECK(spaced.second < 1e8f);
  CHECK(std::fabs(spaced.second - spaced.first) < 0.01f);  // flush with the first line
  CHECK(std::fabs(spaced.second - tight.second) < 0.01f);  // the space is nothing
  // The same in a tabularray X cell, where the text is in braces.
  const auto inBraces = startOfSecondLine("Sed ut \\\\ perspiciatis", true);
  CHECK(inBraces.second < 1e8f);
  CHECK(std::fabs(inBraces.second - inBraces.first) < 0.01f);
}

// \NewTableCommand takes the arguments \newcommand does.
TEST(tabularray_NewTableCommand_defines_a_command_with_the_arguments_newcommand_has) {
  const auto text = [](const std::string& tex) {
    std::string all;
    for (const std::string& line : check::lines(rec(tex, 300, 10, InputMode::mixed))) all += line;
    return all;
  };
  CHECK_EQ(text("\\NewTableCommand{\\foo}[2]{[#1|#2]}\\foo{a}{b}"), std::string("[a|b]"));
  CHECK_EQ(text("\\NewTblrTableCommand{\\zzcmd}[1][x]{<#1>}\\zzcmd\\ \\zzcmd{y}"), std::string("<x><x>y"));  // [1][x]: an optional argument, `{y}` is not one
  CHECK(quiet("\\NewTableCommand{\\tinytableDefineColor}[3]{\\definecolor{#1}{#2}{#3}}"
              "\\tinytableDefineColor{cabc}{HTML}{aabbcc}\\textcolor{cabc}{x}"));
}

// tabularray takes a table command out of its cell, so the space or line end that follows one at the start of a cell is
// the start of the cell, and is dropped: the table is as wide as it is without the command.
TEST(tblr_a_table_command_at_the_start_of_a_cell_leaves_no_space_in_it) {
  const auto width = [](const std::string& first) {
    return rec("\\begin{tblr}{colspec={Q[]Q[]}}\n" + first + "\nmpg & cyl \\\\\n21.0 & 6 \\\\\n\\end{tblr}", 300, 10,
               InputMode::document).width;
  };
  CHECK(std::fabs(width("\\tinytableDefineColor{cz}{HTML}{FFA500}") - width("")) < 0.01f);
  CHECK(std::fabs(width("\\tinytableDefineColor{cz}{HTML}{FFA500}\\tinytableDefineColor{cy}{HTML}{00A5FF}") - width("")) < 0.01f);
}

// \ignorespaces expands what follows until a token that is not a space (a space after a braced argument is a space).
TEST(ignorespaces_drops_the_spaces_after_a_macro_and_only_those) {
  const auto width = [](const std::string& tex) { return rec(tex, 300, 10, InputMode::mixed).width; };
  const float tight = width("Ab");
  CHECK(width("A b") > tight + 0.5f);
  CHECK(std::fabs(width("\\def\\a#1{#1\\ignorespaces}\\a{A}   b") - tight) < 0.01f);
  CHECK(std::fabs(width("\\def\\s{ }\\def\\a#1{#1\\ignorespaces}\\a{A}\\s\\s b") - tight) < 0.01f);
  CHECK(width("\\def\\a#1{#1}\\a{A}   b") > tight + 0.5f);
}

// \newlength refuses a name that is taken, as LaTeX does ("Command \parindent already defined"), and keeps its value.
TEST(lengths_newlength_says_a_name_that_is_taken_and_keeps_its_value) {
  const auto twice = rec("\\newlength{\\gmx}\\setlength{\\gmx}{5pt}\\newlength{\\gmx}x", 300, 10, InputMode::document);
  CHECK(check::warns(twice, "\\gmx is already defined"));
  CHECK(check::warns(rec("\\newlength{\\parindent}x", 300, 10, InputMode::document), "\\parindent is already defined"));
  CHECK(rec("\\newlength{\\gmx}x", 300, 10, InputMode::document).warnings.empty());
  // The value set before the second \newlength is still there: a paragraph's indent shows it.
  const auto indented = [](const std::string& lead) {
    return check::of(rec(lead + "\\setlength{\\parindent}{30pt}\\newlength{\\parindent}\n\nx", 300, 10, InputMode::document),
                     DrawRecord::TEXT);
  };
  const auto first = indented("");
  CHECK(!first.empty());
  CHECK(first[0].x > 25.f);
}

// xparse looks past spaces for an optional argument and, when there is none, leaves them: `\f{x} b` keeps its space whatever
// `\f` ends in (LuaLaTeX, `\NewDocumentCommand\zza{mo}{[#1]}`: `a\zza{x} b` 24.73pt, `a\zza{x}b` 21.40, `a\zza{x}[y] b` 24.73).
TEST(xparse_a_trailing_optional_argument_that_is_not_there_leaves_the_space_after_the_command) {
  const auto width = [](const std::string& tex) { return rec(tex, 300, 10, InputMode::mixed).width; };
  for (const char* spec : {"mo", "ms", "mt="}) {
    const std::string def = std::string("a\\NewDocumentCommand\\zza{") + spec + "}{[#1]}";
    CHECK(width(def + "\\zza{x} b") > width(def + "\\zza{x}b") + 0.5f);
  }
  // Found, a spaced `[y]` is its argument, as it is in LaTeX.
  const std::string def = "a\\NewDocumentCommand\\zza{mo}{[#1]}";
  CHECK(std::fabs(width(def + "\\zza{x} [y] b") - width(def + "\\zza{x}[y] b")) < 0.01f);
}

// An `si` column keeps the room `table-format` says, and a key of siunitx's that is not read is said.
TEST(tblr_si_reserves_the_room_of_its_table_format_and_says_the_options_it_does_not_read) {
  const std::string rows = "1.5 \\\\ 22.25 \\\\";
  const auto natural = rec(tblr("colspec={Q[]}, column{1}={si={}}", rows), 300, 10, InputMode::document);
  const auto formatted = rec(tblr("colspec={Q[]}, column{1}={si={table-format=-5.3}}", rows), 300, 10, InputMode::document);
  CHECK(natural.warnings.empty());
  CHECK(formatted.warnings.empty());
  // Room for a sign, five digits, the marker and three decimals: wider than the numbers it holds.
  CHECK(formatted.width > natural.width + 10.f);
  // Room for five digits and a sign is more than for three digits without.
  const auto smaller = rec(tblr("colspec={Q[]}, column{1}={si={table-format=3.3}}", rows), 300, 10, InputMode::document);
  CHECK(formatted.width > smaller.width);
  CHECK(smaller.width > natural.width);
  const auto odd = rec(tblr("colspec={Q[]}, column{1}={si={table-format=3.2, round-mode=places}, a=b}", rows), 300, 10,
                       InputMode::document);
  CHECK(!check::warns(odd, "round-mode"));  // read
  const auto unread = rec(tblr("colspec={Q[]}, column{1}={si={table-format=3.2, group-separator=x}}", rows), 300, 10,
                          InputMode::document);
  CHECK(check::warns(unread, "group-separator"));
  const auto exponent = rec(tblr("colspec={Q[]}, column{1}={si={table-format=1.2e2}}", rows), 300, 10, InputMode::document);
  CHECK(check::warns(exponent, "exponent"));
}

// tabularray's `font=` is declarations the cell is set under, whatever they are.
TEST(tblr_font_sets_the_cell_under_the_declarations_it_names) {
  const auto plain = rec(tblr("colspec={Q[]}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  const auto big =
    rec(tblr("colspec={Q[]}, cell{1}{1}={}{font=\\fontsize{2em}{2em}\\selectfont}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  CHECK(big.warnings.empty());
  CHECK(big.height > plain.height);
  // The same as writing the declarations in the cell: no space of its own comes with them.
  const auto typed = rec(tblr("colspec={Q[]}", "{\\fontsize{2em}{2em}\\selectfont a} \\\\ b \\\\"), 300, 10, InputMode::document);
  CHECK(std::fabs(big.width - typed.width) < 0.01f);
  const auto bad = rec(tblr("colspec={Q[]}, cell{1}{1}={}{font=\\nosuchdeclaration}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  CHECK(check::warns(bad, "font=\\nosuchdeclaration"));
  // Small capitals are made from source text, which a built cell has not: said.
  CHECK(check::warns(rec(tblr("colspec={Q[]}, cell{1}{1}={}{font=\\scshape}", "a \\\\ b \\\\"), 300, 10, InputMode::document),
                     "small capitals"));
}

// tabularray reads `cmd=` and `font=` when it sets the cell, with the definitions in force at the `\begin`: a command
// the document defined before the table is known to them, one defined after it is not.
TEST(tblr_cmd_and_font_see_the_macros_the_document_defined_before_the_table) {
  const std::string table = tblr("colspec={Q[]}, cell{1}{1}={}{cmd=\\mybox, font=\\myfont}", "a \\\\ b \\\\");
  const auto plain = rec(tblr("colspec={Q[]}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  const auto known = rec("\\newcommand{\\mybox}[1]{\\fbox{#1}}\\newcommand{\\myfont}{\\bfseries}\n" + table, 300, 10,
                         InputMode::document);
  CHECK(known.warnings.empty());
  CHECK(known.width > plain.width);  // \fbox's frame
  // The same with the command written as `\fbox`: the document's macro is that command.
  const auto direct = rec(tblr("colspec={Q[]}, cell{1}{1}={}{cmd=\\fbox, font=\\bfseries}", "a \\\\ b \\\\"), 300, 10,
                          InputMode::document);
  CHECK(std::fabs(known.width - direct.width) < 0.01f);
  CHECK(std::fabs(known.height - direct.height) < 0.01f);
  // Defined after the table, TeX has not got it yet either.
  CHECK(check::warns(rec(table + "\n\\newcommand{\\mybox}[1]{\\fbox{#1}}\\newcommand{\\myfont}{\\bfseries}", 300, 10,
                         InputMode::document), "cmd=\\mybox"));
  // A definition inside a group that has ended is gone.
  CHECK(check::warns(rec("{\\newcommand{\\mybox}[1]{\\fbox{#1}}\\newcommand{\\myfont}{\\bfseries}}\n" + table, 300, 10,
                         InputMode::document), "cmd=\\mybox"));
}

// tabularray applies a cell's `cmd=` as `\cmd{text}`, whatever the command is.
TEST(tblr_cmd_applies_any_command_to_the_cell_and_says_one_it_does_not_know) {
  const auto plain = rec(tblr("colspec={Q[]}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  const auto boxed = rec(tblr("colspec={Q[]}, cell{1}{1}={}{cmd=\\fbox}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  CHECK(boxed.warnings.empty());
  // \fbox's frame and padding make the cell, and so the table, bigger.
  CHECK(boxed.width > plain.width);
  CHECK(boxed.height > plain.height);
  const auto unknown = rec(tblr("colspec={Q[]}, cell{1}{1}={}{cmd=\\nosuchcommand}", "a \\\\ b \\\\"), 300, 10, InputMode::document);
  CHECK(check::warns(unknown, "cmd=\\nosuchcommand"));
  // The LaTeX of a caption, a note and `preto` is said to be wrong where it is, not left drawn as a red name; and
  // the caption's own `\enskip` is known.
  const auto caption = rec("\\begin{talltblr}[caption={Cars \\nosuchcaptioncmd}]{colspec={Q[]}}\na \\\\\n\\end{talltblr}", 300, 10,
                           InputMode::document);
  CHECK(check::warns(caption, "caption"));
  CHECK(check::warns(rec("\\begin{talltblr}[note{a}={\\nosuchnotecmd}]{colspec={Q[]}}\na \\\\\n\\end{talltblr}", 300, 10,
                         InputMode::document), "note{a}"));
  CHECK(quiet("\\begin{talltblr}[caption={Cars}]{colspec={Q[]}}\naaaaaaaaaaaaaaaaaaaaaaaa \\\\\n\\end{talltblr}"));
  CHECK(quiet("a\\enskip b"));
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
  CHECK(same("\\makecell[tl]{a\\\\bb}", "\\begin{tabular}[t]{@{}l@{}}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[br]{a\\\\bb}", "\\begin{tabular}[b]{@{}r@{}}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[cl]{a\\\\bb}", "\\begin{tabular}[c]{@{}l@{}}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[r]{a\\\\bb}", "\\begin{tabular}[c]{@{}r@{}}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell[t]{a\\\\bb}", "\\begin{tabular}[t]{@{}c@{}}a\\\\bb\\end{tabular}"));
  CHECK(same("\\makecell{a\\\\bb}", "\\begin{tabular}[c]{@{}c@{}}a\\\\bb\\end{tabular}"));
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

TEST(tables_a_rule_before_a_p_column_is_drawn_across_the_table_and_extrarowheight_makes_rows_taller) {
  // An \hline in front of a p{} cell was drawn inside the cell, as a point: pdflatex draws it across the table.
  const auto longest = [](const std::string& tex) {
    float most = 0.f;
    for (const auto& l : check::of(rec(tex), DrawRecord::LINE)) most = std::max(most, std::fabs(l.x2 - l.x1));
    return most;
  };
  const float narrow = longest(R"(\begin{tabular}{|l|l|}\hline a&b\\\hline\end{tabular})");
  const float wide = longest(R"(\begin{tabular}{|p{3cm}|l|}\hline a&b\\\hline\end{tabular})");
  CHECK(narrow > 10.f);
  CHECK(wide > 2.f * narrow);
  size_t rules = 0;
  for (const auto& l : check::of(rec(R"(\begin{tabular}{|p{3cm}|}\hline a\\\hline b\\\hline\end{tabular})"), DrawRecord::LINE)) {
    if (std::fabs(l.x2 - l.x1) > 20.f) rules++;
  }
  CHECK_EQ(rules, size_t(3));
  // array.sty's \extrarowheight is more height in every row.
  const float plain = rec(R"(\begin{tabular}{c}a\\b\end{tabular})").height;
  const float taller = rec(R"({\setlength{\extrarowheight}{4pt}\begin{tabular}{c}a\\b\end{tabular}})").height;
  CHECK(taller > plain + 3);
  CHECK(quiet(R"({\setlength{\extrarowheight}{4pt}\begin{tabular}{c}a\end{tabular}})"));
}

TEST(tables_the_notes_of_a_threeparttable_stand_under_the_table) {
  const auto l = rec(R"(\begin{threeparttable}\begin{tabular}{cc}a&b\end{tabular}\begin{tablenotes}\item[a] note\end{tablenotes}\end{threeparttable})");
  float cell = -1e9f, note = -1e9f;
  for (const auto& r : check::of(l, DrawRecord::TEXT)) {
    if (r.text == "b") cell = r.y;
    if (r.text == "note") note = r.y;
  }
  CHECK(cell > -1e8f && note > cell + 5);
}

TEST(tables_a_longtables_caption_is_a_tables_numbered_by_the_table_counter) {
  // pdflatex: `\caption{A table}` in a longtable is "Table 1: A table".
  const auto words = check::of(rec("\\begin{longtable}{l}\\caption{A table}\\\\ a\\end{longtable}"), DrawRecord::TEXT);
  bool label = false;
  for (const auto& w : words) label = label || w.text == "Table" || w.text == "Table1:";
  CHECK(label);
}

// xcolor's \rowcolors{2}{red!40}{blue!40} (pdflatex): the header row is not coloured, row 2 is the even colour (blue) and
// row 3 the odd one (red): the colour of a row is its number's parity, from the row named.
TEST(tables_rowcolors_colours_the_rows_from_the_first_by_the_parity_of_their_number) {
  const std::string rows = "\\begin{tabular}{c}H\\\\a\\\\b\\\\c\\end{tabular}";
  const auto colours = [&](const std::string& tex) {
    std::vector<std::pair<float, std::uint32_t>> by;
    for (const auto& r : check::of(rec(tex), DrawRecord::FILL_RECT)) by.emplace_back(r.y, r.col);
    std::sort(by.begin(), by.end());
    std::vector<std::uint32_t> out;
    for (const auto& p : by) out.push_back(p.second);
    return out;
  };
  const auto got = colours("\\rowcolors{2}{red}{blue}" + rows);
  CHECK_EQ(got.size(), std::size_t(3));
  CHECK_EQ(got[0], 0xFF0000FFu);  // row 2, even: blue
  CHECK_EQ(got[1], 0xFFFF0000u);  // row 3, odd: red
  CHECK_EQ(got[2], 0xFF0000FFu);
  // From row 1, and an empty colour is none.
  CHECK_EQ(colours("\\rowcolors{1}{red}{}" + rows).size(), std::size_t(2));
  // Rules and a group: the table after the group is not coloured, and rules are not rows.
  CHECK_EQ(colours("{\\rowcolors{1}{red}{blue}}" + rows).size(), std::size_t(0));
  CHECK_EQ(colours("\\rowcolors{1}{red}{blue}\\begin{tabular}{c}\\hline a\\\\\\hline b\\\\\\hline\\end{tabular}"), (std::vector<std::uint32_t>{0xFFFF0000u, 0xFF0000FFu}));
  CHECK(check::warns(rec("\\rowcolors{1}{nosuchcolour}{red}" + rows), "undefined colour"));
}

// dcolumn: a D{in}{out}{places} column holds numbers set in math, aligned on the separator. (stargazer's align = TRUE
// and texreg's dcolumn = TRUE write `D{.}{.}{-1}`, with the stars as `^{***}` after the number.)
TEST(tables_a_dcolumn_aligns_its_cells_on_the_separator) {
  const std::string table = "\\begin{tabular}{D{.}{.}{-1}} 1.5 \\\\ 22.25 \\\\ 333 \\\\ -4.5^{***} \\end{tabular}";
  CHECK(quiet(table));
  // the '.' of every cell that has one stands in one vertical line
  const auto dot = check::of(rec("$.$", 300, 20, InputMode::mixed), DrawRecord::GLYPH);
  CHECK_EQ(dot.size(), std::size_t(1));
  if (dot.size() != 1) return;
  std::vector<float> xs;
  for (const auto& g : check::of(rec(table), DrawRecord::GLYPH)) {
    if (g.glyph_id == dot[0].glyph_id) xs.push_back(g.x);
  }
  CHECK_EQ(xs.size(), std::size_t(3));
  for (const float x : xs) CHECK(std::fabs(x - xs[0]) < 0.01f);
  // a number with no separator is flush right of it, before the separator's place
  const auto narrow = rec("\\begin{tabular}{D{.}{.}{-1}} 7 \\end{tabular}");
  const auto wide = rec("\\begin{tabular}{D{.}{.}{-1}} 7 \\\\ 1.5 \\end{tabular}");
  CHECK(wide.width > narrow.width);
  // the output separator may differ from the one typed
  CHECK(quiet("\\begin{tabular}{D{.}{,}{1}} 1.5 \\\\ 22.25 \\end{tabular}"));
  CHECK(quiet("\\begin{tabular}{lD{.}{.}{-1}D{.}{.}{-1}} a & 1.5 & 2.25 \\\\ \\multicolumn{1}{c}{b} & 3 & 4.125 \\end{tabular}"));
}

// siunitx: a comparator in front of a number is an ordinary symbol close to it (LuaLaTeX: \num{< 0.1} is 20.56pt, \mathord{<}0.1).
TEST(tables_siunitx_num_reads_a_comparator_in_front_of_the_number) {
  const std::string pkg = "\\usepackage{siunitx}";
  CHECK(quiet(pkg + "\\num{< 0.1}", 300, 10, InputMode::mixed));
  CHECK(quiet(pkg + "\\num{\\leq 3}", 300, 10, InputMode::mixed));
  CHECK(check::sameInk(rec(pkg + "\\num{< 0.1}", 300, 10, InputMode::mixed), rec(pkg + "$\\mathord{<}0.1$", 300, 10, InputMode::mixed)));
}

// A column spec that begins with @{...} puts that material before the first cell of every row, including the rows of a rule:
// stargazer's `@{\extracolsep{5pt}}lcc`. A rule is a rule all the same, and adds no row of text.
TEST(tables_a_rule_is_a_rule_after_a_leading_at_column) {
  const auto height = [&](const std::string& spec) {
    return rec("\\begin{tabular}{" + spec + "}a\\\\\\hline b\\\\\\hline\\end{tabular}").height;
  };
  CHECK_EQ(height("@{}l"), height("l"));
  CHECK_EQ(height("@{\\extracolsep{5pt}}l"), height("l"));
  CHECK_EQ(rec("\\begin{tabular}{@{}l}\\\\[-1.8ex]\\hline\\end{tabular}").height, rec("\\begin{tabular}{l}\\\\[-1.8ex]\\hline\\end{tabular}").height);
}

// `\extracolsep{5pt}` in an @ column adds 5pt to each gap between the columns after it (LuaLaTeX, lcc: 60pt with `@{}`,
// 70pt with `@{\extracolsep{5pt}}`: two gaps), and nothing at the edges.
TEST(tables_extracolsep_widens_each_gap_between_columns) {
  const auto width = [&](const std::string& spec) {
    return rec("\\begin{tabular}{" + spec + "}a & b & c\\end{tabular}", 0).width;
  };
  CHECK(std::abs((width("@{\\extracolsep{5pt}}lcc") - width("@{}lcc")) - 10) <= 1);
  CHECK(std::abs(width("@{\\extracolsep{1cm}}lc") - width("@{\\extracolsep{28.4528pt}}lc")) <= 1);
}

// A cell is read as far as its own end: an S column's last cell, with no `\\`, does not look into the text after
// `\end{tabular}` for a script, so the number is drawn the same whatever follows the table.
TEST(tables_an_s_cell_does_not_read_past_the_end_of_its_table) {
  const auto drawn = [&](const std::string& after) {
    const auto l = rec("\\begin{tabular}{S}12\\end{tabular}" + after, 300, 10);
    std::vector<std::pair<std::string, int>> out;
    for (const auto& r : check::of(l, DrawRecord::TEXT)) {
      if (r.text == "1" || r.text == "2" || r.text == "12") out.emplace_back(r.text, static_cast<int>(std::lround(r.x)));
    }
    return out;
  };
  CHECK(!drawn("").empty());
  CHECK(drawn("") == drawn(" $x^2$"));
}
