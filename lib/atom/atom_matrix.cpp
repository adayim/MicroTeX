#include "atom/atom_matrix.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "atom/atom_basic.h"
#include "atom/atom_font.h"
#include "atom/atom_row.h"
#include "atom/atom_vrow.h"
#include "core/formula.h"
#include "core/glue.h"
#include "core/split.h"
#include "env/env.h"
#include "env/units.h"
#include "front/lower.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"

using namespace std;
using namespace microtex;

color MatrixAtom::LINE_COLOR = transparent;

map<string, string> MatrixAtom::_colspeReplacement;

SpaceAtom MatrixAtom::_hsep(UnitType::em, 1.f, 0.f, 0.f);
SpaceAtom MatrixAtom::_semihsep(UnitType::em, 0.5f, 0.f, 0.f);
SpaceAtom MatrixAtom::_vsep_in(UnitType::ex, 0.f, 1.f, 0.f);
SpaceAtom MatrixAtom::_vsep_ext_top(UnitType::ex, 0.f, 0.5f, 0.f);
SpaceAtom MatrixAtom::_vsep_ext_bot(UnitType::ex, 0.f, 0.5f, 0.f);
SpaceAtom MatrixAtom::_align(SpaceType::medMuSkip);

sptr<Box> MatrixAtom::_nullbox = StrutBox::empty();

void MatrixAtom::defineColumnSpecifier(const string& rep, const string& spe) {
  _colspeReplacement[rep] = spe;
}

void MatrixAtom::resetDefinitions() {
  _colspeReplacement.clear();
  LINE_COLOR = transparent;
  gArrayRuleColor = transparent;
}

namespace {

/** One argument in a column specification, read TeX's way: a braced group
 *  (its text, with nested braces and \{ \} in it), or else one token -- a
 *  control word or a single character. */
struct SpecArgument {
  string text;
  /** Index just past what was read. */
  int end;
  bool found;
  bool braced;
};

SpecArgument specArgument(const string& s, int pos) {
  const int len = static_cast<int>(s.size());
  while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) pos++;
  if (pos >= len) return {"", pos, false, false};
  if (s[pos] == '{') {
    int depth = 0;
    for (int i = pos; i < len; i++) {
      if (s[i] == '\\' && i + 1 < len) {
        i++;
        continue;
      }
      if (s[i] == '{') depth++;
      if (s[i] == '}' && --depth == 0) return {s.substr(pos + 1, i - pos - 1), i + 1, true, true};
    }
    return {s.substr(pos + 1), len, true, true};  // never closed: the rest
  }
  int end = pos + 1;
  if (s[pos] == '\\') {
    while (end < len && std::isalpha(static_cast<unsigned char>(s[end]))) end++;
    if (end == pos + 1 && end < len) end++;  // a control symbol
  } else {
    // A whole UTF-8 character, not one byte of it.
    while (end < len && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) end++;
  }
  return {s.substr(pos, end - pos), end, true, false};
}

/** What an argument of @{...} or >{...} draws: a group is read as a formula
 *  in a row of its own, a lone token as itself, and nothing as nothing. */
sptr<Atom> specAtom(const SpecArgument& a) {
  if (!a.found) return sptrOf<EmptyAtom>();
  auto atom = front::buildFragment(a.text, true);
  if (!a.braced) return atom;
  auto row = sptrOf<RowAtom>();
  row->add(atom);
  return row;
}

// A paragraph cell, `box` broken to the column's width `w`, with each of
// its lines aligned in it as `>{\centering}` or `>{\raggedleft}` sets them.
sptr<Box> alignedCell(const sptr<Box>& box, float w, Alignment align) {
  const auto lines = dynamic_pointer_cast<VBox>(box);
  if (align == Alignment::left || lines == nullptr || !lines->_lines) {
    return sptrOf<HBox>(box, w, align);
  }
  auto out = sptrOf<VBox>();
  out->_lines = true;
  for (const auto& line : lines->_children) {
    if (dynamic_pointer_cast<HBox>(line) != nullptr) {
      out->add(sptrOf<HBox>(line, w, align));
    } else {
      out->add(line);  // the space between lines
    }
  }
  return sptrOf<HBox>(out, w, Alignment::left);
}

}  // namespace

void MatrixAtom::parsePositions(string opt, vector<Alignment>& lpos) {
  int len = opt.length();
  int pos = 0;
  char ch;
  // Column types and *{n}{...} insert text to be read on. A type defined
  // in terms of itself, or a count past any table, would never end.
  constexpr int maxReplacements = 1000;
  constexpr size_t maxLength = 1 << 14;
  int replacements = 0;
  const auto runaway = []() {
    return ex_parse("Column specification expands without end: is a column type defined in terms of itself?");
  };
  // clear first
  lpos.clear();
  _fillCols.clear();
  _lineAligns.clear();
  _colStyles.clear();
  _colFonts.clear();
  _colSuffixes.clear();
  while (pos < len) {
    ch = opt[pos];
    switch (ch) {
      case 'l': lpos.push_back(Alignment::left); break;
      case 'r': lpos.push_back(Alignment::right); break;
      case 'c': lpos.push_back(Alignment::center); break;
      case 'X':
        _fillCols.push_back(static_cast<int>(lpos.size()));
        lpos.push_back(Alignment::left);
        break;
      case '|': {
        int nb = 1;
        while (++pos < len) {
          ch = opt[pos];
          if (ch != '|') {
            pos--;
            break;
          } else {
            nb++;
          }
        }
        _vlines[lpos.size()] = sptrOf<VlineAtom>(nb);
      } break;
      case ':': {
        // A dashed line between columns.
        auto dashed = sptrOf<VlineAtom>(1);
        dashed->_dashed = true;
        _vlines[lpos.size()] = dashed;
      } break;
      case '<': {
        // What follows each cell of the column before it.
        const SpecArgument a = specArgument(opt, pos + 1);
        if (!lpos.empty()) _colSuffixes[static_cast<int>(lpos.size()) - 1] = specAtom(a);
        pos = a.end - 1;
      } break;
      case '!':  // between columns, as @ is, but keeping the space there
      case '@': {
        const SpecArgument a = specArgument(opt, pos + 1);
        // Keep columns same with the matrix
        if (lpos.size() > _matrix->cols()) {
          lpos.resize(_matrix->cols());
        }
        _matrix->insertAtomIntoCol(lpos.size(), specAtom(a));
        if (const auto at = a.text.find("\\gmextracolsep{"); at != string::npos) {
          const auto from = at + 15, to = a.text.find('}', from);
          const string len = to == string::npos ? string() : a.text.substr(from, to - from);
          // (\fill is tabular*'s, handled by its width)
          if (!len.empty() && len[0] != '\\') _extraColSep[static_cast<int>(lpos.size())] = Units::getDimen(len);
        }

        lpos.push_back(Alignment::none);
        pos = a.end - 1;
      } break;
      case '*': {
        // *{n}{cols}: the columns n times over, read from where they end.
        const SpecArgument times = specArgument(opt, pos + 1);
        const SpecArgument cols = specArgument(opt, times.end);
        int nrep = 0;
        valueOf(times.text, nrep);
        if (nrep > 0 && static_cast<size_t>(nrep) * cols.text.size() + opt.size() > maxLength) {
          throw runaway();
        }
        string str;
        for (int j = 0; j < nrep; j++) str += cols.text;
        pos = cols.end;
        opt.insert(pos, str);
        len = opt.length();
        pos--;
      } break;
      case '>': {
        const SpecArgument a = specArgument(opt, pos + 1);
        _columnSpecifiers[lpos.size()] = specAtom(a);
        // array's `>{\centering\arraybackslash}p{3cm}`: the declaration that
        // aligns the lines of the column's paragraphs.
        const int col = static_cast<int>(lpos.size());
        if (a.text.find("\\centering") != string::npos) {
          _lineAligns[col] = Alignment::center;
        } else if (a.text.find("\\raggedleft") != string::npos) {
          _lineAligns[col] = Alignment::right;
        } else if (a.text.find("\\raggedright") != string::npos) {
          _lineAligns[col] = Alignment::left;
        }
        // A font switch sets the column's cells in that font.
        const auto has = [&](const string& name) {
          for (size_t at = a.text.find(name); at != string::npos; at = a.text.find(name, at + 1)) {
            const size_t next = at + name.size();
            if (next >= a.text.size() || !isalpha(static_cast<unsigned char>(a.text[next]))) {
              return true;
            }
          }
          return false;
        };
        for (const auto& [name, style] : {pair<const char*, const char*>{"\\bfseries", "bf"},
                                         {"\\bf", "bf"}, {"\\itshape", "it"}, {"\\it", "it"},
                                         {"\\ttfamily", "tt"}, {"\\tt", "tt"},
                                         {"\\sffamily", "sf"}, {"\\sf", "sf"},
                                         {"\\rmfamily", "rm"}, {"\\rm", "rm"}}) {
          if (has(name)) {
            _colFonts[col] = FontContext::mainFontStyleOf(style);
            break;
          }
        }
        // "scriptscriptstyle" holds "scriptstyle": the longer name first.
        if (a.text.find("\\displaystyle") != string::npos) {
          _colStyles[col] = TexStyle::display;
        } else if (a.text.find("\\textstyle") != string::npos) {
          _colStyles[col] = TexStyle::text;
        } else if (a.text.find("\\scriptscriptstyle") != string::npos) {
          _colStyles[col] = TexStyle::scriptScript;
        } else if (a.text.find("\\scriptstyle") != string::npos) {
          _colStyles[col] = TexStyle::script;
        }
        pos = a.end - 1;
      } break;
      case 'p':
      case 'm':
      case 'b': {
        // A fixed-width column: `p{3cm}`. The content is wrapped to that
        // measure in createBoxInner() instead of sizing to itself, which
        // is the only way a wide table can reflow rather than overflow.
        //
        // LaTeX's m{} and b{} differ from p{} only in how the cell sits
        // vertically against its neighbours; the row builder already
        // aligns on the tallest box, so all three parse the same here.
        // A width is not a nested construct, so the closing brace is
        // found by a plain scan rather than by running the parser.
        if (pos + 1 < len && opt[pos + 1] == '{') {
          const int start = pos + 2;
          int close = start;
          while (close < len && opt[close] != '}') close++;
          if (close < len) {
            const auto dimen = Units::getDimen(opt.substr(start, close - start));
            // An unreadable width leaves the column content-sized rather
            // than collapsing it to zero.
            if (dimen.isValid()) _colWidths[lpos.size()] = dimen;
            pos = close;
          }
        }
        lpos.push_back(Alignment::left);
      } break;
      case ' ':
      case '\t': break;
      default: {
        int spos = len + 1;
        bool hasrep = false;
        while (--spos > pos) {
          auto it = _colspeReplacement.find(opt.substr(pos, spos - pos));
          if (it != _colspeReplacement.end()) {
            if (++replacements > maxReplacements || opt.size() + it->second.size() > maxLength) {
              throw runaway();
            }
            hasrep = true;
            opt.insert(spos, it->second);
            len = opt.length();
            pos = spos - 1;
            break;
          }
        }
        if (!hasrep) {
          // Notify an error instead of using a default alignment
          throw ex_parse("Invalid alignment in array environment!");
        }
      } break;
    }
    pos++;
  }

  for (size_t j = lpos.size(); j < _matrix->cols(); j++) {
    lpos.push_back(Alignment::center);
  }

  if (lpos.empty()) lpos.push_back(Alignment::center);

  // A paragraph column's alignment is its lines': a cell that is one short
  // line sits in the column as the lines of a long one do. (In an l, c or
  // r column, whose cell is no paragraph, \centering does nothing.)
  for (auto it = _lineAligns.begin(); it != _lineAligns.end();) {
    const int col = it->first;
    const bool paragraph = _colWidths.count(col) > 0 ||
                           std::find(_fillCols.begin(), _fillCols.end(), col) != _fillCols.end();
    if (!paragraph || col >= static_cast<int>(lpos.size())) {
      it = _lineAligns.erase(it);
      continue;
    }
    lpos[col] = it->second;
    ++it;
  }
}

