#ifndef MICROTEX_MACRO_ENV_H
#define MICROTEX_MACRO_ENV_H

#include <cctype>
#include <functional>
#include <vector>

#include "atom/atom_basic.h"
#include "atom/atom_font.h"
#include "atom/atom_matrix.h"
#include "core/formula.h"
#include "env/units.h"
#include "macro/macro.h"
#include "macro/macro_decl.h"
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

inline cmdmacro(arrayATATenv) {
  const auto arr = args.alignment(2);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, args.text(1), true);
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

// What a \multicolumn or \multirow spans is a cell: math in an array, text
// in a tabular met in text.
inline sptr<Atom> cellContent(CommandArgs& args, std::size_t i) {
  if (args.isMathMode()) return args.formula(i, true, true);
  return sptrOf<FontStyleAtom>(FontStyle::rm, false, args.formula(i, false));
}

inline cmdmacro(multicolumn) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command 'multicolumn' only available in array mode!");
  int n = 0;
  valueOf(args.text(1), n);
  arr->add(sptrOf<MulticolumnAtom>(n, args.text(2), cellContent(args, 3)));
  arr->addCol(n);
  return nullptr;
}

inline cmdmacro(hdotsfor) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command 'hdotsfor' only available in array mode!");
  int n = 0;
  valueOf(args.text(1), n);
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

inline cmdmacro(thickhline) {
  if (args.alignmentHere() == nullptr)
    throw ex_parse("The macro \\thickhline only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  a->setThicknessScale(2.f);
  return a;
}

// booktabs' \specialrule{thickness}{above}{below}: a rule of that
// thickness. The space above and below is not set; no rule here has any.
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
  return a;
}

inline cmdmacro(cline) {
  if (args.alignmentHere() == nullptr)
    throw ex_parse("The macro \\cline only available in array mode!");
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
  color c = ColorAtom::getColor(args.text(1));
  arr->addCellSpecifier(sptrOf<CellColorAtom>(c));
  return nullptr;
}

inline cmdmacro(newcolumntype) {
  MatrixAtom::defineColumnSpecifier(args.text(1), args.text(2));
  return nullptr;
}

inline cmdmacro(arrayrulecolor) {
  color c = ColorAtom::getColor(args.text(1));
  MatrixAtom::LINE_COLOR = c;
  return nullptr;
}

inline cmdmacro(columnbg) {
  color c = ColorAtom::getColor(args.text(1));
  return sptrOf<CellColorAtom>(c);
}

inline cmdmacro(rowcolor) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command \\rowcolor must used in array environment!");
  color c = ColorAtom::getColor(args.text(1));
  arr->addRowSpecifier(sptrOf<CellColorAtom>(c));
  return nullptr;
}

inline cmdmacro(shoveright) {
  auto a = args.formula(1, true, true);
  a->_alignment = Alignment::right;
  return a;
}

inline cmdmacro(shoveleft) {
  auto a = args.formula(1, true, true);
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

inline std::string listRoman(int n) {
  static const int v[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  static const char* s[] =
    {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
  std::string r;
  for (int k = 0; k < 13 && n > 0; k++) {
    while (n >= v[k]) {
      r += s[k];
      n -= v[k];
    }
  }
  return r;
}

inline std::string listAlph(int n) {
  std::string r;
  while (n > 0) {
    n--;
    r = (char)('a' + n % 26) + r;
    n /= 26;
  }
  return r;
}

inline std::string listToUpper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
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
    if (!at(i, "\\begin{itemize}") && !at(i, "\\begin{enumerate}")) {
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

// Lay a list body out as rows, one per item: its marker, set right as
// LaTeX sets labels, a quad, and its text in an X column, which takes the
// rest of the text width and wraps to it (the whole line without a
// width). A list nested after an item's text goes on rows of its own,
// level with that text, as LaTeX sets it; one that opens the item shares
// the item's first line, as in LaTeX too.
inline sptr<Atom> listBuild(
  CommandArgs& args,
  const std::vector<std::string>& items,
  const std::function<std::string(int)>& marker
) {
  if (items.empty()) return nullptr;
  std::string s;
  const auto row = [&](const std::string& lead, const std::string& content) {
    if (!s.empty()) s += "\\\\";
    s += lead + "&" + content;
  };
  for (size_t i = 0; i < items.size(); i++) {
    const std::string mark = marker((int)i + 1);
    const auto parts = listParts(items[i]);
    bool first = true;
    for (size_t k = 0; k < parts.size(); k++) {
      const std::string part = listTrim(parts[k]);
      if (part.empty()) continue;
      const bool nested = k % 2 == 1;
      // A nested list is met in text when its item is text.
      const std::string content = args.isMathMode() ? part
                                  : nested         ? "\\text{" + part + "}"
                                                   : listTextItem(part);
      row(first ? mark : "", content);
      first = false;
    }
    if (first) row(mark, "");  // an empty item still has its marker
  }
  const auto arr = args.alignmentOfText(s);
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(args.isPartial(), arr, "r@{\\quad}X", false);
}

inline cmdmacro(itemizeATATenv) {
  std::string body = args.text(1);
  const std::string opt = listPeelOptional(body);
  const std::string mark = opt.empty() ? "\\bullet" : opt;
  return listBuild(args, listSplitItems(body), [&](int) { return mark; });
}

inline cmdmacro(enumerateATATenv) {
  std::string body = args.text(1);
  std::string opt = listPeelOptional(body);
  if (opt.empty()) opt = "\\arabic*.";
  return listBuild(args, listSplitItems(body), [&](int n) {
    return "\\mathrm{" + listFormatLabel(opt, n) + "}";
  });
}

// endregion

}  // namespace microtex

#endif  // MICROTEX_MACRO_ENV_H
