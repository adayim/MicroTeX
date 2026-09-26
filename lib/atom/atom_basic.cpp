#include "atom/atom_basic.h"

#include "atom/atom_scripts.h"
#include "box/box_factory.h"
#include "box/box_group.h"
#include "env/env.h"
#include "env/units.h"

using namespace std;
using namespace microtex;

sptr<Box> StyleAtom::createBox(Env& env) {
  return env.withStyle(_style, [&](Env& e) { return _base->createBox(e); });
}

sptr<Box> AStyleAtom::createBox(Env& env) {
  TexStyle style = TexStyle::display;
  if (_name == "dnomstyle") {
    style = env.dnomStyle();
  } else if (_name == "numstyle") {
    style = env.numStyle();
  } else if (_name == "substyle") {
    style = env.subStyle();
  } else if (_name == "supstyle") {
    style = env.supStyle();
  }
  return env.withStyle(style, [&](Env& e) { return _base->createBox(e); });
}

sptr<Box> SmashedAtom::createBox(Env& env) {
  auto b = sptrOf<HBox>(_base->createBox(env));
  if (_h) b->_height = 0;
  if (_d) b->_depth = 0;
  return b;
}

sptr<Box> ScaleAtom::createBox(Env& env) {
  auto box = sptrOf<ScaleBox>(_base->createBox(env), _sx, _sy);
  box->_openable = _declaration;
  return box;
}

sptr<Box> MathAtom::createBox(Env& env) {
  const auto style = env.style();
  // if parent style greater than "this style", that means the parent uses smaller font size,
  // then uses parent style instead
  if (_style > style) {
    env.setStyle(_style);
  }
  _base = _base == nullptr ? sptrOf<EmptyAtom>() : _base;
  auto box = _base->createBox(env);
  env.setStyle(style);
  return box;
}

sptr<Box> HlineAtom::createBox(Env& env) {
  const auto drt = _thicknessUnit != UnitType::none
                     ? Units::fsize(_thicknessUnit, _thickness, env)
                     : env.ruleThickness() * _thicknessScale;
  auto b = new RuleBox(drt, _width, _shift, _color, false);
  auto vb = new VBox();
  vb->add(sptr<Box>(b));
  vb->_type = AtomType::hline;
  return sptr<Box>(vb);
}

const color ColorAtom::_default = black;

ColorAtom::ColorAtom(const sptr<Atom>& atom, color bg, color c) : _background(bg), _color(c) {
  _elements = sptrOf<RowAtom>(atom);
}

void ColorAtom::defineColor(const string& name, color c) {
  _colors[name] = c;
}

sptr<Box> ColorAtom::createBox(Env& env) {
  auto box = sptrOf<ColorBox>(_elements->createBox(env), _color, _background);
  // A colour is no box in LaTeX (\color, \textcolor), and a background
  // here is a highlight (\bgcolor, soul's \hl): both break with their
  // text. \colorbox, which is a box, is an FBoxAtom.
  box->_openable = true;
  return box;
}

PhantomAtom::PhantomAtom(const sptr<Atom>& el) {
  if (el == nullptr)
    _elements = sptrOf<RowAtom>();
  else
    _elements = sptrOf<RowAtom>(el);
  _w = _h = _d = true;
}

PhantomAtom::PhantomAtom(const sptr<Atom>& el, bool w, bool h, bool d) {
  if (el == nullptr)
    _elements = sptrOf<RowAtom>();
  else
    _elements = sptrOf<RowAtom>(el);
  _w = w, _h = h, _d = d;
}

sptr<Box> PhantomAtom::createBox(Env& env) {
  auto res = _elements->createBox(env);
  float w = (_w ? res->_width : 0);
  float h = (_h ? res->_height : 0);
  float d = (_d ? res->_depth : 0);
  float s = res->_shift;
  return sptrOf<StrutBox>(w, h, d, s);
}

sptr<Box> ExtensibleAtom::createBox(Env& env) {
  const auto len = _getLen(env);
  return _vertical ? microtex::createVDelim(_sym, env, len)
                   : microtex::createHDelim(_sym, env, len);
}
