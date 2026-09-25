#include "atom/atom_fence.h"

#include "atom/atom_row.h"
#include "box/box_factory.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "core/glue.h"
#include "env/env.h"
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
    : _base(b), _l(std::move(l)), _r(std::move(r)) {
  checkDelimiter(_l);
  checkDelimiter(_r);
}

FencedAtom::FencedAtom(
  const sptr<Atom>& b,
  std::string l,
  std::string r,
  std::vector<sptr<MiddleAtom>> m
)
    : _base(b), _l(std::move(l)), _r(std::move(r)), _m(std::move(m)) {
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
  center(base);
  const auto h = base->vlen();

  for (const auto& m : _m) {
    m->_height = h;
    auto b = m->createBox(env);
    center(b);
    b->_shift -= base->_shift;
    base->replaceFirst(m->_placeholder, b);
  }

  auto hbox = sptrOf<HBox>();

  if (!_l.empty() && _l != ".") {
    auto l = microtex::createVDelim(_l, env, h, true);
    center(l);
    hbox->add(l);
    if (!base->isSpace()) {
      hbox->add(Glue::get(AtomType::opening, _base->leftType(), env));
    }
  }

  hbox->add(base);

  if (!_r.empty() && _r != ".") {
    if (!base->isSpace()) {
      hbox->add(Glue::get(_base->rightType(), AtomType::closing, env));
    }
    auto r = microtex::createVDelim(_r, env, h, true);
    center(r);
    hbox->add(r);
  }

  return hbox;
}
