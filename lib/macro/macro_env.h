#ifndef MICROTEX_MACRO_ENV_H
#define MICROTEX_MACRO_ENV_H

#include <cctype>
#include <functional>
#include <vector>

#include "atom/atom_basic.h"
#include "atom/atom_fence.h"
#include "atom/atom_font.h"
#include "atom/atom_matrix.h"
#include "core/formula.h"
#include "env/units.h"
#include "front/tblr.h"
#include "macro/macro.h"
#include "macro/macro_colors.h"
#include "macro/macro_decl.h"
#include "utils/counter_format.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"

namespace microtex {

inline cmdmacro(smallmatrixATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::smallMatrix);
}

inline cmdmacro(matrixATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::matrix);
}

// LaTeX's own: three columns, right, centre, left, and numbered rows.
inline cmdmacro(eqnarrayATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  auto table = sptrOf<MatrixAtom>(args.isPartial(), arr, "rcl", true);
  table->setRowSpacing(RowSpacing::jot);
  return table;
}

/** A table's column gap is 2\tabcolsep in text and 2\arraycolsep in math, as in LaTeX. */
inline void setColumnSepOf(MatrixAtom& table, CommandArgs& args) {
  Dimen half = Units::getDimen(args.isMathMode() ? "\\arraycolsep" : "\\tabcolsep");
  if (!half.isValid()) return;
  half.val *= 2.f;
  table.setColumnSep(half);
}

// tabular*{width}{columns} and tabularx{width}{columns}.
inline sptr<Atom> widthTable(CommandArgs& args, bool spread) {
  const auto arr = args.alignment(3);
  arr->checkDimensions();
  auto table = sptrOf<MatrixAtom>(args.isPartial(), arr, args.text(2), true);
  setColumnSepOf(*table, args);
  table->setTableWidth(Units::getDimen(args.text(1)), spread);
  table->setRowSpacing(RowSpacing::strut);
  return table;
}

inline cmdmacro(tabularstarATATenv) {
  return widthTable(args, true);
}

inline cmdmacro(tabularxATATenv) {
  return widthTable(args, false);
}

// longtable[position]{columns}: a tabular. The lowering has set its rows in
// order, and its caption above.
inline cmdmacro(longtableATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  auto table = sptrOf<MatrixAtom>(args.isPartial(), arr, args.text(1), true);
  setColumnSepOf(*table, args);
  table->setRowSpacing(RowSpacing::strut);
  return table;
}

// tabularray's tblr[options]{spec}: a tabular, of the columns its spec gives.
inline cmdmacro(tblrATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  const auto spec = front::parseTblr("", args.text(1));
  auto table = sptrOf<MatrixAtom>(args.isPartial(), arr, front::tblrColumns(spec, arr->cols()), true);
  setColumnSepOf(*table, args);  // tabularray's colsep is 6pt too
  return table;
}

inline cmdmacro(arrayATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  auto table = sptrOf<MatrixAtom>(args.isPartial(), arr, args.text(1), true);
  setColumnSepOf(*table, args);
  // tabular is this handler too: a table of text has TeX's strut rows as an array has.
  if (arr->_rows == "jot") {
    table->setRowSpacing(RowSpacing::jot);
  } else if (arr->_rows == "sub") {
    table->setRowSpacing(RowSpacing::sub);
  } else {
    table->setRowSpacing(RowSpacing::strut);
  }
  return table;
}

inline cmdmacro(alignATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::align);
}

inline cmdmacro(flalignATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::flAlign);
}

inline cmdmacro(alignatATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  size_t n = 0;
  valueOf(args.text(1), n);
  if (arr->cols() != 2 * n) throw ex_parse("Bad number of equations in alignat environment!");

  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::alignAt);
}

inline cmdmacro(alignedATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::aligned);
}

inline cmdmacro(alignedatATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  size_t n = 0;
  valueOf(args.text(1), n);
  if (arr->cols() != 2 * n) {
    throw ex_parse("Bad number of equations in alignedat environment!");
  }

  return sptrOf<MatrixAtom>(args.isPartial(), arr, MatrixType::alignedAt);
}

inline cmdmacro(multlineATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  if (arr->cols() > 1) {
    throw ex_parse("Requires exact one column in multiline environment!");
  }
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(args.isPartial(), arr, MultiLineType::multiline);
}

