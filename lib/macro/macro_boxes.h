#ifndef MICROTEX_MACRO_BOXES_H
#define MICROTEX_MACRO_BOXES_H

#include "atom/atom_box.h"
#include "atom/atom_misc.h"
#include "macro/macro_decl.h"
#include "utils/string_utils.h"

namespace microtex {

inline cmdmacro(rotatebox) {
  float angle = 0;
  if (!args.text(1).empty()) valueOf(args.text(1), angle);
  return sptrOf<RotateAtom>(args.formula(2, true, true), angle, args.text(3));
}

inline cmdmacro(reflectbox) {
  return sptrOf<ReflectAtom>(args.formula(1, true, true));
}

inline cmdmacro(scalebox) {
  float sx = 1, sy = 1;
  valueOf(args.text(1), sx);

  if (args.text(3).empty())
    sy = sx;
  else
    valueOf(args.text(3), sy);

  if (sx == 0) sx = 1;
  if (sy == 0) sy = 1;
  return sptrOf<ScaleAtom>(args.formula(2, args.isMathMode()), sx, sy);
}

inline cmdmacro(resizebox) {
  const std::string& ws = args.text(1);
  const std::string& hs = args.text(2);
  return sptrOf<ResizeAtom>(args.formula(3, true, true), ws, hs, ws == "!" || hs == "!");
}

inline cmdmacro(shadowbox) {
  return sptrOf<ShadowAtom>(args.formula(1, true, true));
}

inline cmdmacro(ovalbox) {
  return sptrOf<OvalAtom>(args.formula(1, true, true));
}

inline cmdmacro(cornersize) {
  float size = 0.5f;
  valueOf(args.text(1), size);
  if (size <= 0 || size > 0.5f) size = 0.5f;
  OvalAtom::_multiplier = size;
  OvalAtom::_diameter = 0;
  return nullptr;
}

inline cmdmacro(doublebox) {
  return sptrOf<DoubleFramedAtom>(args.formula(1, true, true));
}

inline cmdmacro(fbox) {
  return sptrOf<FBoxAtom>(args.formula(1));
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_BOXES_H
