#include "atom/atom_frac.h"

#include "atom/atom_fence.h"
#include "atom/atom_row.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "env/env.h"
#include "env/units.h"

using namespace microtex;

FracAtom::FracAtom(const sptr<Atom>& num, const sptr<Atom>& den, bool rule, const Dimen& thickness)
    : _thickness(thickness), _num(RowAtom::withItalic(num)), _dnom(RowAtom::withItalic(den)), _rule(rule) {}

FracAtom::FracAtom(
  const sptr<Atom>& num,
  const sptr<Atom>& den,
  Alignment numAlign,
  Alignment denAlign
)
    : _num(RowAtom::withItalic(num)), _dnom(RowAtom::withItalic(den)), _rule(true) {
  _numAlign = checkAlign(numAlign);
  _dnomAlign = checkAlign(denAlign);
}

sptr<Box> FracAtom::createBox(Env& env) {
  const auto& math = env.mathConsts();

  const auto theta = [&]() {
    if (!_rule) return 0.f;
    if (!_thickness.isValid()) return math.fractionRuleThickness() * env.scale();
    return Units::fsize(_thickness, env);
  }();

  const auto x = env.withStyle(env.numStyle(), [&](Env& style) { return _num->createBox(style); });
  const auto z =
    env.withStyle(env.dnomStyle(), [&](Env& style) { return _dnom->createBox(style); });
  const auto w = std::max(x->_width, z->_width);

  const auto isDisplay = env.style() < TexStyle::text;

  auto u = 0.f, v = 0.f;
  if (isDisplay) {
    u = _rule ? math.fractionNumeratorDisplayStyleShiftUp() : math.stackTopDisplayStyleShiftUp();
    v = _rule ? math.fractionDenominatorDisplayStyleShiftDown()
              : math.stackBottomDisplayStyleShiftDown();
  } else {
    u = _rule ? math.fractionNumeratorShiftUp() : math.stackTopShiftUp();
    v = _rule ? math.fractionDenominatorShiftDown() : math.stackBottomShiftDown();
  }

  const auto axis = math.axisHeight() * env.scale();
  if (!_rule) {
    const auto phi = isDisplay ? math.stackDisplayStyleGapMin() : math.stackGapMin();
    const auto psi = (u - x->_depth) - (z->_depth - v);
    if (psi < phi) {
      u += (phi - psi) / 2;
      v += (phi - psi) / 2;
    }
  } else {
    // TeX's gaps are a multiple of the rule's own thickness (rule 15d), the font's constants those of its default one
    const auto fixed = math.fractionRuleThickness() * env.scale();
    const auto grow = _thickness.isValid() && fixed > 0.f ? theta / fixed : 1.f;
    const auto phi = grow * (isDisplay ? math.fractionNumeratorDisplayStyleGapMin()
                                       : math.fractionNumeratorGapMin());
    const auto d = (u - x->_depth) - (axis + theta / 2);
    if (d < phi) {
      u += phi - d;
    }
    const auto rho = grow * (isDisplay ? math.fractionDenominatorDisplayStyleGapMin()
                                       : math.fractionDenominatorGapMin());
    const auto f = (axis - theta / 2) - (z->_height - v);
    if (f < rho) {
      v += rho - f;
    }
  }

  const auto shift = [&](const sptr<Box>& b, Alignment align) {
    if (align == Alignment::center) {
      b->_shift = (w - b->_width) / 2;
    } else if (align == Alignment::right) {
      b->_shift = w - b->_width;
    }
  };
  shift(x, _numAlign);
  shift(z, _dnomAlign);

  const auto kern = (x->_height + u + z->_depth + v) - (x->vlen() + z->vlen()) - theta;
  const auto vbox = sptrOf<VBox>();
  vbox->add(x);
  if (_rule) {
    // TODO equal gaps between numerator and denominator?
    const auto dn = x->_height + u - axis - theta / 2 - x->vlen();
    vbox->add(sptrOf<StrutBox>(0.f, dn, 0.f, 0.f));
    vbox->add(sptrOf<RuleBox>(theta, w, 0.f));
    const auto dd = kern - dn;
    vbox->add(sptrOf<StrutBox>(0.f, dd, 0.f, 0.f));
  } else {
    vbox->add(sptrOf<StrutBox>(0.f, kern, 0.f, 0.f));
  }
  vbox->add(z);
  vbox->_height = x->_height + u;
  vbox->_depth = z->_depth + v;

  if (!_nullLeft && !_nullRight) return vbox;
  // \nulldelimiterspace is a length, 1.2pt of the 10pt page, whatever the style
  const auto space = Units::fsize(Dimen{1.2f, UnitType::pt}, env);
  const auto hbox = sptrOf<HBox>();
  if (_nullLeft) hbox->add(sptrOf<StrutBox>(space, 0.f, 0.f, 0.f));
  hbox->add(vbox);
  if (_nullRight) hbox->add(sptrOf<StrutBox>(space, 0.f, 0.f, 0.f));
  return hbox;
}

sptr<Atom> FracAtom::fence(const sptr<FracAtom>& frac, const std::string& l, const std::string& r) {
  const bool nullLeft = l.empty() || l == ".", nullRight = r.empty() || r == ".";
  // no delimiter at all: the fraction with its own two null ones, as \frac is
  if (nullLeft && nullRight) return frac;
  // built first: it refuses what is not a delimiter, and the fraction is then left as it was
  auto fenced = sptrOf<FencedAtom>(frac, l, r);
  fenced->setNullSpace(false);
  fenced->setFractionDelimiters(true);
  frac->_nullLeft = nullLeft;
  frac->_nullRight = nullRight;
  return fenced;
}