Alignment MatrixAtom::lineAlign(int col) const {
  const auto it = _lineAligns.find(col);
  return it == _lineAligns.end() ? Alignment::left : it->second;
}

vector<float> MatrixAtom::getColumnSep(Env& env, float width) {
  const int cols = _matrix->cols();
  vector<float> arr(cols + 1, 0.f);
  sptr<Box> Align, AlignSep, Hsep;
  float h, w = env.textWidth();
  int i = 0;

  // The numbers' room, at both sides to keep the display centred; a flalign
  // is flush left, so only at its right.
  if (w != POS_INF) w = max(w - (_matType == MatrixType::flAlign ? 1 : 2) * _tagReserve, 0.f);
  if (_matType == MatrixType::aligned || _matType == MatrixType::alignedAt) w = POS_INF;

  switch (_matType) {
    case MatrixType::array: {
      // Array: (hsep_col/2 or 0) elem hsep_col elem hsep_col ... hsep_col elem (hsep_col/2 or 0)
      Hsep = (_colSep.isValid() ? SpaceAtom(_colSep.unit, _colSep.val, 0.f, 0.f) : _hsep).createBox(env);
      for (int i = 0; i < cols; i++) {
        if (_position[i] == Alignment::none) {
          arr[i] = arr[i + 1] = 0;
          i++;
        } else {
          arr[i] = Hsep->_width;
        }
      }
      if (_spaceAround) {
        const auto half = Hsep->_width / 2;
        if (_position.front() != Alignment::none) arr[0] = half;
        if (_position.back() != Alignment::none) arr[cols] = half;
      }
      return arr;
    }
    case MatrixType::matrix:
    case MatrixType::smallMatrix: {
      // Simple matrix: 0 elem hsep_col elem hsep_col ... hsep_col elem 0
      arr[0] = 0;
      arr[cols] = arr[0];
      Hsep = _hsep.createBox(env);
      // smallmatrix's columns are a \thickspace (5mu of the text around it, not of the script style its cells are set in)
      const float gap = _matType == MatrixType::smallMatrix ? 5.f / 18.f * _baseEm : Hsep->_width;
      for (i = 1; i < cols; i++) arr[i] = gap;
      return arr;
    }
    case MatrixType::aligned:
    case MatrixType::align: {
      // Align env: hsep = (textwidth - matwidth) / (2n + 1)
      // Spaces: hsep eq_left \medskip eq_right hsep ... hsep elem hsep
      Align = _align.createBox(env);
      if (w != POS_INF) {
        h = max((w - width - cols / 2 * Align->_width) / floor((cols + 3) / 2.f), 0.f);
        AlignSep = sptrOf<StrutBox>(h, 0.f, 0.f, 0.f);
      } else {
        AlignSep = _hsep.createBox(env);
      }

      arr[cols] = AlignSep->_width;
      for (int i = 0; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = AlignSep->_width;
        else
          arr[i] = Align->_width;
      }
    } break;
    case MatrixType::alignedAt:
    case MatrixType::alignAt: {
      // Aignat env: hsep = (textwidth - matwdith) / 2
      // Spaces: hsep elem ... elem hsep
      if (w != POS_INF)
        h = max((w - width) / 2, 0.f);
      else
        h = 0;
      Align = _align.createBox(env);
      arr[0] = h;
      arr[cols] = arr[0];
      for (int i = 1; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = 0;
        else
          arr[i] = Align->_width;
      }
    } break;
    case MatrixType::flAlign: {
      // flalgin env : hsep = (textwidth - matwidth) / (2n + 1)
      // Spaces: hsep eq_left \medskip el_right hsep ... hsep elem hsep
      Align = _align.createBox(env);
      if (w != POS_INF) {
        h = max((w - width - (cols / 2) * Align->_width) / floor((cols - 1) / 2.f), 0.f);
        AlignSep = sptrOf<StrutBox>(h, 0.f, 0.f, 0.f);
      } else {
        AlignSep = _hsep.createBox(env);
      }

      arr[0] = 0;
      arr[cols] = arr[0];
      for (int i = 1; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = AlignSep->_width;
        else
          arr[i] = Align->_width;
      }
    } break;
  }

  if (w == POS_INF) {
    arr[0] = 0;
    arr[cols] = arr[0];
  }

  return arr;
}

