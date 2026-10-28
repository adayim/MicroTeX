#include "atom/atom_delim.h"
#include "atom/atom_row.h"

#include "atom/atom_basic.h"

#include "box/box_factory.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "env/env.h"
#include "env/units.h"

using namespace microtex;

sptr<Box> OverUnderBar::createBox(Env& env) {
  const auto base = (_base == nullptr ? sptrOf<EmptyAtom>() : _base)->createBox(env);
  const auto vbox = sptrOf<VBox>();
  if (_over) {
    const auto asc = env.mathConsts().overbarExtraAscender() * env.scale();
    vbox->add(sptrOf<StrutBox>(0.f, asc, 0.f, 0.f));
    const auto t = env.mathConsts().overbarRuleThickness() * env.scale();
    vbox->add(sptrOf<RuleBox>(t, base->_width, 0.f));
    const auto gap = env.mathConsts().overbarVerticalGap() * env.scale();
    vbox->add(sptrOf<StrutBox>(0.f, gap, 0.f, 0.f));
    vbox->add(base);
    const auto h = vbox->_height + vbox->_depth;
    vbox->_depth = base->_depth;
    vbox->_height = h - vbox->_depth;
  } else {
    vbox->add(base);
    const auto gap = env.mathConsts().underbarVerticalGap() * env.scale();
    vbox->add(sptrOf<StrutBox>(0.f, gap, 0.f, 0.f));
    const auto t = env.mathConsts().underbarRuleThickness() * env.scale();
    vbox->add(sptrOf<RuleBox>(t, base->_width, 0.f));
    const auto dsc = env.mathConsts().underbarExtraDescender() * env.scale();
    vbox->add(sptrOf<StrutBox>(0.f, dsc, 0.f, 0.f));
    const auto h = vbox->_height + vbox->_depth;
    vbox->_height = base->_height;
    vbox->_depth = h - vbox->_height;
  }

  return vbox;
}

sptr<Box> OverUnderDelimiter::createBox(Env& env) {
  // (a lone letter has the italic correction TeX sets after it, as clean_box takes it)
  const auto base = RowAtom::withItalic(_base)->createBox(env);
  // A brace, a parenthesis or an arrow over or under a base is a stretched accent: LuaLaTeX with unicode-math (\mathover and
  // \mathunder) makes it as long as the base and as wide as the base too, however short that is (`\overbrace{a}` is as wide as
  // `a`, where pdflatex's brace is 18pt), the glyph hanging over the sides.
  const auto delim = microtex::createHDelim(_delim, env, base->_width);
  delim->_shift = (base->_width - delim->_width) / 2;

  const auto vbox = sptrOf<VBox>();
  if (_over) {
    vbox->add(delim);
    // The glyph sits on the base as an accent does: 4.5pt of its height are in front of the base's top (measured against LuaLaTeX
    // for braces, parentheses and arrows round `abc`, `x` and a fraction, within 0.1pt)
    const auto kern = SpaceAtom(UnitType::em, 0.f, -0.45f, 0.f).createBox(env);
    vbox->add(kern);
    vbox->add(base);
    const auto h = vbox->_height + vbox->_depth;
    vbox->_depth = base->_depth;
    vbox->_height = h - vbox->_depth;
  } else {
    // and under it, with nothing between
    vbox->add(base);
    vbox->add(delim);
    const auto h = vbox->_height + vbox->_depth;
    vbox->_height = base->_height;
    vbox->_depth = h - vbox->_height;
  }
  vbox->_width = base->_width;

  return vbox;
}
