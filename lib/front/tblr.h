#ifndef GRIDMICROTEX_FRONT_TBLR_H
#define GRIDMICROTEX_FRONT_TBLR_H

#include <string>
#include <vector>

#include "env/units.h"

namespace microtex::front {

/**
 * tabularray's `tblr`, the table of tinytable's LaTeX output: a subset, read
 * from its spec (`colspec`, `hline{2}={1-4}{0.05em}`, `row{2}={}{bg=red}`,
 * `cell{1}{3}={c=2}{halign=c}` ...) into what the lowering sets a table
 * with. What is not understood is listed, to be said once.
 */

/** The rows or columns a setting is for: 1-based, as tabularray's are. */
struct TblrIndex {
  /** Inclusive ranges; `to` is 0 for no end. */
  std::vector<std::pair<int, int>> ranges;
  bool odd = false;
  bool even = false;
  bool all = false;

  /** Whether `i` (1-based) of `n` is among them. */
  bool matches(int i, int n) const;
};

/** A rule across the table: above row `at` (a vertical one: left of column
 *  `at`), for the columns `from`..`to` (0: all of them). */
struct TblrRule {
  TblrIndex at;
  int from = 0;
  int to = 0;
  Dimen thickness;
};

/** What is set for the cells of some rows and columns. */
struct TblrSetting {
  TblrIndex rows;
  TblrIndex cols;
  /** Which wins where several name a cell, whatever the order they were
   *  written in: cell over row over column over cells, rows and columns. */
  int priority = 0;
  int colspan = 1;
  int rowspan = 1;
  std::string background;
  std::string foreground;
  /** A font switch, as `bfseries`: "bf", "it", "tt", "sf", "rm", or "". */
  std::string font;
  /** "l", "c" or "r", or "". */
  std::string halign;
};

struct TblrSpec {
  /** The columns, as a tabular's column specification. */
  std::string columns;
  std::vector<TblrRule> horizontal;
  std::vector<TblrRule> vertical;
  std::vector<TblrSetting> settings;
  /** The caption of a talltblr or longtblr. */
  std::string caption;
  /** What was not understood, each as its key. */
  std::vector<std::string> unsupported;
};

/** The spec of `\begin{tblr}[outer]{inner}`. */
TblrSpec parseTblr(const std::string& outer, const std::string& inner);

/** Whether `name` is one of tabularray's table environments. */
bool isTblrEnvironment(const std::string& name);

/** The table's column specification, as a tabular's: its columns (`l` for
 *  each of `ncols` where it gives none), with its vertical rules. */
std::string tblrColumns(const TblrSpec& spec, int ncols);

/** What applies to the cell at row `i`, column `j` (1-based) of an `n` by
 *  `m` table: the settings that name it, the later over the earlier. */
TblrSetting tblrCell(const TblrSpec& spec, int i, int j, int n, int m);

}  // namespace microtex::front

#endif