inline cmdmacro(gatherATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  if (arr->cols() > 1) throw ex_parse("Requires exact one column in gather environment!");
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(args.isPartial(), arr, MultiLineType::gather);
}

inline cmdmacro(gatheredATATenv) {
  const auto arr = args.alignment(1);
  arr->checkDimensions();
  if (arr->cols() > 1) throw ex_parse("Requires exact one column in gathered environment!");
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(args.isPartial(), arr, MultiLineType::gathered);
}

// The cases package's numcases{left side} and subnumcases: the left side, a
// brace, and the rows of a case and its explanation, each with a number at
// the right margin of the display. The lowering has numbered the rows
// (`_rowTags`), as it does an align's. The numbers are set here as a column of
// their own at the right, the rows of which are as tall as the cases' (each
// has a phantom of its row's cells), so that each stands level with its case:
// as a tag of the one line the display is.
inline cmdmacro(numcasesATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  std::size_t rows = arr->_array.size();
  while (rows > 0 && arr->_array[rows - 1].empty()) rows--;
  if (rows == 0) return nullptr;

  auto numbers = sptrOf<ArrayFormula>();
  numbers->_stretch = arr->_stretch;
  bool numbered = false;
  for (std::size_t r = 0; r < rows; r++) {
    auto cell = sptrOf<RowAtom>();
    for (const auto& c : arr->_array[r]) {
      if (c != nullptr) cell->add(sptrOf<PhantomAtom>(c, false, true, true));
    }
    const auto tag = arr->_rowTags.find(static_cast<int>(r));
    if (tag != arr->_rowTags.end()) {
      cell->add(tag->second);
      numbered = true;
    }
    numbers->add(cell);
    const auto gap = arr->_rowGaps.find(static_cast<int>(r));
    if (gap != arr->_rowGaps.end()) numbers->addRowGap(gap->second);
    numbers->addRow();
  }
  arr->_rowTags.clear();

  const auto left = args.formulaOf(args.text(1), true);
  auto line = sptrOf<RowAtom>();
  if (left != nullptr) line->add(left);
  // Rows as amsmath sets them, the cases' and their numbers' alike.
  auto cases = sptrOf<MatrixAtom>(
    args.isPartial(), arr, "@{\\hspace{3pt}}>{\\displaystyle}l@{\\quad}l@{}", true);
  cases->setRowSpacing(RowSpacing::jot);
  line->add(sptrOf<FencedAtom>(cases, "lbrace", "."));

  auto display = sptrOf<ArrayFormula>();
  display->add(line);
  if (numbered) {
    numbers->checkDimensions();
    auto column = sptrOf<MatrixAtom>(args.isPartial(), numbers, "@{}r@{}", true);
    column->setRowSpacing(RowSpacing::jot);
    display->_rowTags[0] = column;
  }
  display->checkDimensions();
  return sptrOf<MultlineAtom>(false, display, MultiLineType::gather);
}

// What a \multicolumn or \multirow spans is a cell: math in an array, text
// in a tabular met in text.
inline sptr<Atom> cellContent(CommandArgs& args, std::size_t i) {
  if (args.isMathMode()) return args.formula(i, true, true);
  return sptrOf<FontStyleAtom>(FontStyle::rm, false, args.formula(i, false));
}

// No table has more columns than a column specification can list (the cap
// in MatrixAtom::parsePositions): a larger span is a runaway, not a table.
constexpr int kMaxSpan = 1 << 14;

inline cmdmacro(multicolumn) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command 'multicolumn' only available in array mode!");
  int n = 0;
  valueOf(args.text(1), n);
  n = std::min(n, kMaxSpan);
  arr->add(sptrOf<MulticolumnAtom>(n, args.text(2), cellContent(args, 3)));
  // addCol() leaves the last column it spans to the next & or \\, which for
  // one column is the cell itself.
  if (n > 1) arr->addCol(n);
  return nullptr;
}

inline cmdmacro(hdotsfor) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command 'hdotsfor' only available in array mode!");
  int n = 0;
  valueOf(args.text(1), n);
  n = std::min(n, kMaxSpan);
  float f = 1.f;
  if (!args.text(2).empty()) valueOf(args.text(2), f);
  arr->add(sptrOf<HdotsforAtom>(n, f));
  arr->addCol(n);
  return nullptr;
}

