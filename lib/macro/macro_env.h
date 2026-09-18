#ifndef MICROTEX_MACRO_ENV_H
#define MICROTEX_MACRO_ENV_H

#include <cctype>
#include <functional>
#include <vector>

#include "atom/atom_basic.h"
#include "atom/atom_matrix.h"
#include "core/formula.h"
#include "core/parser.h"
#include "macro/macro.h"
#include "macro/macro_decl.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"

namespace microtex {

inline macro(smallmatrixATATenv) {
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), args[1], arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::smallMatrix);
}

inline macro(matrixATATenv) {
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), args[1], arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::matrix);
}

inline macro(arrayATATenv) {
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), args[2], arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), args[1], true);
}

inline macro(alignATATenv) {
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), args[1], arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::align);
}

inline macro(flalignATATenv) {
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), args[1], arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::flAlign);
}

inline macro(alignatATATenv) {
  auto* arr = new ArrayFormula();
  Parser par(tp.isPartial(), args[2], arr, false);
  par.parse();
  arr->checkDimensions();
  size_t n = 0;
  valueOf(args[1], n);
  if (arr->cols() != 2 * n) throw ex_parse("Bad number of equations in alignat environment!");

  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::alignAt);
}

inline macro(alignedATATenv) {
  auto* arr = new ArrayFormula();
  Parser p(tp.isPartial(), args[1], arr, false);
  p.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::aligned);
}

inline macro(alignedatATATenv) {
  auto* arr = new ArrayFormula();
  Parser p(tp.isPartial(), args[2], arr, false);
  p.parse();
  arr->checkDimensions();
  size_t n = 0;
  valueOf(args[1], n);
  if (arr->cols() != 2 * n) {
    throw ex_parse("Bad number of equations in alignedat environment!");
  }

  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MatrixType::alignedAt);
}

inline macro(multlineATATenv) {
  auto* arr = new ArrayFormula();
  Parser p(tp.isPartial(), args[1], arr, false);
  p.parse();
  arr->checkDimensions();
  if (arr->cols() > 1) {
    throw ex_parse("Requires exact one column in multiline environment!");
  }
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MultiLineType::multiline);
}

inline macro(gatherATATenv) {
  auto* arr = new ArrayFormula();
  Parser p(tp.isPartial(), args[1], arr, false);
  p.parse();
  arr->checkDimensions();
  if (arr->cols() > 1) throw ex_parse("Requires exact one column in gather environment!");
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MultiLineType::gather);
}

inline macro(gatheredATATenv) {
  auto* arr = new ArrayFormula();
  Parser p(tp.isPartial(), args[1], arr, false);
  p.parse();
  arr->checkDimensions();
  if (arr->cols() > 1) throw ex_parse("Requires exact one column in gathered environment!");
  if (arr->cols() == 0) return nullptr;

  return sptrOf<MultlineAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), MultiLineType::gathered);
}

inline macro(multicolumn) {
  if (!tp.isArrayMode()) throw ex_parse("Command 'multicolumn' only available in array mode!");
  int n = 0;
  valueOf(args[1], n);
  tp.addAtom(sptrOf<MulticolumnAtom>(n, args[2], Formula(tp, args[3])._root));
  ((ArrayFormula*)tp._formula)->addCol(n);
  return nullptr;
}

inline macro(hdotsfor) {
  if (!tp.isArrayMode()) throw ex_parse("Command 'hdotsfor' only available in array mode!");
  int n = 0;
  valueOf(args[1], n);
  float f = 1.f;
  if (!args[2].empty()) valueOf(args[2], f);
  tp.addAtom(sptrOf<HdotsforAtom>(n, f));
  ((ArrayFormula*)tp._formula)->addCol(n);
  return nullptr;
}

inline macro(hline) {
  if (!tp.isArrayMode()) throw ex_parse("The macro \\hline only available in array mode!");
  return sptrOf<HlineAtom>();
}

inline macro(thickhline) {
  if (!tp.isArrayMode())
    throw ex_parse("The macro \\thickhline only available in array mode!");
  auto a = sptrOf<HlineAtom>();
  a->setThicknessScale(2.f);
  return a;
}

