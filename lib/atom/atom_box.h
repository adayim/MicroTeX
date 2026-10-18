#ifndef MICROTEX_ATOM_BOX_H
#define MICROTEX_ATOM_BOX_H

#include "atom/atom.h"
#include "atom/atom_row.h"
#include "env/units.h"
#include "graphic/graphic_basic.h"

namespace microtex {

/** An atom representing a boxed base atom */
class FBoxAtom : public Atom {
protected:
  sptr<Atom> _base;
  color _bg, _line;
  // \fboxsep and \fboxrule as they stand when the atom is made -- the input's
  // lengths exist only while it is lowered -- and whether there is a frame:
  // \colorbox has none, only its colour.
  Dimen _sep, _rule;
  bool _frame;

public:
  FBoxAtom() = delete;

  explicit FBoxAtom(
    const sptr<Atom>& base,
    color bg = TRANSPARENT,
    color line = TRANSPARENT,
    bool frame = true
  )
      : _sep(Units::getDimen("\\fboxsep")), _rule(Units::getDimen("\\fboxrule")), _frame(frame) {
    if (!_sep.isValid()) _sep = 0.3_em;
    if (!_rule.isValid()) _rule = {0.04f, UnitType::em};
    if (base == nullptr)
      _base = sptrOf<RowAtom>();
    else {
      _base = base;
      _type = base->_type;
    }
    _bg = bg;
    _line = line;
  }

  sptr<Box> createBox(Env& env) override;
};

/** An atom representing a boxed base atom */
class DoubleFramedAtom : public FBoxAtom {
public:
  DoubleFramedAtom() = delete;

  explicit DoubleFramedAtom(const sptr<Atom>& base) : FBoxAtom(base) {}

  sptr<Box> createBox(Env& env) override;
};

/** An atom representing a box-shadowed atom */
class ShadowAtom : public FBoxAtom {
public:
  ShadowAtom() = delete;

  explicit ShadowAtom(const sptr<Atom>& base) : FBoxAtom(base) {}

  sptr<Box> createBox(Env& env) override;
};

/**
 * An atom representing a oval-boxed base atom
 */
class OvalAtom : public FBoxAtom {
public:
  static float _multiplier;
  static float _diameter;

  OvalAtom() = delete;

  explicit OvalAtom(const sptr<Atom>& base) : FBoxAtom(base) {}

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_BOX_H