inline cmdmacro(hline) {
  if (args.alignmentHere() == nullptr) throw ex_parse("The macro \\hline only available in array mode!");
  return sptrOf<HlineAtom>();
}

// arydshln's \hdashline: a dashed rule across the table.
inline cmdmacro(hdashline) {
  if (args.alignmentHere() == nullptr) throw ex_parse("The macro \\hdashline only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  a->setDashed(true);
  return a;
}

inline cmdmacro(thickhline) {
  if (args.alignmentHere() == nullptr)
    throw ex_parse("The macro \\thickhline only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  a->setThicknessScale(2.f);
  return a;
}

// booktabs' \specialrule{thickness}{above}{below}: a rule of that thickness, with that much space above and below it.
inline cmdmacro(specialrule) {
  if (args.alignmentHere() == nullptr)
    throw ex_parse("The macro \\specialrule only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  const Dimen thick = Units::getDimen(args.text(1));
  if (thick.isValid()) {
    a->setThickness(thick.val, thick.unit);
  } else {
    a->setThicknessScale(2.f);  // an unreadable thickness: \toprule's
  }
  a->setSpacing(Units::getDimen(args.text(2)), Units::getDimen(args.text(3)));
  return a;
}

// booktabs' \addlinespace[len]: space of that height between two rows, or
// .5em.
inline cmdmacro(addlinespace) {
  if (args.alignmentHere() == nullptr)
    throw ex_parse("The macro \\addlinespace only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  const Dimen gap = Units::getDimen(args.text(1));
  if (gap.isValid()) {
    a->setThickness(gap.val, gap.unit);
  } else {
    a->setThickness(0.5f, UnitType::em);
  }
  a->setBlank(true);
  return a;
}

inline sptr<HlineAtom> clineRule(CommandArgs& args, const char* name) {
  if (args.alignmentHere() == nullptr) throw ex_parse(std::string("The macro \\") + name + " only available in array mode!");
  const std::string& spec = args.text(1);
  int a = 0, b = 0;
  const auto dash = spec.find('-');
  if (dash == std::string::npos) {
    valueOf(spec, a);
    b = a;
  } else {
    valueOf(spec.substr(0, dash), a);
    valueOf(spec.substr(dash + 1), b);
  }
  auto at = sptrOf<HlineAtom>();
  // LaTeX columns are 1-indexed; HlineAtom uses 0-indexed. Kept in a range
  // `- 1` cannot overflow; MatrixAtom clamps them to the table's columns.
  constexpr int most = 1 << 20;
  a = std::min(std::max(a, 0), most);
  b = std::min(std::max(b, 0), most);
  at->setColumnRange(a - 1, b - 1);
  return at;
}

inline cmdmacro(cline) {
  return clineRule(args, "cline");
}

// booktabs' \cmidrule[width](trim){a-b}: a light rule across columns a to b, with \aboverulesep (0.4ex) above it and
// \belowrulesep (0.65ex) below, as \midrule has.
inline cmdmacro(cmidrule) {
  auto at = clineRule(args, "cmidrule");
  at->setThickness(0.05f, UnitType::em);
  at->setSpacing({0.4f, UnitType::ex}, {0.65f, UnitType::ex});
  return at;
}

inline cmdmacro(multirow) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command \\multirow must used in array environment!");
  int n = 0;
  valueOf(args.text(1), n);
  arr->add(sptrOf<MultiRowAtom>(n, args.text(2), cellContent(args, 3)));
  return nullptr;
}

inline cmdmacro(cellcolor) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command \\cellcolor must used in array environment!");
  color c = colourArg(args, args.text(1));
  if (!isTransparent(c)) arr->addCellSpecifier(sptrOf<CellColorAtom>(c));
  return nullptr;
}

inline cmdmacro(newcolumntype) {
  MatrixAtom::defineColumnSpecifier(args.text(1), args.text(2));
  return nullptr;
}

inline cmdmacro(arrayrulecolor) {
  color c = colourArg(args, args.text(1));
  if (!isTransparent(c)) {
    MatrixAtom::LINE_COLOR = c;
    gArrayRuleColor = c;
  }
  return nullptr;
}

inline cmdmacro(columnbg) {
  color c = colourArg(args, args.text(1));
  return isTransparent(c) ? nullptr : sptrOf<CellColorAtom>(c);
}

inline cmdmacro(rowcolor) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command \\rowcolor must used in array environment!");
  color c = colourArg(args, args.text(1));
  if (!isTransparent(c)) arr->addRowSpecifier(sptrOf<CellColorAtom>(c));
  return nullptr;
}

inline cmdmacro(shoveright) {
  auto a = orEmpty(args.formula(1, true, true));
  a->_alignment = Alignment::right;
  return a;
}

inline cmdmacro(shoveleft) {
  auto a = orEmpty(args.formula(1, true, true));
  a->_alignment = Alignment::left;
  return a;
}

// region itemize / enumerate list environments

// Peel a leading optional [..] argument from `body` (after skipping
// whitespace). Returns its content; on success `body` is advanced past
// the closing bracket. If there is no balanced optional argument, the
// empty string is returned and `body` is left untouched.
inline std::string listPeelOptional(std::string& body) {
  size_t i = 0;
  while (i < body.size() && std::isspace((unsigned char)body[i]) != 0) i++;
  if (i >= body.size() || body[i] != '[') return "";
  int depth = 0;
  for (size_t j = i + 1; j < body.size(); j++) {
    const char c = body[j];
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
    } else if (c == ']' && depth == 0) {
      std::string opt = body.substr(i + 1, j - i - 1);
      body.erase(0, j + 1);
      return opt;
    }
  }
  return "";
}

