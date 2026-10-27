#include "atom/atom_fence.h"

#include "atom/atom_row.h"
#include "box/box_factory.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "core/glue.h"
#include "env/env.h"
#include "env/units.h"
#include "utils/exceptions.h"

using namespace microtex;

namespace {

// Refused when read, as TeX refuses one: found only at layout, a name that
// is not a delimiter failed the whole formula, where a parse error in a
// command is contained to that command.
void checkDelimiter(const std::string& name) {
  if (name.empty() || name == ".") return;
  if (delimiterSymbol(name) == nullptr) throw ex_parse(name + " is not a delimiter!");
}

}  // namespace

FencedAtom::FencedAtom(const sptr<Atom>& b, std::string l, std::string r)
    : _base(RowAtom::withItalic(b)), _l(std::move(l)), _r(std::move(r)) {
  checkDelimiter(_l);
  checkDelimiter(_r);
}

FencedAtom::FencedAtom(
  const sptr<Atom>& b,
  std::string l,
  std::string r,
  std::vector<sptr<MiddleAtom>> m
)
    : _base(RowAtom::withItalic(b)), _l(std::move(l)), _r(std::move(r)), _m(std::move(m)) {
  checkDelimiter(_l);
  checkDelimiter(_r);
}

MiddleAtom::MiddleAtom(std::string sym) : _sym(std::move(sym)), _placeholder(StrutBox::empty()) {
  checkDelimiter(_sym);
}

sptr<Box> MiddleAtom::createBox(Env& env) {
  if (_height == 0) return _placeholder;
  if (_sym.empty() || _sym == ".") return StrutBox::empty();
  return microtex::createVDelim(_sym, env, _height, true);
}

sptr<Box> FencedAtom::createBox(Env& env) {
  if (_base == nullptr) {
    return StrutBox::empty();
  }
  if (auto r = dynamic_cast<RowAtom*>(_base.get()); r != nullptr) {
    r->setBreakable(false);
  }

  const auto axis = env.axisHeight() * env.scale();
  const auto center = [axis](const sptr<Box>& b) {
    b->_shift = -(b->vlen() / 2 - b->_height) - axis;
  };

  const auto base = _base->createBox(env);
  // (a fraction's delimiter is the first variant at least that size, as TeX's var_delimiter has it: no rounding; the sizes are LuaTeX's \Umathfractiondelsize for an OpenType math font, which the MATH table does not
  // carry: 2.4em in display style, 1.01em otherwise, the same for Latin Modern Math and STIX Two Math)
  //
  // Otherwise tex.web 762 (make_left_right): the contents stay on their baseline and the delimiters, centred on the axis, are
  // as tall as twice the larger reach of the contents above and below it, at least \delimiterfactor/1000 (901) of that, or that
  // less \delimitershortfall (5pt): `\left( x^2 \right)` has taller parentheses than `(x^2)`, whose height is not what counts.
  float h;
  if (_fractionDelims) {
    h = (env.style() < TexStyle::text ? 2.4f : 1.01f) * env.em();
  } else {
    const auto reach = std::max(base->_height - base->_shift - axis, base->_depth + base->_shift + axis);
    h = std::max(0.901f * 2.f * reach, 2.f * reach - Units::fsize(UnitType::pt, 5.f, env));
  }

  for (const auto& m : _m) {
    m->_height = h;
    auto b = m->createBox(env);
    center(b);
    b->_shift -= base->_shift;
    base->replaceFirst(m->_placeholder, b);
  }

  auto hbox = sptrOf<HBox>();
  // \nulldelimiterspace, 1.2pt of the 10pt page: a length, so it does not shrink in a script
  const auto nullSpace = [&] { return sptrOf<StrutBox>(Units::fsize(Dimen{1.2f, UnitType::pt}, env), 0.f, 0.f, 0.f); };

  if (_l.empty() || _l == ".") {
    if (_nullSpace) hbox->add(nullSpace());
  } else {
    auto l = microtex::createVDelim(_l, env, h, !_fractionDelims);
    center(l);
    hbox->add(l);
    if (!base->isSpace()) {
      hbox->add(Glue::get(AtomType::opening, _base->leftType(), env));
    }
  }

  hbox->add(base);

  if (_r.empty() || _r == ".") {
    if (_nullSpace) hbox->add(nullSpace());
  } else {
    if (!base->isSpace()) {
      hbox->add(Glue::get(_base->rightType(), AtomType::closing, env));
    }
    auto r = microtex::createVDelim(_r, env, h, !_fractionDelims);
    center(r);
    hbox->add(r);
  }

  return hbox;
}
