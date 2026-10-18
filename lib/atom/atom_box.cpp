#include "atom/atom_box.h"

#include "box/box_group.h"
#include "box/box_single.h"
#include "env/env.h"
#include "env/units.h"

using namespace microtex;

float OvalAtom::_multiplier = 0.5f;
float OvalAtom::_diameter = 0.f;

sptr<Box> FBoxAtom::createBox(Env& env) {
  auto base = _base->createBox(env);
  // LaTeX's \fbox: \fboxsep of room inside a frame of \fboxrule.
  auto t = Units::fsize(_rule, env);
  auto space = Units::fsize(_sep, env);
  if (!_frame) {
    // \colorbox: the room and the colour, with no frame.
    auto padded = sptrOf<WrapperBox>(base, base->_width, base->_height, base->_depth, Alignment::left);
    padded->addInsets(space, space, space, space);
    padded->setBackground(_bg);
    return padded;
  }
  if (isTransparent(_bg)) return sptrOf<FramedBox>(base, t, space);
  return sptrOf<FramedBox>(base, t, space, _line, _bg);
}

sptr<Box> DoubleFramedAtom::createBox(Env& env) {
  auto base = _base->createBox(env);
  auto t = env.mathConsts().fractionRuleThickness() * env.scale();
  auto space = Units::fsize(0.5_em, env);
  auto sspace = 1.5f * t + Units::fsize(1._pt, env);
  auto inner = sptrOf<FramedBox>(base, 0.75f * t, space);
  return sptrOf<FramedBox>(inner, 1.5f * t, sspace);
}

sptr<Box> ShadowAtom::createBox(Env& env) {
  auto x = FBoxAtom::createBox(env);
  auto b = std::static_pointer_cast<FramedBox>(x);
  float t = env.mathConsts().fractionRuleThickness() * env.scale() * 3;
  return sptrOf<ShadowBox>(b, t);
}

sptr<Box> OvalAtom::createBox(Env& env) {
  auto x = FBoxAtom::createBox(env);
  auto b = std::static_pointer_cast<FramedBox>(x);
  return sptrOf<OvalBox>(b, _multiplier, _diameter);
}
