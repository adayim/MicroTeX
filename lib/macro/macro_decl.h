#ifndef MICROTEX_MACRO_DECL_H
#define MICROTEX_MACRO_DECL_H

#include "atom/atom.h"
#include "atom/atom_basic.h"
#include "core/formula.h"
#include "macro/macro_args.h"

namespace microtex {

/** `a`, or an empty atom where an empty argument gave nullptr: for a handler
 *  whose atom has to have something to wrap, as the old parser's Formula did. */
inline sptr<Atom> orEmpty(const sptr<Atom>& a) {
  return a != nullptr ? a : sptrOf<EmptyAtom>();
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_DECL_H
