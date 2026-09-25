#include "macro/macro_scripts.h"

#include "atom/atom_basic.h"
#include "atom/atom_scripts.h"

namespace microtex {

using namespace std;

cmdmacro(sideset) {
  auto l = args.formula(1, true, true);
  auto r = args.formula(2, true, true);
  auto op = args.formula(3, true, true);
  if (op == nullptr) {
    auto in = sptrOf<CharAtom>('M', FontStyle::rm, true);
    op = sptrOf<PhantomAtom>(in, false, true, true);
  }
  op->_limitsType = LimitsType::limits;
  op->_type = AtomType::bigOperator;
  return sptrOf<SideSetsAtom>(op, l, r);
}

cmdmacro(prescript) {
  auto base = args.formula(3, true, true);
  return sptrOf<ScriptsAtom>(base, args.formula(2, true, true), args.formula(1, true, true), false);
}

}  // namespace microtex
