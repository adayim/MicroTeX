#ifndef MICROTEX_MACRO_DELIMS_H
#define MICROTEX_MACRO_DELIMS_H

#include "atom/atom.h"
#include "atom/atom_basic.h"
#include "atom/atom_delim.h"
#include "atom/atom_fence.h"
#include "atom/atom_misc.h"
#include "atom/atom_root.h"
#include "macro/macro_decl.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

namespace microtex {

inline cmdmacro(overdelim) {
  const auto& name = args.text(0);
  const auto& base = args.formula(1);
  return sptrOf<OverUnderDelimiter>(base, name, true);
}

inline cmdmacro(underdelim) {
  const auto& name = args.text(0);
  const auto& base = args.formula(1);
  return sptrOf<OverUnderDelimiter>(base, name, false);
}

cmdmacro(xarrow);

inline cmdmacro(overline) {
  return sptrOf<OverUnderBar>(args.formula(1), true);
}

// A box in both modes, as LaTeX's: in text it underlines text, in the
// text's own font.
inline cmdmacro(underline) {
  return sptrOf<OverUnderBar>(args.formula(1, args.isMathMode()), false);
}

inline cmdmacro(Braket) {
  std::string str(args.text(1));
  replaceAll(str, "\\|", "\\middle\\vert ");
  return args.formulaOf("\\left\\langle " + str + "\\right\\rangle");
}

inline cmdmacro(Set) {
  std::string str(args.text(1));
  replaceFirst(str, "\\|", "\\middle\\vert ");
  return args.formulaOf("\\left\\{" + str + "\\right\\}");
}

inline cmdmacro(middle) {
  return sptrOf<MiddleAtom>(args.text(1));
}

inline cmdmacro(sqrt) {
  if (args.text(2).empty()) return sptrOf<NthRoot>(args.formula(1), nullptr);
  return sptrOf<NthRoot>(args.formula(1), args.formula(2));
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_DELIMS_H