void MatrixAtom::recalculateLine(
  const int rows,
  vector<vector<sptr<Box>>>& boxarr,
  vector<sptr<Atom>>& multiRows,
  vector<float>& height,
  vector<float>& depth,
  float drt,
  float vspace
) {
  const size_t s = multiRows.size();
  for (size_t i = 0; i < s; i++) {
    auto* m = (MultiRowAtom*)multiRows[i].get();
    const int r = m->_i;
    const int c = m->_j;
    int n = m->_n;
    int skipped = 0;
    float h = 0;
    if (n < 0) {
      // Across from bottom to top
      int j = r;
      for (; j >= 0 && j > r + n; j--) {
        if (boxarr[j][0]->_type == AtomType::hline) {
          if (j == 0) break;
          h += drt;
          n--;
        } else {
          skipped++;
          h += height[j] + depth[j] + vspace;
        }
      }
      m->_i = ++j;
      auto tmp = boxarr[r][c];
      boxarr[r][c] = boxarr[j][c];
      boxarr[j][c] = tmp;
    } else {
      // Across from top to bottom
      for (int j = r; j < r + n && j < rows; j++) {
        if (boxarr[j][0]->_type == AtomType::hline) {
          if (j == rows - 1) break;
          h += drt;
          n++;
        } else {
          skipped++;
          h += height[j] + depth[j] + vspace;
        }
      }
    }
    // The rows it spans, which end at the table's edge.
    m->_n = n < 0 ? r - m->_i + 1 : std::min(r + n, rows) - r;
    auto b = boxarr[m->_i][m->_j];
    const float bh = b->_height + b->_depth + vspace;
    if (h > bh) {
      b->_height = (h - bh + vspace) / 2.f;
    } else if (h < bh) {
      const float ex = (bh - h) / skipped / 2.f;
      const int mr = m->_i + m->_n;
      for (int j = m->_i; j < mr; j++) {
        if (boxarr[j][0]->_type != AtomType::hline) {
          height[j] += ex;
          depth[j] += ex;
        }
      }
      b->_height = height[m->_i];
      b->_depth = bh - b->_height - vspace;
    }
    boxarr[m->_i][m->_j]->_type = AtomType::none;
  }
}

sptr<Box> MatrixAtom::generateMulticolumn(
  Env& env,
  const sptr<Box>& b,
  const vector<float>& hsep,
  const vector<float>& colWidth,
  int i,
  int j
) {
  float w = 0;
  auto* mca = (MulticolumnAtom*)(_matrix->_array[i][j].get());
  int k, n = mca->skipped();
  for (k = j; k < j + n - 1; k++) {
    w += colWidth[k] + hsep[k + 1];
    auto it = _vlines.find(k + 1);
    if (it != _vlines.end()) w += it->second->getWidth(env);
  }
  w += colWidth[k];

  if (mca->isNeedWidth() && mca->colWidth() <= PREC) {
    mca->setColWidth(w);
    return mca->createBox(env);
  }

  if (b->_width >= w) return b;

  return sptrOf<HBox>(b, w, mca->alignFromColumn() && j < static_cast<int>(_position.size()) ? _position[j] : mca->align());
}

MatrixAtom::MatrixAtom(
  bool isPartial,
  const sptr<ArrayFormula>& arr,
  const string& options,
  bool spaceAround
) {
  _matrix = arr;
  _matType = MatrixType::array;
  _isPartial = isPartial;
  _spaceAround = spaceAround;
  parsePositions(string(options), _position);
}

MatrixAtom::MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, const string& options) {
  _matrix = arr;
  _matType = MatrixType::array;
  _isPartial = isPartial;
  _spaceAround = false;
  parsePositions(string(options), _position);
}

MatrixAtom::MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, MatrixType type) {
  _matrix = arr;
  _matType = type;
  _isPartial = isPartial;
  _spaceAround = false;
  switch (type) {
    case MatrixType::matrix: _spacing = RowSpacing::strut; break;
    case MatrixType::smallMatrix: _spacing = RowSpacing::small; break;
    case MatrixType::array: break;
    default: _spacing = RowSpacing::jot;
  }

  const int cols = arr->cols();
  if (type != MatrixType::matrix && type != MatrixType::smallMatrix) {
    _position.resize(cols);
    for (size_t i = 0; i < cols; i += 2) {
      _position[i] = Alignment::right;
      if (i + 1 < cols) _position[i + 1] = Alignment::left;
    }
  } else {
    _position.resize(cols);
    for (size_t i = 0; i < cols; i++) _position[i] = Alignment::center;
  }
}

sptr<Box> DecimalAtom::createBox(Env& env) {
  const auto part = [&](const sptr<Atom>& atom) {
    return atom == nullptr ? sptr<Box>(sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f)) : RowAtom::withItalic(atom)->createBox(env);
  };
  _beforeBox = part(_before);
  _sepBox = part(_sep);
  _afterBox = part(_after);
  auto box = sptrOf<HBox>();
  box->add(_beforeBox);
  box->add(_sepBox);
  box->add(_afterBox);
  return box;
}

sptr<Box> MatrixAtom::cellBox(const sptr<Atom>& atom, int col, Env& env) const {
  sptr<Atom> content = RowAtom::withItalic(atom);
  const auto font = _colFonts.find(col);
  if (font != _colFonts.end()) content = sptrOf<FontStyleAtom>(font->second, false, content);
  const auto suffix = _colSuffixes.find(col);
  if (suffix != _colSuffixes.end()) {
    auto row = sptrOf<RowAtom>();
    row->add(content);
    row->add(suffix->second);
    content = row;
  }
  const auto style = _colStyles.find(col);
  if (style == _colStyles.end()) return content->createBox(env);
  return env.withStyle(style->second, [&](Env& styled) { return content->createBox(styled); });
}

void MatrixAtom::applyCell(WrapperBox& box, int i, int j) {
  // 1. apply column specifier
  const auto col = _columnSpecifiers.find(j);
  if (col != _columnSpecifiers.end()) {
    auto spe = col->second;
    RowAtom* p = nullptr;
    auto* r = dynamic_cast<RowAtom*>(spe.get());
    while (r != nullptr) {
      spe = r->getFirstAtom();
      p = r;
      r = dynamic_cast<RowAtom*>(spe.get());
    }
    if (p != nullptr) {
      for (size_t k = 0; k < p->size(); k++) {
        CellSpecifier* s = dynamic_cast<CellSpecifier*>(p->get(k).get());
        if (s != nullptr) {
          s->apply(box);
        }
      }
    }
  }
  // 2. apply row specifier
  const auto row = _matrix->_rowSpecifiers.find(i);
  if (row != _matrix->_rowSpecifiers.end()) {
    for (const auto& s : row->second) s->apply(box);
  }
  // 3. apply cell specifier
  const string key = ArrayFormula::cellKey(i, j);
  auto cell = _matrix->_cellSpecifiers.find(key);
  if (cell != _matrix->_cellSpecifiers.end()) {
    for (const auto& s : cell->second) s->apply(box);
  }
}

