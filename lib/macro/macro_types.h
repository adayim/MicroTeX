#ifndef MICROTEX_MACRO_TYPES_H
#define MICROTEX_MACRO_TYPES_H

#include "atom/atom_basic.h"
#include "macro/macro_decl.h"

namespace microtex {

inline sptr<Atom> _math_type(CommandArgs& args, AtomType type) {
  return sptrOf<TypedAtom>(type, type, args.formula(1));
}

inline cmdmacro(mathop) {
  auto a = _math_type(args, AtomType::bigOperator);
  a->_limitsType = LimitsType::noLimits;
  return a;
}

inline cmdmacro(mathpunct) {
  return _math_type(args, AtomType::punctuation);
}

inline cmdmacro(mathord) {
  return _math_type(args, AtomType::ordinary);
}

inline cmdmacro(mathrel) {
  return _math_type(args, AtomType::relation);
}

inline cmdmacro(mathinner) {
  return _math_type(args, AtomType::inner);
}

inline cmdmacro(mathbin) {
  return _math_type(args, AtomType::binaryOperator);
}

inline cmdmacro(mathopen) {
  return _math_type(args, AtomType::opening);
}

inline cmdmacro(mathclose) {
  return _math_type(args, AtomType::closing);
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_TYPES_H
