// tabularray's tblr spec (lib/front/tblr.h), read as tinytable writes it.

#include <string>

#include "check.h"
#include "front/tblr.h"

using namespace microtex::front;

TEST(tblr_colspec_becomes_a_tabular_column_spec) {
  CHECK_EQ(parseTblr("", "colspec={Q[]Q[]Q[]}").columns, std::string("lll"));
  CHECK_EQ(parseTblr("", "colspec={Q[r]Q[c]Q[l]}").columns, std::string("rcl"));
  CHECK_EQ(parseTblr("", "colspec={Q[halign=r]}").columns, std::string("r"));
  CHECK_EQ(parseTblr("", "colspec={Q[wd=3cm]X[r]}").columns,
           std::string("p{3cm}>{\\raggedleft\\arraybackslash}X"));
  CHECK_EQ(parseTblr("", "colspec={*{3}{c}}").columns, std::string("ccc"));
  CHECK_EQ(parseTblr("", "colspec={lcr}").columns, std::string("lcr"));
  // No more columns than a column specification can hold.
  CHECK(parseTblr("", "colspec={*{999999}{c}}").columns.size() <= 100u);
}

TEST(tblr_vertical_rules_go_between_the_columns) {
  const TblrSpec spec = parseTblr("", "colspec={Q[]Q[]}, vline{1,3}={solid}");
  CHECK_EQ(tblrColumns(spec, 2), std::string("|ll|"));
  // Where none is given: one l for each column.
  CHECK_EQ(tblrColumns(parseTblr("", ""), 3), std::string("lll"));
  CHECK_EQ(tblrColumns(parseTblr("", "vlines={}, colspec={ll}"), 2), std::string("|l|l|"));
}

TEST(tblr_rules_have_positions_columns_and_thickness) {
  const TblrSpec spec = parseTblr("", "hline{2}={1-4}{solid, black, 0.05em}, hline{1,3}={0.08em}");
  CHECK_EQ(spec.horizontal.size(), 2u);
  const TblrRule& first = spec.horizontal[0];
  CHECK(first.at.matches(2, 4));
  CHECK(!first.at.matches(1, 4));
  CHECK(first.cols.matches(1, 4));
  CHECK(first.cols.matches(4, 4));
  CHECK(!first.cols.matches(5, 8));
  CHECK(first.thickness.isValid());
  CHECK(spec.horizontal[1].at.matches(3, 4));
  CHECK(spec.horizontal[1].cols.matches(2, 4));
}

TEST(tblr_a_rule_keeps_every_range_of_its_column_list) {
  // tinytable's header rules over column groups: three pieces, not the first range only.
  const TblrSpec spec = parseTblr("", "hline{2}={3-4,7-10,13}{solid, black, 0.03em}");
  const std::vector<TblrRuleSegment> s = tblrRuleSegments(spec, 2, 5, 13);
  CHECK_EQ(s.size(), 3u);
  CHECK_EQ(s[0].from, 3);
  CHECK_EQ(s[0].to, 4);
  CHECK_EQ(s[1].from, 7);
  CHECK_EQ(s[1].to, 10);
  CHECK_EQ(s[2].from, 13);
  CHECK_EQ(s[2].to, 13);
  CHECK(tblrRuleSegments(spec, 3, 5, 13).empty());
}

TEST(tblr_leftpos_and_rightpos_trim_the_ends_and_leave_gaps) {
  // tinytable's three entries for one row: the groups 2-5, 6-11, 12-13 each end short,
  // so the rules of neighbouring groups do not touch.
  const TblrSpec spec = parseTblr("",
    "hline{2}={3-4,7-10,13}{solid, black, 0.03em}, hline{2}={2,6,12}{solid, black, 0.03em, l=-0.5},"
    " hline{2}={5,11}{solid, black, 0.03em, r=-0.5}");
  const std::vector<TblrRuleSegment> s = tblrRuleSegments(spec, 2, 5, 13);
  CHECK_EQ(s.size(), 3u);
  CHECK_EQ(s[0].from, 2);
  CHECK_EQ(s[0].to, 5);
  CHECK_EQ(s[0].leftpos, -0.5f);
  CHECK_EQ(s[0].rightpos, -0.5f);
  CHECK_EQ(s[1].from, 6);
  CHECK_EQ(s[1].to, 11);
  CHECK_EQ(s[2].from, 12);
  CHECK_EQ(s[2].to, 13);
  CHECK_EQ(s[2].rightpos, 1.f);
  // The later rule wins where two cover a column; ranges that meet with no trim are one piece.
  const TblrSpec over = parseTblr("", "hlines={0.05em}, hline{1}={2}{0.1em}");
  CHECK_EQ(tblrRuleSegments(over, 2, 3, 4).size(), 1u);
  CHECK_EQ(tblrRuleSegments(over, 1, 3, 4).size(), 3u);
}

TEST(tblr_cells_rows_and_columns_take_what_is_set_for_them) {
  const TblrSpec spec = parseTblr(
    "", "cell{1}{3}={c=2}{halign=c}, row{2}={}{font=\\bfseries, bg=red}, column{2-4}={}{halign=r}");
  const TblrSetting a = tblrCell(spec, 1, 3, 5, 4);
  CHECK_EQ(a.colspan, 2);
  CHECK_EQ(a.halign, std::string("c"));
  const TblrSetting b = tblrCell(spec, 2, 1, 5, 4);
  CHECK(b.hasFont);
  CHECK_EQ(b.font, std::string("\\bfseries"));
  CHECK_EQ(b.background, std::string("red"));
  CHECK_EQ(b.halign, std::string(""));
  // A later setting wins where both name a cell.
  const TblrSetting c = tblrCell(spec, 1, 3, 5, 4);
  CHECK_EQ(c.halign, std::string("c"));
  CHECK_EQ(tblrCell(spec, 3, 2, 5, 4).halign, std::string("r"));
  CHECK_EQ(tblrCell(spec, 3, 1, 5, 4).halign, std::string(""));
}