sptr<Box> MatrixAtom::createBoxInner(Env& env) {
  const int rows = _matrix->rows();
  const int cols = _matrix->cols();

  // Owned by value: cells are built below, and building one can throw.
  vector<float> lineDepth(rows, 0.f);
  vector<float> lineHeight(rows, 0.f);
  vector<float> colWidth(cols, 0.f);
  vector<vector<sptr<Box>>> boxarr(rows, vector<sptr<Box>>(cols));

  float matW = 0;
  const auto drt = env.ruleThickness();

  // multi-column & multi-row atoms
  vector<sptr<Atom>> multiCols;
  vector<sptr<Atom>> multiRows;
  // The cells that span rows (tabularray's `r=2`), by where they begin: the row and column, how many columns and
  // rows they cover, and where their text stands in them.
  struct RowSpan {
    int row, col, cols, rows;
    char valign;
  };
  vector<RowSpan> rowSpans;
  for (int i = 0; i < rows; i++) {
    lineDepth[i] = 0;
    lineHeight[i] = 0;
    const int size = _matrix->_array[i].size();
    for (int j = 0; j < cols; j++) {
      if (j >= size) {
        // If current row is not full-filled, fill the row with _nullbox
        for (int k = j; k < cols; k++) boxarr[i][k] = _nullbox;
        break;
      }

      sptr<Atom> atom = _matrix->_array[i][j];
      {
        // A p{} column is broken to its own measure just below, whatever
        // the global width is. Its cells therefore have to keep the
        // word-level runs the breaker needs: folded into one phrase they
        // would be unbreakable, and the column would silently size to its
        // content instead of wrapping.
        MergeTextGuard guard(_colWidths.find(j) != _colWidths.end());
        boxarr[i][j] = (atom == nullptr) ? _nullbox : cellBox(atom, j, env);
      }
      if (atom != nullptr && atom->_type == AtomType::interText) {
        boxarr[i][j]->_type = AtomType::interText;
      }

      // `p{len}`: wrap the cell to the requested measure, then pin it to
      // that width so the column is sized by the spec and not by its
      // content. BoxSplitter is the same breaker \\ and max_width use.
      const auto pw = _colWidths.find(j);
      // (A rule's row is the rule alone, whatever the first column is: `\hline` before a p{} cell.)
      if (pw != _colWidths.end() && atom != nullptr && boxarr[i][j]->_type != AtomType::hline) {
        const float w = Units::fsize(pw->second, env);
        if (w > 0) {
          auto cell = boxarr[i][j];
          auto [wasSplit, splitBox] = BoxSplitter::split(cell, w, env.baselineSkip());
          (void)wasSplit;
          boxarr[i][j] = alignedCell(splitBox, w, lineAlign(j));
        }
      }

      // A cell that spans rows is as tall as the rows it covers, not as tall as the first: it does not set the row's
      // height (the rows are made to hold it below).
      const auto* spanning = dynamic_cast<const MulticolumnAtom*>(atom.get());
      if (spanning != nullptr && spanning->rowSpan() > 1) {
        rowSpans.push_back({i, j, spanning->skipped(), std::min(spanning->rowSpan(), rows - i), spanning->valign()});
      } else if (boxarr[i][j]->_type != AtomType::multiRow) {
        // Find the highest line (row)
        lineDepth[i] = max(boxarr[i][j]->_depth, lineDepth[i]);
        lineHeight[i] = max(boxarr[i][j]->_height, lineHeight[i]);
      } else {
        auto* mra = (MultiRowAtom*)atom.get();
        mra->setRowColumn(i, j);
        multiRows.push_back(atom);
      }

      if (boxarr[i][j]->_type != AtomType::multiColumn) {
        // Find the widest column
        colWidth[j] = max(boxarr[i][j]->_width, colWidth[j]);
      } else {
        auto* mca = (MulticolumnAtom*)atom.get();
        mca->setRowColumn(i, j);
        multiCols.push_back(atom);
      }
    }
  }

  // A rule's row is known by its first cell. A column spec that begins with `@{...}` puts that material in front of
  // it in every row (stargazer's `@{\extracolsep{5pt}}lcc`), so the rule has to come back to the front, or the row
  // is set as one with text in it: a strut taller, for each \hline.
  for (int i = 0; i < rows; i++) {
    if (boxarr[i][0] == nullptr || boxarr[i][0]->_type == AtomType::hline) continue;
    for (int j = 1; j < cols; j++) {
      if (boxarr[i][j] != nullptr && boxarr[i][j]->_type == AtomType::hline) {
        std::swap(boxarr[i][0], boxarr[i][j]);
        // (the atoms too: a rule is drawn from its own atom)
        auto& cells = _matrix->_array[i];
        if (static_cast<int>(cells.size()) > j) std::swap(cells[0], cells[j]);
        break;
      }
    }
  }

  // dcolumn's D columns: the cells put in line at their separators, the part before flush right and the part after
  // flush left, so the column is as wide as the widest of each.
  for (int j = 0; j < cols; j++) {
    float before = 0.f, sep = 0.f, after = 0.f;
    bool any = false;
    for (int i = 0; i < rows; i++) {
      if (j >= static_cast<int>(_matrix->_array[i].size())) continue;
      const auto* d = dynamic_cast<const DecimalAtom*>(_matrix->_array[i][j].get());
      if (d == nullptr || d->_beforeBox == nullptr) continue;
      any = true;
      before = max(before, d->_beforeBox->_width);
      sep = max(sep, d->_sepBox->_width);
      after = max(after, d->_afterBox->_width);
    }
    if (!any) continue;
    for (int i = 0; i < rows; i++) {
      if (j >= static_cast<int>(_matrix->_array[i].size())) continue;
      const auto* d = dynamic_cast<const DecimalAtom*>(_matrix->_array[i][j].get());
      if (d == nullptr || d->_beforeBox == nullptr) continue;
      auto line = sptrOf<HBox>();
      line->add(StrutBox::create(before - d->_beforeBox->_width));
      line->add(d->_beforeBox);
      line->add(d->_sepBox);
      line->add(StrutBox::create(sep - d->_sepBox->_width));
      line->add(d->_afterBox);
      line->add(StrutBox::create(after - d->_afterBox->_width));
      boxarr[i][j] = line;
    }
    colWidth[j] = 0.f;
    for (int i = 0; i < rows; i++) {
      if (boxarr[i][j] != nullptr && boxarr[i][j]->_type != AtomType::multiColumn) colWidth[j] = max(colWidth[j], boxarr[i][j]->_width);
    }
  }

  // `X`: what the other columns and the space between them leave of the
  // text width, shared by the X columns. A column whose cells all fit its
  // share is left as it was -- a short list in a label keeps its own
  // width, which is what `hjust` places -- and what it does not use goes
  // to the others, as an HTML table shares its width. A cell wider than
  // its column's share is made again at that width -- so a list or a table
  // inside it fits in turn -- with the word-level runs the breaker needs,
  // and broken to it as p{} is.
  // What the table is given to fill: its own width, which a width that is
  // the measure leaves out where there is no measure -- the table is as wide
  // as it is -- or else the measure.
  float given = env.textWidth();
  if (_tableWidth.isValid() &&
      !(_tableWidth.unit == UnitType::tw && env.textWidth() == POS_INF)) {
    given = Units::fsize(_tableWidth, env);
  }
  if (!_fillCols.empty() && given != POS_INF) {
    float used = 0;
    for (int j = 0; j < cols; j++) {
      if (std::find(_fillCols.begin(), _fillCols.end(), j) == _fillCols.end()) {
        used += colWidth[j];
      }
    }
    const vector<float> sep = getColumnSep(env, used);
    for (int j = 0; j <= cols; j++) {
      used += sep[j];
      const auto it = _vlines.find(j);
      if (it != _vlines.end()) used += it->second->getWidth(env);
    }
    float avail = given - used;
    vector<int> wide;
    for (const int j : _fillCols) {
      if (j < cols) wide.push_back(j);
    }
    // Those that fit an equal share keep their width; the share of the
    // rest grows by what they left, until none more fit. In a tabularx, which
    // has a width of its own, the X columns all take an equal share of it.
    const bool fills = _tableWidth.isValid() && !_spread;
    for (bool fitted = !fills; fitted && !wide.empty();) {
      fitted = false;
      const float share = avail / static_cast<float>(wide.size());
      for (auto it = wide.begin(); it != wide.end();) {
        if (colWidth[*it] <= share) {
          avail -= colWidth[*it];
          it = wide.erase(it);
          fitted = true;
        } else {
          ++it;
        }
      }
    }
    const float xw = wide.empty() ? 0.f : avail / static_cast<float>(wide.size());
    if (xw > 0) {
      for (int i = 0; i < rows; i++) {
        const int size = _matrix->_array[i].size();
        bool made = false;
        for (const int j : wide) {
          if (j >= size) continue;
          const sptr<Atom>& atom = _matrix->_array[i][j];
          if (atom == nullptr || boxarr[i][j]->_type != AtomType::none) continue;
          if (boxarr[i][j]->_width <= xw) continue;
          sptr<Box> cell;
          {
            MergeTextGuard guard(true);
            cell = env.withTextWidth(xw, [&](Env& e) { return cellBox(atom, j, e); });
          }
          const auto [wasSplit, splitBox] = BoxSplitter::split(cell, xw, env.baselineSkip());
          (void)wasSplit;
          boxarr[i][j] = alignedCell(splitBox, xw, lineAlign(j));
          made = true;
        }
        if (!made) continue;
        lineHeight[i] = lineDepth[i] = 0;
        for (int j = 0; j < cols; j++) {
          if (boxarr[i][j] == nullptr || boxarr[i][j]->_type == AtomType::multiRow) continue;
          lineHeight[i] = max(boxarr[i][j]->_height, lineHeight[i]);
          lineDepth[i] = max(boxarr[i][j]->_depth, lineDepth[i]);
        }
      }
      for (const int j : wide) colWidth[j] = fills ? xw : min(colWidth[j], xw);
    }
  }

  // A caption or a note of the table is set to the width of the table, which it does not make wider: wrapped
  // there (to the left, as a wrapped caption is, a short one in the middle).
  {
    float tableWidth = -1.f;
    for (const auto& a : multiCols) {
      auto* mca = static_cast<MulticolumnAtom*>(a.get());
      if (!mca->wrapToTable()) continue;
      if (tableWidth < 0.f) {
        // The columns as they will be: a cell that spans columns (a centred column's cell is one of one) is not in
        // colWidth yet, and widens what it spans below, as it does after this.
        vector<float> width(colWidth.begin(), colWidth.begin() + cols);
        float natural = 0.f;
        for (const float w : width) natural += w;
        const vector<float> between = getColumnSep(env, natural);
        for (const auto& b : multiCols) {
          const auto* m = static_cast<const MulticolumnAtom*>(b.get());
          if (m->wrapToTable() || boxarr[m->row()][m->col()] == nullptr) continue;
          const int first = m->col(), last = std::min(cols, first + m->skipped()) - 1;
          float spanned = 0.f;
          for (int j = first; j <= last; j++) spanned += width[j] + (j > first ? between[j] : 0.f);
          const float more = boxarr[m->row()][first]->_width - spanned;
          if (more > 0.f) {
            for (int j = first; j <= last; j++) width[j] += more / static_cast<float>(last - first + 1);
          }
        }
        natural = 0.f;
        for (const float w : width) natural += w;
        const vector<float> edge = getColumnSep(env, natural);
        tableWidth = natural;
        for (int j = 0; j <= cols; j++) {
          tableWidth += edge[j];
          const auto it = _vlines.find(j);
          if (it != _vlines.end()) tableWidth += it->second->getWidth(env);
        }
      }
      const int r = mca->row(), c = mca->col();
      // As a paragraph the width of the table sets it: on one line, in the place its alignment says, when it
      // fits; else broken to the width, flush left (a hanging atom, as a caption's or a note's, hangs).
      MergeTextGuard guard(true);
      const sptr<Atom> text = mca->content() == nullptr ? sptr<Atom>(sptrOf<EmptyAtom>()) : mca->content();
      const sptr<Box> oneLine = env.withTextWidth(POS_INF, [&](Env& e) { return cellBox(text, c, e); });
      sptr<Box> wrapped;
      if (oneLine->_width <= tableWidth) {
        wrapped = alignedCell(oneLine, tableWidth, mca->align());
      } else {
        const sptr<Box> cell = env.withTextWidth(tableWidth, [&](Env& e) { return cellBox(text, c, e); });
        const auto [wasSplit, splitBox] = BoxSplitter::split(cell, tableWidth, env.baselineSkip());
        (void)wasSplit;
        // A word that does not fit stays on its line. LaTeX hyphenates one it knows how to, which is not done here.
        if (mca->report && splitBox->_width > tableWidth + 0.5f) {
          mca->report("a word of the caption or note is wider than the table: it is not hyphenated, so its line is wider");
        }
        wrapped = alignedCell(splitBox, tableWidth, Alignment::left);
      }
      wrapped->_type = AtomType::multiColumn;
      boxarr[r][c] = wrapped;
      lineHeight[r] = lineDepth[r] = 0;
      for (int j = 0; j < cols; j++) {
        if (boxarr[r][j] == nullptr || boxarr[r][j]->_type == AtomType::multiRow) continue;
        lineHeight[r] = max(boxarr[r][j]->_height, lineHeight[r]);
        lineDepth[r] = max(boxarr[r][j]->_depth, lineDepth[r]);
      }
    }
  }

  // TeX's rows (see RowSpacing). An array gives every row a strut. The
  // alignments of amsmath and smallmatrix set a baselineskip between the
  // baselines; where the rows are too tall for it, a lineskip between them
  // (TeX's rule: a lineskip when the baselineskip leaves less than
  // \lineskiplimit). `skipBelow` is what goes under each row, by the rows' own
  // depth and height, before a `\\[len]` is added to it.
  vector<float> skipBelow(rows, 0.f);
  if (_texRows) {
    const auto isRule = [&](int i) { return boxarr[i][0]->_type == AtomType::hline; };
    if (_spacing == RowSpacing::strut) {
      const float strut = 1.2f * _baseEm * _matrix->_stretch;
      // A caption or a note (wrapToTable) is a paragraph of the table's width, not a row of cells: it has no strut,
      // its first line is as tall as its text, and the lines of one such paragraph and the next are a baselineskip apart.
      const auto paragraphRow = [&](int i) {
        for (std::size_t j = 0; j < _matrix->_array[i].size(); j++) {
          const auto* m = dynamic_cast<const MulticolumnAtom*>(_matrix->_array[i][j].get());
          if (m != nullptr && m->wrapToTable()) return true;
        }
        return false;
      };
      for (int i = 0; i + 1 < rows; i++) {
        if (!isRule(i) && !isRule(i + 1) && paragraphRow(i) && paragraphRow(i + 1)) skipBelow[i] = -1.f;  // set below
      }
      for (int i = 0; i < rows; i++) {
        if (isRule(i)) continue;
        const bool paragraph = paragraphRow(i);
        // A paragraph in a cell (p{}, X) ends in a strut of the row's, as array's \@finalstrut makes it: the last
        // line is at least 0.3 of a baselineskip deep, so the rule under it stands that far below its text.
        for (int j = 0; !paragraph && j < cols; j++) {
          const auto& cell = boxarr[i][j];
          if (cell == nullptr || cell->_type == AtomType::multiRow) continue;
          // The paragraph is the one box with size in what wraps it (an HBox of it and a strut).
          const sptr<Box>* inner = &cell;
          while (const auto* h = dynamic_cast<const HBox*>(inner->get())) {
            const sptr<Box>* only = nullptr;
            int sized = 0;
            for (const auto& child : h->_children) {
              if (child->_height + child->_depth <= 0.f) continue;
              only = &child;
              sized++;
            }
            if (sized != 1) break;
            inner = only;
          }
          const auto* lines = dynamic_cast<const VBox*>(inner->get());
          // A paragraph (the lines a p{} or X cell was broken into), not a fraction or a stack.
          if (lines == nullptr || !(lines->_lines || lines->_rows) || lines->_children.size() < 2) continue;
          const float extra = std::max(0.f, 0.3f * strut - lines->_children.back()->_depth);
          cell->_depth += extra;
          lineDepth[i] = max(lineDepth[i], cell->_depth);
        }
        if (!paragraph) {
          const float extraRow = _matrix->_extraRow.isValid() ? Units::fsize(_matrix->_extraRow, env) : 0.f;
          lineHeight[i] = max(lineHeight[i], 0.7f * strut + extraRow);
          lineDepth[i] = max(lineDepth[i], 0.3f * strut);
        }
        // tabularray's abovesep and belowsep: inside the row, outside its strut.
        const auto sep = _matrix->_rowSeps.find(i);
        if (sep != _matrix->_rowSeps.end()) {
          if (sep->second.first.isValid()) lineHeight[i] += Units::fsize(sep->second.first, env);
          if (sep->second.second.isValid()) lineDepth[i] += Units::fsize(sep->second.second, env);
        }
      }
      for (int i = 0; i + 1 < rows; i++) {
        if (skipBelow[i] < 0.f) skipBelow[i] = env.interline(lineDepth[i], lineHeight[i + 1]);
      }
    } else {
      const bool jot = _spacing == RowSpacing::jot;
      float baselineskip = (jot ? 1.5f : 0.6f) * _baseEm;
      if (_spacing == RowSpacing::sub) {
        const auto& math = env.mathConsts();
        baselineskip = (math.stackTopShiftUp() + math.fractionDenominatorShiftDown()) * env.scale();
      }
      const float lineskip = (jot ? 0.4f : 0.15f) * _baseEm;
      const float lineskiplimit = (jot ? 0.3f : 0.15f) * _baseEm;
      // amsmath's alignments are \halign rows that each begin with a \strut@ (0.7 and 0.3 of the 12pt baselineskip of a 10pt
      // page), so the first row stands that high and the last that deep whatever they hold (LuaLaTeX: `aligned` of one row
      // is 8.5pt above its baseline and 3.5 below, as an array's is). The spacing between rows is unchanged: with the strut
      // a row is never shorter than the baselineskip leaves room for.
      if (jot) {
        const float strut = 1.2f * _baseEm;
        for (int i = 0; i < rows; i++) {
          if (isRule(i)) continue;
          lineHeight[i] = max(lineHeight[i], 0.7f * strut);
          lineDepth[i] = max(lineDepth[i], 0.3f * strut);
        }
      }
      for (int i = 0; i + 1 < rows; i++) {
        if (isRule(i) || isRule(i + 1)) continue;
        const float glue = baselineskip - lineDepth[i] - lineHeight[i + 1];
        skipBelow[i] = glue >= lineskiplimit ? glue : lineskip;
      }
    }
  }

  // `\\[len]`: extra space below a row, added to its depth as LaTeX's array
  // adds it, so the rows after it move down and vertical rules run through.
  for (const auto& [row, gap] : _matrix->_rowGaps) {
    if (row >= 0 && row < rows) lineDepth[row] += Units::fsize(gap, env);
  }

  for (int j = 0; j < cols; j++) matW += colWidth[j];

  // An equation's number or \tag, at the right of its row. The columns of
  // an align are spaced across what the numbers leave, at both sides, so
  // that the display stays centred on the page and clear of them.
  vector<sptr<Box>> tags(rows);
  float tagWidth = 0;
  for (const auto& [row, tag] : _matrix->_rowTags) {
    if (row < 0 || row >= rows || tag == nullptr) continue;
    tags[row] = tag->createBox(env);
    tagWidth = max(tagWidth, tags[row]->_width);
  }
  const float tagGap = tagWidth > 0 ? SpaceAtom(UnitType::em, 2.f, 0.f, 0.f).createBox(env)->_width : 0.f;
  _tagReserve = tagWidth > 0 ? tagWidth + tagGap : 0.f;
  const bool centred = _matType == MatrixType::align || _matType == MatrixType::alignAt;
  const float leftShift = centred && env.textWidth() != POS_INF ? _tagReserve : 0.f;
  // A display of align's kind (`equation` is one row of align): what TeX centres is the formula, the number apart; the
  // matrix is as wide as the line, spread across it, and the formula is not.
  if (centred) {
    gDisplayFormulaWidth = matW;
    gDisplayNumberWidth = tagWidth;
  }

  // The horizontal separator's width
  vector<float> Hsep = getColumnSep(env, matW);
  for (const auto& [col, len] : _extraColSep) {
    const float extra = SpaceAtom(len.unit, len.val, 0.f, 0.f).createBox(env)->_width;
    for (int j = col + 2; j < cols; j++) {
      if (_position[j - 1] != Alignment::none && _position[j] != Alignment::none) Hsep[j] += extra;
    }
  }
  // tabular*'s \extracolsep{\fill}: what its width has left over goes
  // between the columns.
  if (_spread && given != POS_INF && cols > 1) {
    float natural = matW;
    for (const float s : Hsep) natural += s;
    for (const auto& [col, vline] : _vlines) natural += vline->getWidth(env);
    // Only between the columns of the table: an @{...} is no column of its
    // text, and the gap beside it is not for stretching.
    vector<int> gaps;
    for (int j = 1; j < cols; j++) {
      if (_position[j - 1] != Alignment::none && _position[j] != Alignment::none) gaps.push_back(j);
    }
    if (given > natural && !gaps.empty()) {
      const float extra = (given - natural) / static_cast<float>(gaps.size());
      for (const int j : gaps) Hsep[j] += extra;
    }
  }

  for (auto& i : multiCols) {
    auto* multi = (MulticolumnAtom*)i.get();
    if (multi->wrapToTable()) continue;  // as wide as the table, not wider
    const int c = multi->col(), r = multi->row(), n = multi->skipped();
    float w = 0;
    int j = 0;
    for (j = c; j < c + n - 1; j++) w += colWidth[j] + Hsep[j + 1];
    w += colWidth[j];
    if (boxarr[r][c]->_width > w) {
      // If the multi-column's width > the total width of the acrossed columns,
      // add an extra-space to each column
      matW += boxarr[r][c]->_width - w;
      const float extraW = (boxarr[r][c]->_width - w) / n;
      for (int k = c; k < c + n; k++) colWidth[k] += extraW;
    }
  }

  // Add separator's space to the matrix width
  for (int j = 0; j < cols + 1; j++) {
    matW += Hsep[j];
    auto it = _vlines.find(j);
    if (it != _vlines.end()) matW += it->second->getWidth(env);
  }

  auto Vsep = _vsep_in.createBox(env);
  // \arraystretch: each row's pitch grows by that much more of a baselineskip.
  if (!_texRows && _matrix->_stretch != 1.f) {
    Vsep->_height += (_matrix->_stretch - 1.f) * 1.2f * Units::fsize(UnitType::em, 1.f, env);
  }
  // The space above and below each row: half the separator at each, or, in
  // TeX's rows, none above and what goes between the rows below.
  vector<float> padTop(rows, _texRows ? 0.f : Vsep->_height / 2);
  vector<float> padBot(rows, _texRows ? 0.f : Vsep->_height / 2);
  if (_texRows) padBot = skipBelow;
  // Recalculate the height of the row
  recalculateLine(rows, boxarr, multiRows, lineHeight, lineDepth, drt, _texRows ? 0.f : Vsep->_height);

  // The rows a spanning cell covers have to hold it: what they lack, the last of them gets.
  const auto isRuleRow = [&](int r) { return boxarr[r][0] != nullptr && boxarr[r][0]->_type == AtomType::hline; };
  for (const auto& s : rowSpans) {
    float have = 0.f;
    int last = s.row;
    for (int r = s.row; r < s.row + s.rows; r++) {
      have += isRuleRow(r) ? drt : lineHeight[r] + padTop[r] + lineDepth[r] + padBot[r];
      if (!isRuleRow(r)) last = r;
    }
    const float need = boxarr[s.row][s.col]->_height + boxarr[s.row][s.col]->_depth;
    if (need > have) lineDepth[last] += need - have;
  }
  // A cell that is under a spanning one is part of it: it has nothing of its own to fill (a row's colour, say).
  const auto underSpan = [&](int r, int c) {
    for (const auto& s : rowSpans) {
      if (r > s.row && r < s.row + s.rows && c >= s.col && c < s.col + s.cols) return true;
    }
    return false;
  };
  vector<float> rowH(rows, 0.f);
  struct PendingSpan {
    sptr<WrapperBox> wb;
    RowSpan span;
    float height, depth;
  };
  vector<PendingSpan> pendingSpans;

  auto vb = sptrOf<VBox>();
  float totalHeight = 0;
  float firstBaseline = 0, lastBaseline = 0;
  bool seenRow = false;

  for (int i = 0; i < rows; i++) {
    auto hb = sptrOf<HBox>();
    const auto first = boxarr[i][0]->_type;
    if (leftShift > 0 && (first == AtomType::none || first == AtomType::multiColumn)) {
      hb->add(sptrOf<StrutBox>(leftShift, 0.f, 0.f, 0.f));
    }
    for (int j = 0; j < cols; j++) {
      switch (boxarr[i][j]->_type) {
        case AtomType::none:
        case AtomType::multiColumn: {
          if (j == 0) {
            auto it = _vlines.find(0);
            if (it != _vlines.end()) {
              auto vat = it->second;
              vat->_height = lineHeight[i] + lineDepth[i] + padTop[i] + padBot[i];
              vat->_shift = lineDepth[i] + padBot[i];
              vat->_row = i;
              auto vatBox = vat->createBox(env);
              hb->add(vatBox);
            }
          }

          bool isLastVline = true;
          bool flush = false;

          sptr<WrapperBox> wb;
          int tj = j;
          float l = j == 0 ? Hsep[j] : Hsep[j] / 2;
          if (boxarr[i][j]->_type == AtomType::none) {
            wb = sptrOf<WrapperBox>(
              boxarr[i][j],
              colWidth[j],
              lineHeight[i],
              lineDepth[i],
              _position[j]  //
            );
          } else {
            auto* matom = (MulticolumnAtom*)_matrix->_array[i][j].get();
            // A caption or note was set to the table's width already, edge to edge.
            flush = matom->wrapToTable();
            auto b = flush ? boxarr[i][j] : generateMulticolumn(env, boxarr[i][j], Hsep, colWidth, i, j);
            j += matom->skipped() - 1;
            wb = sptrOf<WrapperBox>(b, b->_width, lineHeight[i], lineDepth[i], Alignment::left);
            isLastVline = matom->hasRightVline();
          }
          float r = j == cols - 1 ? Hsep[j + 1] : Hsep[j + 1] / 2;
          if (flush) l = r = 0.f;
          wb->addInsets(l, padTop[i], r, padBot[i]);
          // What is set for the cell (a colour, a column's specifier) is set for the cell that begins the span: a
          // spanning cell is the one at its first column (`j` is its last by now).
          if (!underSpan(i, tj)) applyCell(*wb, i, tj);
          // A cell that spans rows is made to cover them once the rows are all set (below).
          for (const auto& s : rowSpans) {
            if (s.row == i && s.col == tj) {
              pendingSpans.push_back({wb, s, boxarr[i][tj]->_height, boxarr[i][tj]->_depth});
            }
          }
          // (a cell of one row, told to stand in the middle or at the bottom of a row taller than its text)
          if (const auto* one = tj < static_cast<int>(_matrix->_array[i].size())
                                  ? dynamic_cast<const MulticolumnAtom*>(_matrix->_array[i][tj].get())
                                  : nullptr;
              one != nullptr && one->rowSpan() == 1 && (one->valign() == 'm' || one->valign() == 'b' || one->valign() == 'f')) {
            pendingSpans.push_back({wb, {i, tj, one->skipped(), 1, one->valign()}, boxarr[i][tj]->_height, boxarr[i][tj]->_depth});
          }
          boxarr[i][tj] = wb;
          hb->add(wb);

          auto it = _vlines.find(j + 1);
          if (isLastVline && it != _vlines.end()) {
            auto vat = it->second;
            vat->_height = lineHeight[i] + lineDepth[i] + padTop[i] + padBot[i];
            vat->_shift = lineDepth[i] + padBot[i];
              vat->_row = i;
            auto vatBox = vat->createBox(env);
            hb->add(vatBox);
          }
        } break;

        case AtomType::interText: {
          float f = env.textWidth();
          f = f == POS_INF ? colWidth[j] : f;
          hb = sptrOf<HBox>(boxarr[i][j], f, Alignment::left);
          j = cols;
        } break;

        case AtomType::hline: {
          auto* at = (HlineAtom*)_matrix->_array[i][j].get();
          if (isTransparent(at->ruleColor())) at->setColor(LINE_COLOR);   // its own colour, from when it was made
          if (i >= 1 && dynamic_cast<HlineAtom*>(_matrix->_array[i - 1][j].get()) != nullptr) {
            hb->add(sptrOf<StrutBox>(0.f, 2 * drt, 0.f, 0.f));
          }
          if (!at->pieces().empty() && cols > 0) {
            // A rule of several pieces (tabularray's hline{2}={1-2,5}): each from the left edge of its
            // first cell to the right edge of its last, less what leftpos / rightpos take off the
            // padding on that side, all in this one row.
            float done = 0;
            for (const auto& p : at->pieces()) {
              const int a = std::min(std::max(p.from, 0), cols - 1);
              const int b = std::min(std::max(p.to, a), cols - 1);
              float left = 0;
              if (a > 0) {
                for (int c = 0; c < a; c++) left += colWidth[c] + Hsep[c];
                left += Hsep[a] / 2;
              }
              float right = 0;
              for (int c = 0; c <= b; c++) right += colWidth[c] + Hsep[c];
              right += b == cols - 1 ? Hsep[cols] : Hsep[b + 1] / 2;
              const float leftPad = a == 0 ? Hsep[0] : Hsep[a] / 2;
              const float rightPad = b == cols - 1 ? Hsep[cols] : Hsep[b + 1] / 2;
              if (p.leftpos < 0) left += -p.leftpos * leftPad;
              if (p.rightpos < 0) right -= -p.rightpos * rightPad;
              if (right <= left || left < done) continue;
              if (left > done) hb->add(sptrOf<StrutBox>(left - done, 0.f, 0.f, 0.f));
              at->setWidth(right - left);
              hb->add(at->createBox(env));
              done = right;
            }
            j = cols;
            break;
          }
          if (at->colStart() >= 0 && cols > 0) {
            // Partial rule: \cline{a-b}. Span left edge of column a to
            // right edge of column b (LaTeX 1-indexed columns are
            // converted to 0-indexed by the macro). Both are kept inside
            // the table: \cline{2-2} on one column read past its widths.
            int a = at->colStart();
            int b = at->colEnd();
            if (a < 0) a = 0;
            if (a >= cols) a = cols - 1;
            if (b >= cols) b = cols - 1;
            if (b < a) b = a;
            float offset = 0;
            for (int c = 0; c < a; c++) offset += colWidth[c];
            for (int c = 0; c <= a; c++) offset += Hsep[c];
            float width = 0;
            for (int c = a; c <= b; c++) width += colWidth[c];
            for (int c = a + 1; c <= b; c++) width += Hsep[c];
            if (offset > 0) hb->add(sptrOf<StrutBox>(offset, 0.f, 0.f, 0.f));
            at->setWidth(width);
          } else {
            at->setWidth(matW);
          }

          hb->add(at->createBox(env));
          j = cols;
        } break;
        default: {
        }
      }
    }

    // The number goes flush right at the measure, or after the row, past a
    // gap, where there is none.
    if (tags[i] != nullptr && (first == AtomType::none || first == AtomType::multiColumn)) {
      const float measure = env.textWidth() == POS_INF ? 0.f : env.textWidth();
      const float right = max(measure, leftShift + matW + tagGap + tagWidth);
      hb->add(sptrOf<StrutBox>(right - leftShift - matW - tags[i]->_width, 0.f, 0.f, 0.f));
      hb->add(tags[i]);
    }

    if (boxarr[i][0]->_type != AtomType::hline) {
      hb->_height = lineHeight[i] + padTop[i];
      hb->_depth = lineDepth[i] + padBot[i];
    }
    // Where the baseline of the first and of the last row stand, from the top.
    if (boxarr[i][0]->_type != AtomType::hline) {
      const float at = vb->_height + vb->_depth + hb->_height;
      if (!seenRow) firstBaseline = at;
      lastBaseline = at;
      seenRow = true;
    }
    rowH[i] = hb->_height + hb->_depth;
    vb->add(hb);
  }

  // Each cell that spans rows now fills the rows it covers and stands in them where its `valign` says.
  for (const auto& p : pendingSpans) {
    float whole = 0.f;
    for (int r = p.span.row; r < p.span.row + p.span.rows; r++) whole += rowH[r];
    const auto sep = [&](int r, bool above) {
      const auto it = _matrix->_rowSeps.find(r);
      if (it == _matrix->_rowSeps.end()) return 0.f;
      const Dimen& d = above ? it->second.first : it->second.second;
      return d.isValid() ? Units::fsize(d, env) : 0.f;
    };
    const float topEdge = sep(p.span.row, true);
    const float bottomEdge = whole - sep(p.span.row + p.span.rows - 1, false);
    const float baseline = lineHeight[p.span.row] + padTop[p.span.row];
    float shift = 0.f;
    if (p.span.valign == 'm') {
      shift = (topEdge + bottomEdge) / 2.f - (baseline + (p.depth - p.height) / 2.f);
    } else if (p.span.valign == 'b' || p.span.valign == 'f') {
      shift = bottomEdge - p.depth - baseline;
    }
    p.wb->extend(whole - rowH[p.span.row], shift);
  }

  totalHeight = vb->_height + vb->_depth;

  const auto axis = _matType == MatrixType::smallMatrix ? _baseAxis : env.axisHeight();
  if (_matrix->_vpos != 'c' && seenRow) {
    // tabular[t] and [b]: the baseline of the first row or of the last.
    const float baseline = _matrix->_vpos == 't' ? firstBaseline : lastBaseline;
    vb->_height = baseline;
    vb->_depth = totalHeight - baseline;
    return vb;
  }
  vb->_height = totalHeight / 2 + axis;
  vb->_depth = totalHeight / 2 - axis;

  return vb;
}

