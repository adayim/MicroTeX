#ifndef MICROTEX_MACRO_COLORS_H
#define MICROTEX_MACRO_COLORS_H

#include "atom/atom_basic.h"
#include "atom/atom_box.h"
#include "atom/atom_misc.h"
#include "macro/macro_decl.h"
#include "utils/utf.h"

namespace microtex {

inline cmdmacro(fgcolor) {
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, TRANSPARENT, ColorAtom::getColor(args.text(1)));
}

inline cmdmacro(bgcolor) {
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, ColorAtom::getColor(args.text(1)), TRANSPARENT);
}

inline cmdmacro(textcolor) {
  // Inherit the surrounding mode — \textcolor in a math expression
  // must still parse its body as math so `\textcolor{red}{c^2}` keeps
  // the superscript, matching LaTeX's xcolor semantics where
  // \textcolor only changes colour, not mode.
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, TRANSPARENT, ColorAtom::getColor(args.text(1)));
}

inline cmdmacro(colorbox) {
  color c = ColorAtom::getColor(args.text(1));
  return sptrOf<FBoxAtom>(args.formula(2, args.isMathMode()), c, c);
}

inline cmdmacro(fcolorbox) {
  color f = ColorAtom::getColor(args.text(2));
  color b = ColorAtom::getColor(args.text(1));
  return sptrOf<FBoxAtom>(args.formula(3, args.isMathMode()), f, b);
}

cmdmacro(definecolor);

}  // namespace microtex

#endif  // MICROTEX_MACRO_COLORS_H
