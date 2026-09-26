#include "atom/atom_font.h"

#include "atom/atom_basic.h"
#include "box/box_single.h"

using namespace microtex;

sptr<Box> FontStyleAtom::createBox(Env& env) {
  // Guard against malformed input (e.g. `\mathbf{` inside an array env) that
  // can leave _atom unset; upstream MicroTeX issue #160.
  if (_atom == nullptr) _atom = sptrOf<EmptyAtom>();
  if (_nested) {
    // Added to the style around it, which is put back after as it was:
    // taking the bit out again cleared one that was set before -- the
    // roman of the prose around a \text, the bold around a \textbf.
    const FontStyle around = _mathMode ? env.mathFontStyle() : env.textFontStyle();
    const auto both = static_cast<FontStyle>(static_cast<u16>(around) | static_cast<u16>(_style));
    return env.withFontStyle(both, _mathMode, [&](Env& e) { return _atom->createBox(e); });
  }
  return env.withFontStyle(_style, _mathMode, [&](Env& e) { return _atom->createBox(e); });
}

sptr<Box> MathFontAtom::createBox(Env& env) {
  env.selectMathFont(_name, _mathStyle);
  return StrutBox::empty();
}
