#include "atom/atom_matrix.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "atom/atom_basic.h"
#include "atom/atom_row.h"
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
  // clear first
  lpos.clear();
  _fillCols.clear();
  _lineAligns.clear();
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
      case '@': {
        const SpecArgument a = specArgument(opt, pos + 1);
        // Keep columns same with the matrix
        if (lpos.size() > _matrix->cols()) {
          lpos.resize(_matrix->cols());
        }
        _matrix->insertAtomIntoCol(lpos.size(), specAtom(a));

        lpos.push_back(Alignment::none);
        pos = a.end - 1;
      } break;
      case '*': {
        // *{n}{cols}: the columns n times over, read from where they end.
        const SpecArgument times = specArgument(opt, pos + 1);
        const SpecArgument cols = specArgument(opt, times.end);
        int nrep = 0;
        valueOf(times.text, nrep);
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

  if (_matType == MatrixType::aligned || _matType == MatrixType::alignedAt) w = POS_INF;

  switch (_matType) {
    case MatrixType::array: {
      // Array: (hsep_col/2 or 0) elem hsep_col elem hsep_col ... hsep_col elem (hsep_col/2 or 0)
      Hsep = _hsep.createBox(env);
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
      for (i = 1; i < cols; i++) arr[i] = Hsep->_width;
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
    m->_n = abs(n);
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

  return sptrOf<HBox>(b, w, mca->align());
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
  const string key = toString(i) + toString(j);
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
        boxarr[i][j] = (atom == nullptr) ? _nullbox : atom->createBox(env);
      }
      if (atom != nullptr && atom->_type == AtomType::interText) {
        boxarr[i][j]->_type = AtomType::interText;
      }

      // `p{len}`: wrap the cell to the requested measure, then pin it to
      // that width so the column is sized by the spec and not by its
      // content. BoxSplitter is the same breaker \\ and max_width use.
      const auto pw = _colWidths.find(j);
      if (pw != _colWidths.end() && atom != nullptr) {
        const float w = Units::fsize(pw->second, env);
        if (w > 0) {
          auto cell = boxarr[i][j];
          auto [wasSplit, splitBox] = BoxSplitter::split(cell, w, env.lineSpace());
          (void)wasSplit;
          boxarr[i][j] = alignedCell(splitBox, w, lineAlign(j));
        }
      }

      if (boxarr[i][j]->_type != AtomType::multiRow) {
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

  // `X`: what the other columns and the space between them leave of the
  // text width, shared by the X columns. A column whose cells all fit its
  // share is left as it was -- a short list in a label keeps its own
  // width, which is what `hjust` places -- and what it does not use goes
  // to the others, as an HTML table shares its width. A cell wider than
  // its column's share is made again at that width -- so a list or a table
  // inside it fits in turn -- with the word-level runs the breaker needs,
  // and broken to it as p{} is.
  if (!_fillCols.empty() && env.textWidth() != POS_INF) {
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
    float avail = env.textWidth() - used;
    vector<int> wide;
    for (const int j : _fillCols) {
      if (j < cols) wide.push_back(j);
    }
    // Those that fit an equal share keep their width; the share of the
    // rest grows by what they left, until none more fit.
    for (bool fitted = true; fitted && !wide.empty();) {
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
            cell = env.withTextWidth(xw, [&](Env& e) { return atom->createBox(e); });
          }
          const auto [wasSplit, splitBox] = BoxSplitter::split(cell, xw, env.lineSpace());
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
      for (const int j : wide) colWidth[j] = min(colWidth[j], xw);
    }
  }

  // `\\[len]`: extra space below a row, added to its depth as LaTeX's array
  // adds it, so the rows after it move down and vertical rules run through.
  for (const auto& [row, gap] : _matrix->_rowGaps) {
    if (row >= 0 && row < rows) lineDepth[row] += Units::fsize(gap, env);
  }

  for (int j = 0; j < cols; j++) matW += colWidth[j];

  // The horizontal separator's width
  const vector<float> Hsep = getColumnSep(env, matW);

  for (auto& i : multiCols) {
    auto* multi = (MulticolumnAtom*)i.get();
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
  // Recalculate the height of the row
  recalculateLine(rows, boxarr, multiRows, lineHeight, lineDepth, drt, Vsep->_height);

  auto vb = sptrOf<VBox>();
  float totalHeight = 0;
  float Vspace = Vsep->_height / 2;

  for (int i = 0; i < rows; i++) {
    auto hb = sptrOf<HBox>();
    for (int j = 0; j < cols; j++) {
      switch (boxarr[i][j]->_type) {
        case AtomType::none:
        case AtomType::multiColumn: {
          if (j == 0) {
            auto it = _vlines.find(0);
            if (it != _vlines.end()) {
              auto vat = it->second;
              vat->_height = lineHeight[i] + lineDepth[i] + Vsep->_height;
              vat->_shift = lineDepth[i] + Vspace;
              auto vatBox = vat->createBox(env);
              hb->add(vatBox);
            }
          }

          bool isLastVline = true;

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
            auto b = generateMulticolumn(env, boxarr[i][j], Hsep, colWidth, i, j);
            auto* matom = (MulticolumnAtom*)_matrix->_array[i][j].get();
            j += matom->skipped() - 1;
            wb = sptrOf<WrapperBox>(b, b->_width, lineHeight[i], lineDepth[i], Alignment::left);
            isLastVline = matom->hasRightVline();
          }
          float r = j == cols - 1 ? Hsep[j + 1] : Hsep[j + 1] / 2;
          wb->addInsets(l, Vspace, r, Vspace);
          applyCell(*wb, i, j);
          boxarr[i][tj] = wb;
          hb->add(wb);

          auto it = _vlines.find(j + 1);
          if (isLastVline && it != _vlines.end()) {
            auto vat = it->second;
            vat->_height = lineHeight[i] + lineDepth[i] + Vsep->_height;
            vat->_shift = lineDepth[i] + Vspace;
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
          at->setColor(LINE_COLOR);
          if (i >= 1 && dynamic_cast<HlineAtom*>(_matrix->_array[i - 1][j].get()) != nullptr) {
            hb->add(sptrOf<StrutBox>(0.f, 2 * drt, 0.f, 0.f));
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

    if (boxarr[i][0]->_type != AtomType::hline) {
      hb->_height = lineHeight[i] + Vspace;
      hb->_depth = lineDepth[i] + Vspace;
    }
    vb->add(hb);
  }

  totalHeight = vb->_height + vb->_depth;

  const auto axis = env.axisHeight();
  vb->_height = totalHeight / 2 + axis;
  vb->_depth = totalHeight / 2 - axis;

  return vb;
}

sptr<Box> MatrixAtom::createBox(Env& env) {
  if (_matType == MatrixType::smallMatrix) {
    return env.withStyle(TexStyle::script, [this](auto& script) { return createBoxInner(script); });
  } /* else if (_matType == MatrixType::matrix) {
    return env.withStyle(
      TexStyle::text,
      [this](auto& text) { return createBoxInner(text); }
    );
  }*/
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
  return _n == 0 ? 0 : env.ruleThickness() * (3 * _n - 2);
}

sptr<Box> VlineAtom::createBox(Env& env) {
  if (_n == 0) return StrutBox::empty();

  const auto drt = env.ruleThickness();
  auto rb = sptrOf<RuleBox>(_height, drt, _shift, MatrixAtom::LINE_COLOR, true);
  auto sep = sptrOf<StrutBox>(2 * drt, 0.f, 0.f, 0.f);
  auto hb = new HBox();
  for (int i = 0; i < _n - 1; i++) {
    hb->add(rb);
    hb->add(sep);
  }
  if (_n > 0) hb->add(rb);
  return sptr<Box>(hb);
}

SpaceAtom MultlineAtom::_vsep_in(UnitType::ex, 0.f, 1.f, 0.f);

sptr<Box> MultlineAtom::createBox(Env& env) {
  float tw = env.textWidth();
  if (tw == POS_INF || _lineType == MultiLineType::gathered)
    return MatrixAtom(_isPartial, _column, "").createBox(env);

  auto vb = sptrOf<VBox>();
  auto atom = _column->_array[0][0];
  Alignment alignment = _lineType == MultiLineType::gather ? Alignment::center : Alignment::left;
  if (atom->_alignment != Alignment::none) alignment = atom->_alignment;

  // `\\[len]`: extra space below a row.
  const auto gapAfter = [&](size_t row) {
    const auto it = _column->_rowGaps.find(static_cast<int>(row));
    if (it != _column->_rowGaps.end()) {
      vb->add(sptrOf<StrutBox>(0.f, Units::fsize(it->second, env), 0.f, 0.f));
    }
  };

  vb->add(sptrOf<HBox>(atom->createBox(env), tw, alignment));
  gapAfter(0);
  auto Vsep = _vsep_in.createBox(env);
  for (size_t i = 1; i < _column->rows() - 1; i++) {
    atom = _column->_array[i][0];
    alignment = Alignment::center;
    if (atom->_alignment != Alignment::none) alignment = atom->_alignment;
    vb->add(Vsep);
    vb->add(sptrOf<HBox>(atom->createBox(env), tw, alignment));
    gapAfter(i);
  }

  if (_column->rows() > 1) {
    atom = _column->_array[_column->rows() - 1][0];
    alignment = _lineType == MultiLineType::gather ? Alignment::center : Alignment::right;
    if (atom->_alignment != Alignment::none) alignment = atom->_alignment;
    vb->add(Vsep);
    vb->add(sptrOf<HBox>(atom->createBox(env), tw, alignment));
  }

  float h = vb->_height + vb->_depth;
  vb->_height = h / 2;
  vb->_depth = h / 2;

  return vb;
}
