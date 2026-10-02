#ifndef MICROTEX_ATOM_MATRIX_H
#define MICROTEX_ATOM_MATRIX_H

#include "atom/atom.h"
#include "atom/atom_char.h"
#include "atom/atom_row.h"
#include "atom/atom_space.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "env/units.h"
#include "graphic/font_style.h"

namespace microtex {

class ArrayFormula;

/** Atom to justify cells in array */
class CellSpecifier : public Atom {
public:
  virtual void apply(WrapperBox& box) = 0;

  sptr<Box> createBox(Env& env) override { return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f); }
};

/** Atom representing column color in array */
class CellColorAtom : public CellSpecifier {
private:
  color _color;

public:
  CellColorAtom() = delete;

  explicit CellColorAtom(color c) : _color(c) {}

  void apply(WrapperBox& box) override { box.setBackground(_color); }
};

/** Atom representing column foreground in array */
class CellForegroundAtom : public CellSpecifier {
private:
  color _color;

public:
  CellForegroundAtom() = delete;

  explicit CellForegroundAtom(color c) : _color(c) {}

  void apply(WrapperBox& box) override { box.setForeground(_color); }
};

class VlineAtom;

enum class MatrixType : i8 {
  array,
  matrix,
  align,
  alignAt,
  flAlign,
  smallMatrix,
  aligned,
  alignedAt
};

/** Atom represents matrix */
class MatrixAtom : public Atom {
private:
  static std::map<std::string, std::string> _colspeReplacement;

  static SpaceAtom _align;

  sptr<ArrayFormula> _matrix;
  std::vector<Alignment> _position;
  std::map<int, sptr<VlineAtom>> _vlines;
  std::map<int, sptr<Atom>> _columnSpecifiers;
  // Fixed column widths from `p{len}` / `m{len}` / `b{len}`. A column
  // listed here is wrapped to that measure instead of sizing to content.
  std::map<int, Dimen> _colWidths;
  // `X` columns (tabularx's): they share the text width the other columns
  // leave, and wrap to it as p{} does. Without a text width they are `l`.
  std::vector<int> _fillCols;
  // How the lines of a paragraph column (p{}, X) are aligned, from a
  // `>{\centering}` or `>{\raggedleft}` before it; left when not here.
  std::map<int, Alignment> _lineAligns;
  // The style a column's cells are set in, from a `>{\displaystyle}` (or
  // \textstyle, \scriptstyle, \scriptscriptstyle) before it.
  std::map<int, TexStyle> _colStyles;
  // The font a column's cells are set in, from a `>{\bfseries}` before it, and
  // what follows each of them, from a `<{...}` after it.
  std::map<int, FontStyle> _colFonts;
  std::map<int, sptr<Atom>> _colSuffixes;

  MatrixType _matType;
  bool _isPartial;
  bool _spaceAround;
  // The room an equation's numbers take at each side of an align, so that
  // the display stays centred on the page and clear of them. Set by
  // createBoxInner() before the columns are spaced.
  float _tagReserve = 0;
  // A table's own width, from tabular*{w} and tabularx{w}: tabularx's X
  // columns share it, and tabular* spreads its columns across it.
  Dimen _tableWidth;
  bool _spread = false;

  void parsePositions(std::string opt, std::vector<Alignment>& lpos);

  Alignment lineAlign(int col) const;

  /** The box of a cell of column `col`, in the style the column asks for. */
  sptr<Box> cellBox(const sptr<Atom>& atom, int col, Env& env) const;

  sptr<Box> generateMulticolumn(
    Env& env,
    const sptr<Box>& b,
    const std::vector<float>& hsep,
    const std::vector<float>& colWidth,
    int i,
    int j
  );

  static void recalculateLine(
    int rows,
    std::vector<std::vector<sptr<Box>>>& boxarr,
    std::vector<sptr<Atom>>& multiRows,
    std::vector<float>& height,
    std::vector<float>& depth,
    float drt,
    float vspace
  );

  /** The space before each column and after the last: `cols + 1` values. */
  std::vector<float> getColumnSep(Env& env, float width);

  void applyCell(WrapperBox& box, int i, int j);

  sptr<Box> createBoxInner(Env& env);

public:
  // The color to draw the rule of the matrix
  static color LINE_COLOR;

  /** Forget the column types defined and the rule color set, for a new
   *  document. */
  static void resetDefinitions();

  static SpaceAtom _hsep, _semihsep, _vsep_in, _vsep_ext_top, _vsep_ext_bot;