sptr<Box> MatrixAtom::createBox(Env& env) {
  sptr<Box> box = createBoxStyled(env);
  if (_flow) {
    if (const auto rows = std::dynamic_pointer_cast<VBox>(box)) rows->_flow = true;
  }
  return box;
}

sptr<Box> MatrixAtom::createBoxStyled(Env& env) {
  _baseEm = env.em();
  _baseAxis = env.axisHeight();
  for (const auto& [col, style] : _matrix->_vlineStyles) {
    const auto it = _vlines.find(col);
    if (it != _vlines.end()) it->second->setStyle(style);
  }
  _texRows = _spacing == RowSpacing::sub ||
             (_spacing != RowSpacing::content && env.style() < TexStyle::script);
  if (_matType == MatrixType::smallMatrix) {
    return env.withStyle(TexStyle::script, [this](auto& script) { return createBoxInner(script); });
  }
  // The cells of an array or matrix are `$...$`, which TeX begins in text
  // style, whatever the display around the matrix is.
  if (_spacing == RowSpacing::strut && env.style() < TexStyle::text) {
    const TexStyle text = env.style() == TexStyle::display ? TexStyle::text : TexStyle::text1;
    return env.withStyle(text, [this](auto& inner) { return createBoxInner(inner); });
  }
  return createBoxInner(env);
}

