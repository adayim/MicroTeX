#include "atom/atom_space.h"

#include "box/box_single.h"
#include "core/glue.h"
#include "env/env.h"
#include "env/units.h"

using namespace std;
using namespace microtex;

sptr<Box> SpaceAtom::createBox(Env& env) {
  if (!_blankSpace) {
    float w = Units::fsize(_unit, _width, env);
    float h = Units::fsize(_unit, _height, env);
    float d = Units::fsize(_unit, _depth, env);
    return sptrOf<StrutBox>(w, h, d, 0.f);
  }
  if (_blankType == SpaceType::none) {
    // An interword space is glue, not a rigid box: it is the only thing
    // a justified line has to give. The natural width is unchanged, and
    // nothing reads _stretch/_shrink unless justification is switched
    // on, so ragged output is byte-for-byte what it was before.
    //
    // Proportions follow TeX's interword glue for an upright font --
    // stretch a half space, shrink a third -- which keeps a stretched
    // line from opening up further than a reader tolerates.
    const float w = env.space(_isMathMode);
    return sptrOf<GlueBox>(w, w / 2.f, w / 3.f);
  }
  return Glue::get(_blankType, env);
}