  static sptr<Box> _nullbox;

  MatrixAtom() = delete;

  MatrixAtom(
    bool isPartial,
    const sptr<ArrayFormula>& arr,
    const std::string& options,
    bool spaceAround
  );

  MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, const std::string& options);

  MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, MatrixType type);

  sptr<Box> createBox(Env& env) override;

  /** The width of `tabular*` (`spread`: its columns take up what the text
   *  leaves of it) or `tabularx` (its X columns do). */
  void setTableWidth(const Dimen& width, bool spread) {
    _tableWidth = width;
    _spread = spread;
  }

  static void defineColumnSpecifier(const std::string& rep, const std::string& spe);
};

/** An atom representing vertical-line in matrix Env */
class VlineAtom : public Atom {
private:
  // Number of lines to draw
  int _n;

public:
  float _height, _shift;
  // Dashes, not a line (the `:` of a column specification).
  bool _dashed = false;

  VlineAtom() = delete;

  explicit VlineAtom(int n) : _n(n), _height(0), _shift(0) {}

  float getWidth(Env& env) const;

  sptr<Box> createBox(Env& env) override;
};

/** An atom used in array mode that across several columns */
class MulticolumnAtom : public Atom {
protected:
  // Number of columns across
  int _n;
  Alignment _align;
  float _width;
  int _beforeVlines, _afterVlines;
  int _row, _col;
  sptr<Atom> _cols;

  Alignment parseAlign(const std::string& str);

public:
  MulticolumnAtom() = delete;

  MulticolumnAtom(int n, const std::string& align, const sptr<Atom>& cols)
      : _width(0), _beforeVlines(0), _afterVlines(0), _row(0), _col(0) {
    _n = n >= 1 ? n : 1;
    _cols = cols;
    _align = parseAlign(align);
  }

  virtual bool isNeedWidth() const { return false; }

  inline void setColWidth(float w) { _width = w; }

  inline float colWidth() const { return _width; }

  inline int skipped() const { return _n; }

  inline bool hasRightVline() const { return _afterVlines != 0; }

  inline void setRowColumn(int i, int j) {
    _row = i;
    _col = j;
  }

  inline Alignment align() { return _align; }

  inline int row() const { return _row; }

  inline int col() const { return _col; }

  sptr<Box> createBox(Env& env) override;
};

/** An atom used in array mode representing "dots" */
class HdotsforAtom : public MulticolumnAtom {
private:
  float _coeff;

  static sptr<Box> createBox(float space, const sptr<Box>& b, Env& env);

public:
  HdotsforAtom() = delete;

  HdotsforAtom(int n, float coeff)
      : MulticolumnAtom(n, "c", sptrOf<CharAtom>('.', true)), _coeff(coeff) {}

  bool isNeedWidth() const override { return true; }

  sptr<Box> createBox(Env& env) override;
};

/** Atom representing multi-row */
class MultiRowAtom : public Atom {
private:
  sptr<Atom> _rows;

public:
  int _i, _j, _n;

  MultiRowAtom() = delete;

  // An argument that could not be built is null: an empty cell then. The
  // row count is kept where the row arithmetic (`r + n`, abs()) cannot
  // overflow; a table has far fewer rows than that.
  MultiRowAtom(int n, const std::string& option, const sptr<Atom>& rows)
      : _rows(rows != nullptr ? rows : sptrOf<RowAtom>()),
        _i(0),
        _j(0),
        _n(n == 0 ? 1 : std::max(-(1 << 20), std::min(n, 1 << 20))) {}

  inline void setRowColumn(int r, int c) {
    _i = r;
    _j = c;
  }

  sptr<Box> createBox(Env& env) override {
    auto b = _rows->createBox(env);
    b->_type = AtomType::multiRow;
    return b;
  }
};

enum class MultiLineType { multiline, gather, gathered };

/** An atom representing a vertical row of other atoms */
class MultlineAtom : public Atom {
private:
  static SpaceAtom _vsep_in;
  sptr<ArrayFormula> _column;
  MultiLineType _lineType;
  bool _isPartial;

public:
  MultlineAtom() = delete;

  MultlineAtom(bool isPartial, const sptr<ArrayFormula>& col, MultiLineType type) {
    _isPartial = isPartial;
    _lineType = type;
    _column = col;
  }

  MultlineAtom(const sptr<ArrayFormula>& col, MultiLineType type) {
    _isPartial = false;
    _lineType = type;
    _column = col;
  }

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_MATRIX_H