inline macro(cline) {
  if (!tp.isArrayMode())
    throw ex_parse("The macro \\cline only available in array mode!");
  const std::string& spec = args[1];
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
  // LaTeX columns are 1-indexed; HlineAtom uses 0-indexed.
  at->setColumnRange(a - 1, b - 1);
  return at;
}

inline macro(multirow) {
  if (!tp.isArrayMode()) throw ex_parse("Command \\multirow must used in array environment!");
  int n = 0;
  valueOf(args[1], n);
  tp.addAtom(sptrOf<MultiRowAtom>(n, args[2], Formula(tp, args[3])._root));
  return nullptr;
}

inline macro(cellcolor) {
  if (!tp.isArrayMode()) throw ex_parse("Command \\cellcolor must used in array environment!");
  color c = ColorAtom::getColor(args[1]);
  auto atom = sptrOf<CellColorAtom>(c);
  ((ArrayFormula*)tp._formula)->addCellSpecifier(atom);
  return nullptr;
}

inline macro(color) {
  if (tp.isArrayMode()) {
    color c = ColorAtom::getColor(args[1]);
    return sptrOf<CellForegroundAtom>(c);
  }
  // Outside array mode, \color is a LaTeX declaration that changes the
  // current foreground colour for every atom until the end of the
  // enclosing group. Approximate that here by consuming the remainder
  // of the current group and wrapping it in a ColorAtom — so
  // `{\color{blue} E = mc^2}` colours the whole inner formula and
  // `\color{blue} E = mc^2` colours the rest of the top-level input.
  const std::string rest = tp.forwardBalancedGroup();
  auto a = Formula(tp, rest, false, tp.isMathMode())._root;
  return sptrOf<ColorAtom>(a, TRANSPARENT, ColorAtom::getColor(args[1]));
}

inline macro(newcolumntype) {
  MatrixAtom::defineColumnSpecifier(args[1], args[2]);
  return nullptr;
}

inline macro(arrayrulecolor) {
  color c = ColorAtom::getColor(args[1]);
  MatrixAtom::LINE_COLOR = c;
  return nullptr;
}

inline macro(columnbg) {
  color c = ColorAtom::getColor(args[1]);
  return sptrOf<CellColorAtom>(c);
}

inline macro(rowcolor) {
  if (!tp.isArrayMode()) throw ex_parse("Command \\rowcolor must used in array environment!");
  color c = ColorAtom::getColor(args[1]);
  auto spe = sptrOf<CellColorAtom>(c);
  ((ArrayFormula*)tp._formula)->addRowSpecifier(spe);
  return nullptr;
}

inline macro(shoveright) {
  auto a = Formula(tp, args[1])._root;
  a->_alignment = Alignment::right;
  return a;
}

inline macro(shoveleft) {
  auto a = Formula(tp, args[1])._root;
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

// Lay a list body out as a single left-aligned column, one row per item,
// each row prefixed with `marker(index)`.
inline sptr<Atom> listBuild(
  Parser& tp,
  const std::vector<std::string>& items,
  const std::function<std::string(int)>& marker
) {
  if (items.empty()) return nullptr;
  std::string s;
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) s += "\\\\";
    s += marker((int)i + 1) + "\\quad{}" + items[i];
  }
  auto* arr = new ArrayFormula();
  Parser parser(tp.isPartial(), s, arr, false);
  parser.parse();
  arr->checkDimensions();
  return sptrOf<MatrixAtom>(tp.isPartial(), sptr<ArrayFormula>(arr), "l", false);
}

inline macro(itemizeATATenv) {
  std::string body = args[1];
  const std::string opt = listPeelOptional(body);
  const std::string mark = opt.empty() ? "\\bullet" : opt;
  return listBuild(tp, listSplitItems(body), [&](int) { return mark; });
}

inline macro(enumerateATATenv) {
  std::string body = args[1];
  std::string opt = listPeelOptional(body);
  if (opt.empty()) opt = "\\arabic*.";
  return listBuild(tp, listSplitItems(body), [&](int n) {
    return "\\mathrm{" + listFormatLabel(opt, n) + "}";
  });
}

// endregion

}  // namespace microtex

#endif  // MICROTEX_MACRO_ENV_H
