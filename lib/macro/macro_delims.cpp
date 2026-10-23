#include "macro/macro_delims.h"

#include "atom/atom_stack.h"
#include "atom/diagram_atom.h"
#include "utils/utf.h"

namespace microtex {

using namespace std;

cmdmacro(xarrow) {
  std::string name = args.text(0).substr(1);
  // Their text is set in script style, as amsmath's \xrightarrow sets it.
  const auto& over =
    StackArgs::autoSpace(args.formula(1, args.isMathMode()), true);
  const auto& under =
    StackArgs::autoSpace(args.formula(2, args.isMathMode()), true);
  const auto stack = new StackAtom(nullptr, over, under);
  // The ones the font has no stretching glyph for are drawn.
  using Kind = StretchArrowAtom::Kind;
  // amsmath sets the arrow a label's width and 14mu of script style longer (pdflatex, \xrightarrow{abcdef}: the
  // label's 24.07pt and 6.37pt); mathtools' \xLeftarrow and \xLeftrightarrow have more (+1.51pt, +4.84pt) and are
  // never shorter than the glyphs of \Leftarrow and \Leftrightarrow (12.22pt, 15.56pt).
  const float more = name == "Leftarrow" ? 0.151f : name == "Leftrightarrow" ? 0.484f : 0.f;
  const float least = name == "Leftarrow" ? 1.222f : name == "Leftrightarrow" ? 1.556f : 0.f;
  const auto length = [stack, more, least](const Env& env) -> float {
    const float em = Units::fsize(UnitType::em, 1.f, env);
    return std::max(stack->getMaxWidth() + (0.637f + more) * em, least * em);
  };
  const Kind kinds[] = {Kind::twoHeadRight, Kind::twoHeadLeft, Kind::longEqual, Kind::toFrom, Kind::doubleLeft, Kind::doubleBoth};
  const char* names[] = {"twoheadrightarrow", "twoheadleftarrow", "longequal", "tofrom", "Leftarrow", "Leftrightarrow"};
  for (int k = 0; k < 6; k++) {
    if (name != names[k]) continue;
    stack->setBaseAtom(sptrOf<StretchArrowAtom>(kinds[k], length));
    return sptr<StackAtom>(stack);
  }
  // The stack is captured by raw pointer to avoid a cycle of references.
  const auto& arrow = sptrOf<ExtensibleAtom>(name, length, false);
  arrow->_type = AtomType::relation;
  stack->setBaseAtom(arrow);
  return sptr<StackAtom>(stack);
}

}  // namespace microtex
