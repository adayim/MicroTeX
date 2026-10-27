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
  /** The columns it covers (`3-4,7-10,13`); all of them where none is given. */
  TblrIndex cols;
  Dimen thickness;
  /** A colour name (`cFFA500`, `white`), "" for the table's rule colour; dashes for `dashed`/`dotted`. */
  std::string colour;
  bool dashed = false;
  /** Where its ends stand in the first and last cell, as tabularray's
   *  `leftpos` and `rightpos`: 1 is the cell's edge, a negative number
   *  moves the end in by that part of the cell's padding. */
  float leftpos = 1.f;
  float rightpos = 1.f;
};

/** One unbroken piece of a horizontal rule: columns `from`..`to` (1-based,
 *  inclusive), with the end positions of its first and last cell. */
struct TblrRuleSegment {
  int from = 1;
  int to = 1;
  float leftpos = 1.f;
  float rightpos = 1.f;
  Dimen thickness;
  std::string colour;
  bool dashed = false;
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
  /** `font=`: the declarations, as written (`\bfseries\fontsize{1.5em}{1.5em}\selectfont`): the cell's text is
   *  set under them, as tabularray sets it. */
  std::string font;
  bool hasFont = false;
  /** "l", "c" or "r", or "". */
  std::string halign;
  /** `valign=` (or the bare `t`, `m`, `b`, `h`, `f`, `p`): where the text stands in a row taller than it, or in the
   *  rows it spans; 0 where not said (tabularray's initial value is `t`). */
  char valign = 0;
  /** A row's `abovesep` and `belowsep` (`rowsep` sets both); invalid where none is given. */
  Dimen aboveSep;
  Dimen belowSep;
  /** `si={...}`: a siunitx S column, whose numbers are set as `\num` does and lined up at the marker. The options
   *  (table-format, ...) are not read. */
  bool siunitx = false;
  std::string si;  // its options, as written
  /** `cmd=`: the commands, as written, that are applied to the cell's text: `\cmd{text}`. `guard` and an
   *  empty value say none. `hasCmd` tells that the setting says either. */
  std::string cmd;
  bool hasCmd = false;
  /** LaTeX put before and after the cell's text (`preto`, `appto`). */
  std::string prepend;
  std::string append;
};

/** A table note, `note{a}={Blah.}`: the tag in front of its text, as a superscript. */
struct TblrNote {
  std::string tag;
  std::string text;
};

struct TblrSpec {
  /** The columns, as a tabular's column specification. */
  std::string columns;
  std::vector<TblrRule> horizontal;
  std::vector<TblrRule> vertical;
  std::vector<TblrSetting> settings;
  /** The caption of a talltblr or longtblr. */
  std::string caption;
  /** The outer keys `headsep` and `footsep`: the space below a caption and above the notes. Invalid where not
   *  given (tabularray's initial value is 6pt for both). */
  Dimen headsep;
  Dimen footsep;
  /** `width=`: what the table's X columns fill; invalid for none. */
  Dimen width;
  /** The notes under it, in the order they are written. */
  std::vector<TblrNote> notes;
  /** What was not understood, each as its key. */
  std::vector<std::string> unsupported;
};

/** The spec of `\begin{tblr}[outer]{inner}`. */
TblrSpec parseTblr(const std::string& outer, const std::string& inner);

/** The table's column specification, as a tabular's: its columns (`l` for
 *  each of `ncols` where it gives none), with its vertical rules. */
std::string tblrColumns(const TblrSpec& spec, int ncols);

/** The pieces of the horizontal rule above row `at` of a table of `rows` by
 *  `columns`, left to right. Each column takes the last rule written that
 *  covers it, as tabularray does; a piece ends where the rule does, where
 *  its thickness changes, and where either side of the join is drawn short
 *  (`l=-0.5`), so groups of columns keep a gap between them. */
std::vector<TblrRuleSegment> tblrRuleSegments(const TblrSpec& spec, int at, int rows, int columns);

/** What applies to the cell at row `i`, column `j` (1-based) of an `n` by
 *  `m` table: the settings that name it, the later over the earlier. */
TblrSetting tblrCell(const TblrSpec& spec, int i, int j, int n, int m);

}  // namespace microtex::front

#endif
