#include "atom/atom_delim.h"

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
  const auto base = _base->createBox(env);
  // LaTeX's are never shorter than the glyphs they are built of: a brace is 18pt (1.8em) over one letter, an arrow
  // the width of \rightarrow (pdflatex, \underbrace{a}, \overrightarrow{a}, \overleftrightarrow{a}).
  const bool brace = _delim == "overbrace" || _delim == "underbrace";
  const bool arrow = _delim == "overrightarrow" || _delim == "overleftarrow" || _delim == "underrightarrow" ||
                     _delim == "underleftarrow";
  const bool both = _delim == "overleftrightarrow" || _delim == "underleftrightarrow";
  const float least = brace ? 1.8f : both ? 1.222f : arrow ? 1.f : 0.f;
  const auto delim = microtex::createHDelim(
    _delim, env, std::max(base->_width, least * Units::fsize(UnitType::em, 1.f, env)));
  base->_shift = (delim->_width - base->_width) / 2;

  const auto vbox = sptrOf<VBox>();
  if (_over) {
    vbox->add(delim);
    // Set off from the base as pdflatex does it: the ink of \overbrace, \underbrace and the arrows round `abc`, within
    // half a point (the braces and arrow heads are taller in Latin Modern's own assembly, so a centre is matched).
    const auto kern = SpaceAtom(UnitType::ex, 0.f, -0.8f, 0.f).createBox(env);
    vbox->add(kern);
    vbox->add(base);
    const auto h = vbox->_height + vbox->_depth;
    vbox->_depth = base->_depth;
    vbox->_height = h - vbox->_depth;
  } else {
    vbox->add(base);
    vbox->add(SpaceAtom(UnitType::ex, 0.f, brace ? 0.27f : 0.15f, 0.f).createBox(env));
    vbox->add(delim);
    const auto h = vbox->_height + vbox->_depth;
    vbox->_height = base->_height;
    vbox->_depth = h - vbox->_height;
  }

  return vbox;
}