TEST(tblr_indexes_are_numbers_ranges_odd_and_even) {
  const TblrSpec spec = parseTblr("", "row{odd}={}{bg=red}, row{3-}={}{fg=blue}, row{2}={}{bg=green}");
  CHECK_EQ(tblrCell(spec, 1, 1, 5, 1).background, std::string("red"));
  CHECK_EQ(tblrCell(spec, 2, 1, 5, 1).background, std::string("green"));
  CHECK_EQ(tblrCell(spec, 3, 1, 5, 1).foreground, std::string("blue"));
  CHECK_EQ(tblrCell(spec, 2, 1, 5, 1).foreground, std::string(""));
}

TEST(tblr_preto_and_appto_add_text_before_and_after_a_cell) {
  // tinytable's indent_text: `preto={\hspace{2em}}` on the cells of a column.
  const TblrSpec spec = parseTblr("", "cell{2-3}{1}={}{preto={\\hspace{2em}}}, row{2}={}{preto=x, appto={y}}");
  CHECK(spec.unsupported.empty());
  CHECK_EQ(tblrCell(spec, 1, 1, 3, 2).prepend, std::string(""));
  CHECK_EQ(tblrCell(spec, 3, 1, 3, 2).prepend, std::string("\\hspace{2em}"));
  // Each setting adds: the later one goes outside the earlier.
  CHECK_EQ(tblrCell(spec, 2, 1, 3, 2).prepend, std::string("\\hspace{2em}x"));
  CHECK_EQ(tblrCell(spec, 2, 2, 3, 2).append, std::string("y"));
}

TEST(tblr_cmd_is_kept_as_written_to_be_applied_to_the_cell) {
  // Any commands: tabularray applies them as `\cmd{text}`, the spec names none in particular.
  const TblrSpec one = parseTblr("", "row{2}={}{cmd=\\textbf, fg=red}");
  CHECK(one.unsupported.empty());
  CHECK(tblrCell(one, 2, 1, 3, 1).hasCmd);
  CHECK_EQ(tblrCell(one, 2, 1, 3, 1).cmd, std::string("\\textbf"));
  CHECK_EQ(tblrCell(one, 2, 1, 3, 1).foreground, std::string("red"));
  CHECK(!tblrCell(one, 1, 1, 3, 1).hasCmd);
  const TblrSpec two = parseTblr("", "cell{1}{1}={}{cmd=\\emph\\textbf}");
  CHECK_EQ(tblrCell(two, 1, 1, 1, 1).cmd, std::string("\\emph\\textbf"));
  // `guard` is a cmd of nothing, and a later setting overrides an earlier one.
  const TblrSpec guard = parseTblr("", "row{1}={}{cmd=\\textbf}, cell{1}{1}={}{guard}");
  CHECK(tblrCell(guard, 1, 1, 1, 1).hasCmd);
  CHECK(tblrCell(guard, 1, 1, 1, 1).cmd.empty());
}

TEST(tblr_font_is_kept_as_written_to_set_the_cell_under) {
  // Any declarations: the spec does not read them, the lowering sets the cell's text under them.
  const TblrSpec spec = parseTblr("",
    "cell{1}{1}={}{font=\\itshape}, cell{1}{2}={}{font=\\bfseries\\fontsize{0.85em}{0.85em}\\selectfont},"
    " cell{1}{3}={}{font=\\small}, cell{1}{4}={}{font=\\scshape\\unheardof}");
  CHECK(spec.unsupported.empty());
  const TblrSetting a = tblrCell(spec, 1, 1, 2, 4);
  CHECK(a.hasFont);
  CHECK_EQ(a.font, std::string("\\itshape"));
  CHECK_EQ(tblrCell(spec, 1, 2, 2, 4).font, std::string("\\bfseries\\fontsize{0.85em}{0.85em}\\selectfont"));
  CHECK_EQ(tblrCell(spec, 1, 4, 2, 4).font, std::string("\\scshape\\unheardof"));
  CHECK(!tblrCell(spec, 2, 1, 2, 4).hasFont);
}

TEST(tblr_rowsep_and_abovesep_set_the_space_inside_a_row) {
  const TblrSpec spec = parseTblr("", "rowsep=3pt, row{2}={abovesep=1pt}");
  CHECK(spec.unsupported.empty());
  CHECK(tblrCell(spec, 1, 1, 3, 1).aboveSep.isValid());
  CHECK(tblrCell(spec, 1, 1, 3, 1).belowSep.isValid());
  // The row's own setting is later and wins above; below keeps the table's.
  CHECK_EQ(tblrCell(spec, 2, 1, 3, 1).aboveSep.val, 1.f);
  CHECK_EQ(tblrCell(spec, 2, 1, 3, 1).belowSep.val, 3.f);
}

TEST(tblr_says_what_it_does_not_understand_and_never_runs_away) {
  const TblrSpec spec = parseTblr("", "frobnicate=3pt, colspec={Q[]}, cell{1}{1}={font=\\scshape}");
  CHECK_EQ(spec.unsupported.size(), 1u);  // a font is not the spec's to judge: the lowering sets the cell under it
  // Unbalanced braces, empty input, a missing value.
  parseTblr("", "{{{{");
  parseTblr("", "hline{");
  parseTblr("[", "}}}");
  parseTblr("", std::string(100000, '{'));
  parseTblr("", std::string(100000, ','));
  CHECK(true);
  // The caption of a talltblr.
  CHECK_EQ(parseTblr("caption={Cars}", "").caption, std::string("Cars"));
}
