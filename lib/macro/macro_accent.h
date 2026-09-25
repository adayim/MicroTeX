#ifndef MICROTEX_MACRO_ACCENT_H
#define MICROTEX_MACRO_ACCENT_H

#include "atom/atom_accent.h"
#include "atom/atom_basic.h"
#include "atom/atom_stack.h"
#include "macro/macro_decl.h"
#include "utils/utf.h"

namespace microtex {

inline cmdmacro(accentset) {
  if (args.text(1) == "")
    return args.formula(2);
  return sptrOf<AccentedAtom>(args.formula(2), args.text(1).substr(1), false, true);
}

inline cmdmacro(stack) {
  const auto& over = StackArgs::autoSpace(args.formula(1));
  const auto& under = StackArgs::autoSpace(args.formula(3));
  return sptrOf<StackAtom>(args.formula(2), over, under);
}

inline cmdmacro(stackrel) {
  const auto& stack = macro_stack(args);
  return sptrOf<TypedAtom>(AtomType::relation, AtomType::relation, stack);
}

inline cmdmacro(stackbin) {
  const auto& stack = macro_stack(args);
  return sptrOf<TypedAtom>(AtomType::binaryOperator, AtomType::binaryOperator, stack);
}

inline cmdmacro(overset) {
  const auto& over = StackArgs::autoSpace(args.formula(1));
  sptr<Atom> a = sptrOf<StackAtom>(args.formula(2), over, true);
  return sptrOf<TypedAtom>(AtomType::relation, AtomType::relation, a);
}

inline cmdmacro(underset) {
  const auto& under = StackArgs::autoSpace(args.formula(1));
  auto a = sptrOf<StackAtom>(args.formula(2), under, false);
  return sptrOf<TypedAtom>(AtomType::relation, AtomType::relation, a);
}

inline cmdmacro(underaccent) {
  const StackArgs under{args.formula(1), UnitType::mu, 1.f, true};
  auto a = sptrOf<StackAtom>(args.formula(2), under, false);
  a->setAdjustBottom(true);
  return a;
}

cmdmacro(accentbiss);

cmdmacro(accents);

cmdmacro(undertilde);

}  // namespace microtex

#endif  // MICROTEX_MACRO_ACCENT_H
