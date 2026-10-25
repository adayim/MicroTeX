#include "atom/atom_font.h"

#include "atom/atom_basic.h"
#include "atom/atom_char.h"
#include "box/box_single.h"

using namespace microtex;

const Atom* FontStyleAtom::loneMathChar() const {
  if (!_mathMode || _atom == nullptr) return nullptr;
  if (const auto* inner = dynamic_cast<const FontStyleAtom*>(_atom.get()); inner != nullptr) {
    return inner->loneMathChar();
  }
  const auto* c = dynamic_cast<const CharSymbol*>(_atom.get());
  if (c == nullptr || !c->isMathMode() || _atom->leftType() == AtomType::bigOperator) return nullptr;
  return _atom.get();
}

sptr<Box> FontStyleAtom::createBox(Env& env) {
  // Guard against malformed input (e.g. `\mathbf{` inside an array env) that
  // can leave _atom unset; upstream MicroTeX issue #160.
  if (_atom == nullptr) _atom = sptrOf<EmptyAtom>();
  // The style is put back after as it was: taking the bit out again cleared
  // one that was set before -- the roman of the prose around a \text, the
  // bold around a \textbf.
  return inStyle(env, [&](Env& e) { return _atom->createBox(e); });
}

sptr<Box> MathFontAtom::createBox(Env& env) {
  env.selectMathFont(_name, _mathStyle);
  return StrutBox::empty();
}