// Split a list body into items on top-level \item tokens. Tokens nested
// inside braces or \begin..\end environments are ignored. Anything before
// the first \item is discarded (LaTeX forbids it there).
inline std::vector<std::string> listSplitItems(const std::string& body) {
  std::vector<std::string> items;
  std::string cur;
  int brace = 0, env = 0;
  bool started = false;
  size_t i = 0;
  while (i < body.size()) {
    if (body[i] == '\\') {
      size_t j = i + 1;
      while (j < body.size() && std::isalpha((unsigned char)body[j]) != 0) j++;
      const std::string cmd = body.substr(i + 1, j - i - 1);
      if (cmd == "begin") {
        env++;
      } else if (cmd == "end") {
        env--;
      } else if (cmd == "item" && brace == 0 && env == 0) {
        if (started) items.push_back(cur);
        cur.clear();
        started = true;
        i = j;
        continue;
      }
      if (j == i + 1) j = i + 2;  // control symbol such as \\ or \{
      cur.append(body, i, j - i);
      i = j;
      continue;
    }
    const char c = body[i];
    if (c == '{') {
      brace++;
    } else if (c == '}') {
      brace--;
    }
    cur += c;
    i++;
  }
  if (started) items.push_back(cur);
  return items;
}

// Build the enumerate marker for item index `n` (1-based) from a label
// template. The first counter placeholder (\arabic*, \alph*, \Alph*,
// \roman*, \Roman*) is substituted; a template with no placeholder
// yields a constant marker.
inline std::string listFormatLabel(const std::string& tmpl, int n) {
  struct Tok {
    const char* name;
    int kind;
  };
  static const Tok toks[] = {
    {"\\arabic*", 0},
    {"\\Alph*", 1},
    {"\\alph*", 2},
    {"\\Roman*", 3},
    {"\\roman*", 4},
  };
  for (const Tok& t : toks) {
    const std::string name = t.name;
    const size_t p = tmpl.find(name);
    if (p == std::string::npos) continue;
    std::string num;
    switch (t.kind) {
      case 0: num = std::to_string(n); break;
      case 1: num = listToUpper(listAlph(n)); break;
      case 2: num = listAlph(n); break;
      case 3: num = listToUpper(listRoman(n)); break;
      default: num = listRoman(n); break;
    }
    return tmpl.substr(0, p) + num + tmpl.substr(p + name.size());
  }
  return tmpl;
}

