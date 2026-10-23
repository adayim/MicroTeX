#ifndef MICROTEX_ATOM_FRAC_H
#define MICROTEX_ATOM_FRAC_H

#include "atom/atom.h"
#include "env/units.h"

namespace microtex {

/** An atom representing a fraction */
class FracAtom : public Atom {
private:
  Dimen _thickness;
  Alignment _numAlign = Alignment::center, _dnomAlign = Alignment::center;
  // the atoms representing the numerator and denominator
  sptr<Atom> _num, _dnom;
  bool _rule = false;
  // TeX sets a null delimiter, \nulldelimiterspace wide, where a generalized
  // fraction has no delimiter: both sides of \frac, \over, \atop... A side
  // that has a real delimiter (see fence()) is the delimiter's.
  bool _nullLeft = true, _nullRight = true;

  static inline Alignment checkAlign(Alignment align) {
    if (align == Alignment::left || align == Alignment::right) return align;
    return Alignment::center;
  }

public:
  FracAtom(const sptr<Atom>& num, const sptr<Atom>& den, bool rule, const Dimen& thickness = {});

  FracAtom(
    const sptr<Atom>& num,
    const sptr<Atom>& den,
    Alignment numAlign = Alignment::center,
    Alignment denAlign = Alignment::center
  );

  /** `frac` between the delimiters `l` and `r` (empty and "." are none, and a null one is
   *  \nulldelimiterspace wide), as \binom and \genfrac set it.
   *  @throw ex_parse if `l` or `r` is not a delimiter */
  static sptr<Atom> fence(const sptr<FracAtom>& frac, const std::string& l, const std::string& r);

  AtomType leftType() const override { return AtomType::inner; }

  AtomType rightType() const override { return AtomType::inner; }

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_FRAC_H