/*************************************** multicolumn atoms ****************************************/

Alignment MulticolumnAtom::parseAlign(const string& str) {
  int pos = 0;
  int len = str.length();
  Alignment align = Alignment::center;
  bool first = true;
  while (pos < len) {
    char c = str[pos];
    switch (c) {
      case 'l': {
        align = Alignment::left;
        first = false;
      } break;
      case 'r': {
        align = Alignment::right;
        first = false;
      } break;
      case 'c': {
        align = Alignment::center;
        first = false;
      } break;
      case '|': {
        if (first) {
          _beforeVlines = 1;
        } else {
          _afterVlines = 1;
        }
        while (++pos < len) {
          c = str[pos];
          if (c != '|') {
            pos--;
            break;
          } else {
            if (first)
              _beforeVlines++;
            else
              _afterVlines++;
          }
        }
      } break;
    }
    pos++;
  }
  return align;
}

sptr<Box> MulticolumnAtom::createBox(Env& env) {
  auto atom = _cols == nullptr ? sptrOf<EmptyAtom>() : _cols;
  sptr<Box> b = _width == 0 ? atom->createBox(env)
                            : sptrOf<HBox>(atom->createBox(env), _width, _align);
  b->_type = AtomType::multiColumn;
  return b;
}

sptr<Box> HdotsforAtom::createBox(float space, const sptr<Box>& b, Env& env) {
  auto sb = sptrOf<StrutBox>(0.f, space, 0.f, 0.f);
  auto vb = sptrOf<VBox>();
  vb->add(sb);
  vb->add(b);
  vb->add(sb);
  vb->_type = AtomType::multiColumn;
  return vb;
}

