#ifndef MICROTEX_MACRO_COLORS_H
#define MICROTEX_MACRO_COLORS_H

#include "atom/atom_basic.h"
#include "atom/atom_box.h"
#include "atom/atom_misc.h"
#include "macro/macro_decl.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

namespace microtex {

/** The colour an argument names, or TRANSPARENT -- no colour, so the colour
 *  around stays -- with a warning when it names none: xcolor stops with
 *  "Undefined color", and we recover. */
inline color colourArg(CommandArgs& args, std::string spec) {
  color c = TRANSPARENT;
  if (ColorAtom::parseColor(spec, c)) return c;
  trim(spec);
  args.warn("undefined colour `" + spec + "': the colour is not changed");
  return TRANSPARENT;
}

inline cmdmacro(fgcolor) {
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, TRANSPARENT, colourArg(args, args.text(1)));
}

inline cmdmacro(bgcolor) {
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, colourArg(args, args.text(1)), TRANSPARENT);
}

inline cmdmacro(textcolor) {
  // Inherit the surrounding mode — \textcolor in a math expression
  // must still parse its body as math so `\textcolor{red}{c^2}` keeps
  // the superscript, matching LaTeX's xcolor semantics where
  // \textcolor only changes colour, not mode.
  auto a = args.formula(2, args.isMathMode());
  return sptrOf<ColorAtom>(a, TRANSPARENT, colourArg(args, args.text(1)));
}

inline cmdmacro(colorbox) {
  color c = colourArg(args, args.text(1));
  auto body = args.formula(2, args.isMathMode());
  // A colour that is not defined paints nothing, and \colorbox has no frame to leave.
  if (isTransparent(c)) return body;
  return sptrOf<FBoxAtom>(body, c, c, false);
}

inline cmdmacro(fcolorbox) {
  color f = colourArg(args, args.text(2));
  color b = colourArg(args, args.text(1));
  return sptrOf<FBoxAtom>(args.formula(3, args.isMathMode()), f, b);
}

cmdmacro(definecolor);

cmdmacro(colorlet);

}  // namespace microtex

#endif  // MICROTEX_MACRO_COLORS_H
