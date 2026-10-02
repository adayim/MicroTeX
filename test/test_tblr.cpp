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
  CHECK_EQ(first.from, 1);
  CHECK_EQ(first.to, 4);
  CHECK(first.thickness.isValid());
  CHECK(spec.horizontal[1].at.matches(3, 4));
  CHECK_EQ(spec.horizontal[1].from, 0);
}

TEST(tblr_cells_rows_and_columns_take_what_is_set_for_them) {
  const TblrSpec spec = parseTblr(
    "", "cell{1}{3}={c=2}{halign=c}, row{2}={}{font=\\bfseries, bg=red}, column{2-4}={}{halign=r}");
  const TblrSetting a = tblrCell(spec, 1, 3, 5, 4);
  CHECK_EQ(a.colspan, 2);
  CHECK_EQ(a.halign, std::string("c"));
  const TblrSetting b = tblrCell(spec, 2, 1, 5, 4);
  CHECK_EQ(b.font, std::string("bf"));
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

TEST(tblr_says_what_it_does_not_understand_and_never_runs_away) {
  const TblrSpec spec = parseTblr("", "rowsep=3pt, colspec={Q[]}, cell{1}{1}={font=\\Huge}");
  CHECK(!spec.unsupported.empty());
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
