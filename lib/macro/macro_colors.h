#ifndef MICROTEX_MACRO_COLORS_H
#define MICROTEX_MACRO_COLORS_H

#include "atom/atom_basic.h"
#include "atom/atom_box.h"
#include "atom/atom_misc.h"
#include "macro/macro_decl.h"
#include "utils/utf.h"

namespace microtex {

inline macro(fgcolor) {
  auto a = Formula(tp, args[2], false, tp.isMathMode())._root;
  return sptrOf<ColorAtom>(a, TRANSPARENT, ColorAtom::getColor(args[1]));
}

inline macro(bgcolor) {
  auto a = Formula(tp, args[2], false, tp.isMathMode())._root;
  return sptrOf<ColorAtom>(a, ColorAtom::getColor(args[1]), TRANSPARENT);
}

inline macro(textcolor) {
  // Inherit the surrounding mode — \textcolor in a math expression
  // must still parse its body as math so `\textcolor{red}{c^2}` keeps
  // the superscript, matching LaTeX's xcolor semantics where
  // \textcolor only changes colour, not mode.
  auto a = Formula(tp, args[2], false, tp.isMathMode())._root;
  return sptrOf<ColorAtom>(a, TRANSPARENT, ColorAtom::getColor(args[1]));
}

inline macro(colorbox) {
  color c = ColorAtom::getColor(args[1]);
  return sptrOf<FBoxAtom>(Formula(tp, args[2], false, tp.isMathMode())._root, c, c);
}

inline macro(fcolorbox) {
  color f = ColorAtom::getColor(args[2]);
  color b = ColorAtom::getColor(args[1]);
  return sptrOf<FBoxAtom>(Formula(tp, args[3], false, tp.isMathMode())._root, f, b);
}

macro(definecolor);

}  // namespace microtex

#endif  // MICROTEX_MACRO_COLORS_H