sptr<Box> HdotsforAtom::createBox(Env& env) {
  auto dot = _cols->createBox(env);
  float space = Glue::getSpace(SpaceType::thinMuSkip, env) * _coeff * 2;

  // If no width specified, create a box with one dot
  if (_width == 0) return createBox(space, dot, env);

  float x = (_width - dot->_width) / (space + dot->_width);
  int count = (int)floor(x);

  // Only one dot can be placed in
  if (count == 0) {
    auto b = sptrOf<HBox>(dot, _width, Alignment::center);
    return createBox(space, b, env);
  }

  // Adjust the space between
  space += (x - count) * space / count;
  auto sb = sptrOf<StrutBox>(space, 0.f, 0.f, 0.f);
  auto b = sptrOf<HBox>();
  for (int i = 0; i < count; i++) {
    b->add(dot);
    b->add(sb);
  }
  b->add(dot);

  auto hb = sptrOf<HBox>(b, _width, Alignment::center);
  return createBox(space, hb, env);
}

float VlineAtom::getWidth(Env& env) const {
  const float drt = _thickness.isValid() ? Units::fsize(_thickness, env) : env.ruleThickness();
  return _n == 0 ? 0 : drt * (3 * _n - 2);
}

sptr<Box> VlineAtom::createBox(Env& env) {
  if (_n == 0) return StrutBox::empty();
  // A row the rule does not run through keeps the room it takes.
  if (!_rows.empty() && !(_row >= 0 && static_cast<std::size_t>(_row) < _rows.size() && _rows[_row])) {
    return sptrOf<StrutBox>(getWidth(env), 0.f, 0.f, 0.f);
  }

  const float drt = _thickness.isValid() ? Units::fsize(_thickness, env) : env.ruleThickness();
  auto rb = sptrOf<RuleBox>(_height, drt, _shift, _hasColour ? _colour : MatrixAtom::LINE_COLOR, true);
  if (_dashed) rb->setDash(Units::fsize(UnitType::em, 0.35f, env));
  auto sep = sptrOf<StrutBox>(2 * drt, 0.f, 0.f, 0.f);
  auto hb = sptrOf<HBox>();
  for (int i = 0; i < _n - 1; i++) {
    hb->add(rb);
    hb->add(sep);
  }
  if (_n > 0) hb->add(rb);
  return hb;
}