// `s` less the spaces at either end, which TeX drops from an item. A
// control space `\ ` is not one of them: trimmed, it left its backslash to
// join the `\\` that ends the row, and the next row lost its first cell.
inline std::string listTrim(const std::string& s) {
  size_t from = 0, to = s.size();
  while (from < to && std::isspace((unsigned char)s[from]) != 0) from++;
  while (to > from && std::isspace((unsigned char)s[to - 1]) != 0) {
    size_t slashes = 0;
    while (to - 1 - slashes > from && s[to - 2 - slashes] == '\\') slashes++;
    if (slashes % 2 == 1) break;
    to--;
  }
  return s.substr(from, to - from);
}

// An item's text met in text is text, as in LaTeX. A `}` that closes
// nothing is dropped, as TeX drops it, so that it cannot end the \text{}.
inline std::string listTextItem(const std::string& item) {
  std::string out;
  int depth = 0;
  for (size_t i = 0; i < item.size(); i++) {
    const char c = item[i];
    if (c == '\\' && i + 1 < item.size()) {
      out += c;
      out += item[++i];
      continue;
    }
    if (c == '}' && depth == 0) continue;
    if (c == '{') depth++;
    if (c == '}') depth--;
    out += c;
  }
  return "\\text{" + out + std::string((size_t)depth, '}') + "}";
}

// An item's text and the lists nested in it, in turn: text, list, text, ...
inline std::vector<std::string> listParts(const std::string& item) {
  const auto at = [&](size_t pos, const std::string& s) { return item.compare(pos, s.size(), s) == 0; };
  std::vector<std::string> parts{""};
  size_t i = 0;
  while (i < item.size()) {
    if (!at(i, "\\begin{itemize}") && !at(i, "\\begin{enumerate}") &&
        !at(i, "\\begin{description}")) {
      if (item[i] == '\\' && i + 1 < item.size()) parts.back() += item[i++];
      parts.back() += item[i++];
      continue;
    }
    // Up to its own \end, nested environments counted.
    size_t j = i + 1;
    int depth = 1;
    while (j < item.size() && depth > 0) {
      if (at(j, "\\begin{")) depth++;
      if (at(j, "\\end{") && --depth == 0) {
        const size_t close = item.find('}', j);
        j = close == std::string::npos ? item.size() : close + 1;
        break;
      }
      j++;
    }
    parts.push_back(item.substr(i, j - i));
    parts.emplace_back();
    i = j;
  }
  return parts;
}

// How deep the list being built is in other lists (a nested list is lowered while its outer one is): LaTeX's default
// labels change with the level.
inline int& listDepth() {
  static thread_local int depth = 0;
  return depth;
}

// Lay a list body out as rows, one per item: its marker, set right as
// LaTeX sets labels, a quad, and its text in an X column, which takes the
// rest of the text width and wraps to it (the whole line without a
// width). A list nested after an item's text goes on rows of its own,
// level with that text, as LaTeX sets it; one that opens the item shares
// the item's first line, as in LaTeX too.
//
// `columns` is the column spec when it is not the usual one; `noteLabels` sets
// an item's own label as a superscript, as threeparttable's notes are; and
// `declarations` (a `\small` that opens the list's body) is put before the
// text of every item.
inline sptr<Atom> listBuild(
  CommandArgs& args,
  const std::vector<std::string>& items,
  const std::function<std::string(int)>& marker,
  bool description = false,
  const char* columns = nullptr,
  bool noteLabels = false,
  const std::string& declarations = std::string(),
  bool numbered = false
) {
  if (items.empty()) return nullptr;
  std::string s;
  const auto row = [&](const std::string& lead, const std::string& content) {
    if (!s.empty()) s += "\\\\";
    s += lead + "&" + content;
  };
  // An item's own label stands for its marker, and a labelled item is no
  // step of the counter, as in LaTeX.
  int counted = 0;
  for (size_t i = 0; i < items.size(); i++) {
    std::string body = items[i];
    const std::string label = listPeelOptional(body);
    std::string mark;
    if (description) {
      mark = label.empty() ? "" : "\\textbf{" + label + "}";
    } else if (noteLabels && !label.empty()) {
      // threeparttable's \tnote in a label box of 0.5em.
      mark = "\\rlap{\\textsuperscript{" + label + "}}\\hspace{0.5em}";
    } else if (!label.empty()) {
      mark = args.isMathMode() ? "\\text{" + label + "}" : label;
    } else {
      mark = marker(++counted);
    }
    // What a \label in an enumerated item names: the item's number, as `\theenumi` is, which a custom label does not change.
    const std::string named = numbered && label.empty() ? "\\gmcurrentlabel{" + std::to_string(counted) + "}" : std::string();
    const auto parts = listParts(body);
    bool first = true;
    for (size_t k = 0; k < parts.size(); k++) {
      const std::string part = listTrim(parts[k]);
      if (part.empty()) continue;
      const bool nested = k % 2 == 1;
      // A nested list is met in text when its item is text.
      const std::string content = (first ? named : std::string()) + (args.isMathMode() ? part
                                  : nested         ? "\\text{" + part + "}"
                                                   : listTextItem(part));
      row(first ? mark : "", declarations.empty() ? content : declarations + " " + content);
      first = false;
    }
    if (first) row(mark, "");  // an empty item still has its marker
  }
  struct Deeper {
    Deeper() { listDepth()++; }
    ~Deeper() { listDepth()--; }
  };
  sptr<ArrayFormula> arr;
  {
    const Deeper inside;   // the lists in this one's items are lowered here
    arr = args.alignmentOfText(s);
  }
  arr->checkDimensions();
  if (columns == nullptr) columns = description ? "l@{\\quad}X" : "r@{\\quad}X";
  auto list = sptrOf<MatrixAtom>(args.isPartial(), arr, columns, false);
  list->_flow = true;
  return list;
}

