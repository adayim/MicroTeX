#ifndef MICROTEX_FORMULA_H
#define MICROTEX_FORMULA_H

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "atom/atom.h"
#include "env/units.h"

namespace microtex {

class MiddleAtom;
class VRowAtom;
class CellSpecifier;

/**
 * Represents a logical mathematical formula that will be displayed (by creating
 * a Render from it and painting it) using algorithms that are based
 * on the TeX algorithms.
 */
class Formula {
private:
  // predefined TeX formulas
  static std::map<std::string, std::string> _predefFormulaStrs;

  std::vector<sptr<MiddleAtom>> _middle;

public:
  // character-to-symbol mappings
  static const std::map<c32, std::string> _charToSymbol;

  // the root atom of the "atom tree" that represents the formula
  sptr<Atom> _root;

  /** What the input says of its fonts as a whole: the math font a
   *  \setmathfont named ("" for none), and the family indices (see
   *  font_family_atom.h) of the body, sans and typewriter fonts its preamble
   *  set. The render starts from them. */
  std::string _mathFontName;
  int _fontRoles[3] = {0, 0, 0};

  /** An empty Formula, for the front end (front/lower.h) to build into.
   *  Nothing here parses LaTeX any more: the front end does. */
  Formula() = default;

  const std::vector<sptr<MiddleAtom>>& middle();

  /** Inserts an atom at the end of the current formula. */
  Formula* add(const sptr<Atom>& a);

  /** Test if this formula is in array mode. */
  virtual bool isArrayMode() const { return false; }

  /**
   * Get a predefined Formula.
   *
   * @param name the name of the predefined Formula
   * @return the predefined Formula or nullptr if not found
   */
  static sptr<Formula> get(const std::string& name);

  /** Whether `name` is a predefined Formula, without parsing it. */
  static bool isPredefined(const std::string& name) {
    return _predefFormulaStrs.count(name) != 0;
  }

  virtual ~Formula() = default;
};

/** Represents a formula in array mode. */
class ArrayFormula : public Formula {
private:
  size_t _row, _col;

public:
  std::vector<std::vector<sptr<Atom>>> _array;
  std::map<int, std::vector<sptr<CellSpecifier>>> _rowSpecifiers;
  std::map<std::string, std::vector<sptr<CellSpecifier>>> _cellSpecifiers;
  /** `\\[len]`: extra space below a row, by row index. */
  std::map<int, Dimen> _rowGaps;
  /** An equation's number or `\tag`, by row index: drawn at the right of the
   *  row, at the right margin of the display. */
  std::map<int, sptr<Atom>> _rowTags;
  /** LaTeX's \arraystretch: the rows are this much further apart than their
   *  content makes them. */
  float _stretch = 1.f;

  ArrayFormula();

  void addCol();

  void addCol(int n);

  void insertAtomIntoCol(int col, const sptr<Atom>& atom);

  void addRow();

  void addRowSpecifier(const sptr<CellSpecifier>& spe);

  /** Extra space below the current row, as `\\[len]` asks for. */
  void addRowGap(const Dimen& gap);

  void addCellSpecifier(const sptr<CellSpecifier>& spe);

  int rows() const;

  int cols() const;

  sptr<VRowAtom> getAsVRow();

  void checkDimensions();

  bool isArrayMode() const override { return true; }

  ~ArrayFormula() override = default;
};

}  // namespace microtex

#endif  // MICROTEX_FORMULA_H
