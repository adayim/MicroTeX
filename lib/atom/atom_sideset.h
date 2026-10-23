#ifndef MICROTEX_ATOM_SIDESET_H
#define MICROTEX_ATOM_SIDESET_H

#include "atom/atom.h"

namespace microtex {

/** An atom representing scripts around of a base atom */
class SideSetsAtom : public Atom {
public:
  // _base is guaranteed to be valid
  sptr<Atom> _left, _right, _base;
  sptr<Atom> _under, _over;

  SideSetsAtom() = delete;

  SideSetsAtom(const sptr<Atom>& base, const sptr<Atom>& left, const sptr<Atom>& right);

  /** amsmath sets all of \sideset in display style, whatever the text around it is (it measures the operator
   *  `\displaystyle` and puts the scripts about that), so the big operator stands in running text too. */
  sptr<Box> createBox(Env& env) override;

  /** amsmath puts an empty `\hbox` before the `\mathop`: what comes before meets an ordinary atom. */
  AtomType leftType() const override { return AtomType::ordinary; }

private:
  sptr<Box> createBoxInStyle(Env& env);
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_SIDESET_H