// threeparttable's tablenotes: a list whose item labels are superscripts in a
// box of 0.5em, \leftmargin 1.5em and \labelsep 0.2em, so the label starts 0.8em
// in and the text 1.5em (pdflatex: 141 and 148 against a left edge of 133, in
// points, though the package sets \leftskip \tabcolsep: the list ignores it).
// `[para]` runs the notes on in one
// paragraph; a `\small` before the first item sets all of them.
inline cmdmacro(tablenotesATATenv) {
  std::string body = args.text(1);
  const std::string options = listPeelOptional(body);
  // What stands before the first \item: declarations, which LaTeX lets stand there.
  const auto firstItem = body.find("\\item");
  const std::string declarations =
    listTrim(firstItem == std::string::npos ? body : body.substr(0, firstItem));
  const auto items = listSplitItems(body);
  if (options.find("para") != std::string::npos) {
    std::string runOn = declarations;
    for (size_t i = 0; i < items.size(); i++) {
      std::string item = items[i];
      const std::string label = listPeelOptional(item);
      if (i > 0) runOn += "\\quad ";
      if (!label.empty()) runOn += "\\textsuperscript{" + label + "}\\,";
      runOn += listTrim(item);
    }
    return args.formulaOf(runOn, false);
  }
  return listBuild(args, items, [](int) { return std::string("\\hspace{0.5em}"); }, false,
                   "@{\\hspace{0.8em}}l@{\\hspace{0.2em}}X", true, declarations);
}

inline cmdmacro(itemizeATATenv) {
  std::string body = args.text(1);
  const std::string opt = listPeelOptional(body);
  // LaTeX's labels by level: a bullet, a bold en dash (\labelitemii is `\normalfont\bfseries\textendash`), an asterisk, a
  // centred dot.
  static const char* marks[] = {"\\bullet", "\\text{\\bfseries\\textendash}", "\\ast", "\\cdot"};
  const std::string mark = opt.empty() ? marks[std::min(listDepth(), 3)] : opt;
  return listBuild(args, listSplitItems(body), [&](int) { return mark; });
}

inline cmdmacro(enumerateATATenv) {
  std::string body = args.text(1);
  std::string opt = listPeelOptional(body);
  // LaTeX's labels by level: 1., (a), i., A.
  static const char* labels[] = {"\\arabic*.", "(\\alph*)", "\\roman*.", "\\Alph*."};
  if (opt.empty()) opt = labels[std::min(listDepth(), 3)];
  return listBuild(args, listSplitItems(body), [&](int n) {
    return "\\mathrm{" + listFormatLabel(opt, n) + "}";
  }, false, nullptr, false, std::string(), true);
}

// A description: each item's label, in bold, hangs before its text.
inline cmdmacro(descriptionATATenv) {
  std::string body = args.text(1);
  return listBuild(args, listSplitItems(body), [](int) { return std::string(); }, true);
}

// endregion

}  // namespace microtex

#endif  // MICROTEX_MACRO_ENV_H
