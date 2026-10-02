#include "macro/macro_delims.h"

#include "atom/atom_stack.h"
#include "atom/diagram_atom.h"
#include "utils/utf.h"

namespace microtex {

using namespace std;

cmdmacro(xarrow) {
  std::string name = args.text(0).substr(1);
  const auto& over =
    StackArgs::autoSpace(args.formula(1, args.isMathMode()), false);
  const auto& under =
    StackArgs::autoSpace(args.formula(2, args.isMathMode()), false);
  const auto stack = new StackAtom(nullptr, over, under);
  // The ones the font has no stretching glyph for are drawn.
  using Kind = StretchArrowAtom::Kind;
  const Kind kinds[] = {Kind::twoHeadRight, Kind::twoHeadLeft, Kind::longEqual, Kind::toFrom};
  const char* names[] = {"twoheadrightarrow", "twoheadleftarrow", "longequal", "tofrom"};
  for (int k = 0; k < 4; k++) {
    if (name != names[k]) continue;
    stack->setBaseAtom(sptrOf<StretchArrowAtom>(kinds[k], [stack](const Env& env) -> float {
      return stack->getMaxWidth() + Units::fsize(UnitType::ex, 1.f, env);
    }));
    return sptr<StackAtom>(stack);
  }
  const auto& arrow = sptrOf<ExtensibleAtom>(
    name,
    // capture raw pointer to avoid cycle reference
    [stack](const Env& env) -> float {
      return stack->getMaxWidth() + Units::fsize(UnitType::ex, 1.f, env);
    },
    false
  );
  arrow->_type = AtomType::relation;
  stack->setBaseAtom(arrow);
  return sptr<StackAtom>(stack);
}

}  // namespace microtex