SpaceAtom MultlineAtom::_vsep_in(UnitType::ex, 0.f, 1.f, 0.f);

sptr<Box> MultlineAtom::createBox(Env& env) {
  float tw = env.textWidth();
  if (tw == POS_INF || _lineType == MultiLineType::gathered) {
    // amsmath's gathered: one centred column and no padding at its sides (an array column has \arraycolsep at each)
    MatrixAtom lines(_isPartial, _column, MatrixType::matrix);
    lines.setRowSpacing(RowSpacing::jot);
    return lines.createBox(env);
  }

  auto vb = sptrOf<VBox>();
  // An empty line's cell is null, or not there at all.
  const auto cell = [&](size_t row) -> sptr<Atom> {
    const auto& cells = _column->_array[row];
    return !cells.empty() && cells[0] != nullptr ? cells[0] : sptrOf<EmptyAtom>();
  };
  auto atom = cell(0);
  Alignment alignment = _lineType == MultiLineType::gather ? Alignment::center : Alignment::left;
  if (atom->_alignment != Alignment::none) alignment = atom->_alignment;

  // `\\[len]`: extra space below a row.
  const auto gapAfter = [&](size_t row) {
    const auto it = _column->_rowGaps.find(static_cast<int>(row));
    if (it != _column->_rowGaps.end()) {
      vb->add(sptrOf<StrutBox>(0.f, Units::fsize(it->second, env), 0.f, 0.f));
    }
  };

  // A line of the measure's width, with the row's number or \tag flush at
  // its right end.
  const auto line = [&](size_t row, const sptr<Atom>& a, Alignment al) -> sptr<Box> {
    const sptr<Box> formula = a->createBox(env);
    auto box = sptrOf<HBox>(formula, tw, al);
    const auto tag = _column->_rowTags.find(static_cast<int>(row));
    if (tag == _column->_rowTags.end() || tag->second == nullptr) return box;
    auto number = tag->second->createBox(env);
    // (The number, and the formula it belongs to, for the display skips: see DisplayAtom.)
    gDisplayFormulaWidth = formula->_width;
    gDisplayNumberWidth = number->_width;
    auto both = sptrOf<HBox>();
    both->add(box);
    both->add(sptrOf<StrutBox>(-number->_width, 0.f, 0.f, 0.f));
    both->add(number);
    return both;
  };

  auto above = line(0, atom, alignment);
  vb->add(above);
  gapAfter(0);
  auto Vsep = _vsep_in.createBox(env);
  // What goes between two lines: amsmath's (RowSpacing::jot), except in a
  // script, where it is the engine's.
  const bool texRows = env.style() < TexStyle::script;
  const auto between = [&](const sptr<Box>& upper, const sptr<Box>& lower) -> sptr<Box> {
    if (!texRows) return Vsep;
    const float em = env.em();
    const float glue = 1.5f * em - upper->_depth - lower->_height;
    return sptrOf<StrutBox>(0.f, glue >= 0.3f * em ? glue : 0.4f * em, 0.f, 0.f);
  };
  for (size_t i = 1; i < _column->rows() - 1; i++) {
    atom = cell(i);
    alignment = Alignment::center;
    if (atom->_alignment != Alignment::none) alignment = atom->_alignment;
    auto below = line(i, atom, alignment);
    vb->add(between(above, below));
    vb->add(below);
    above = below;
    gapAfter(i);
  }

  if (_column->rows() > 1) {
    atom = cell(_column->rows() - 1);
    alignment = _lineType == MultiLineType::gather ? Alignment::center : Alignment::right;
    if (atom->_alignment != Alignment::none) alignment = atom->_alignment;
    auto below = line(_column->rows() - 1, atom, alignment);
    vb->add(between(above, below));
    vb->add(below);
  }

  float h = vb->_height + vb->_depth;
  vb->_height = h / 2;
  vb->_depth = h / 2;

  return vb;
}
