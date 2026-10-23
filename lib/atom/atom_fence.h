#ifndef MICROTEX_ATOM_FENCE_H
#define MICROTEX_ATOM_FENCE_H

#include "atom/atom_char.h"
#include "box/box_single.h"

namespace microtex {

/**
 * An atom representing a middle atom which must be rounded by a left and right
 * delimiter.
 */
class MiddleAtom : public Atom {
private:
  std::string _sym;

public:
  const sptr<Box> _placeholder;
  float _height = 0.f;

  MiddleAtom() = delete;

  /** @throw ex_parse if `sym` is not a delimiter ("" and "." are none) */
  explicit MiddleAtom(std::string sym);

  sptr<Box> createBox(Env& env) override;
};

/**
 * An atom representing a base atom surrounded with delimiters that change their
 * size according to the height of the base
 */
class FencedAtom : public Atom {
private:
  // base atom
  sptr<Atom> _base;
  // delimiters
  std::string _l, _r;
  std::vector<sptr<MiddleAtom>> _m;
  // a null delimiter (empty or "."), as TeX's \left. is, is \nulldelimiterspace wide
  bool _nullSpace = true;
  // a generalized fraction's delimiters are of TeX's delim1 / delim2 size (2.39em in display style, 1.01em in
  // text), whatever the fraction's height, and the fraction stays where it is
  bool _fractionDelims = false;

public:
  /** @throw ex_parse if `l` or `r` is not a delimiter ("" and "." are none) */
  FencedAtom(const sptr<Atom>& b, std::string l, std::string r);

  FencedAtom(const sptr<Atom>& b, std::string l, std::string r, std::vector<sptr<MiddleAtom>> m);

  /** Whether a null delimiter takes room; a fraction's fence leaves that to the fraction. */
  void setNullSpace(bool on) { _nullSpace = on; }

  void setFractionDelimiters(bool on) { _fractionDelims = on; }

  AtomType leftType() const override { return AtomType::inner; }

  AtomType rightType() const override { return AtomType::inner; }

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_FENCE_H
