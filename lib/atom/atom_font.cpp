#include "atom/atom_font.h"

#include "atom/atom_basic.h"
#include "box/box_single.h"

using namespace microtex;

sptr<Box> FontStyleAtom::createBox(Env& env) {
  // Guard against malformed input (e.g. `\mathbf{` inside an array env) that
  // can leave _atom unset; upstream MicroTeX issue #160.
  if (_atom == nullptr) _atom = sptrOf<EmptyAtom>();
  if (_nested) {
    _mathMode ? env.addMathFontStyle(_style) : env.addTextFontStyle(_style);
    auto box = _atom->createBox(env);
    _mathMode ? env.removeMathFontStyle(_style) : env.removeTextFontStyle(_style);
    return box;
  }
  return env.withFontStyle(_style, _mathMode, [&](Env& e) { return _atom->createBox(e); });
}

sptr<Box> MathFontAtom::createBox(Env& env) {
  env.selectMathFont(_name, _mathStyle);
  return StrutBox::empty();
}
