#include "macro/macro_frac.h"

#include "atom/atom_basic.h"
#include "atom/atom_fence.h"
#include "atom/atom_row.h"
#include "atom/atom_space.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

using namespace std;

namespace microtex {

cmdmacro(binom) {
  const auto num = args.formula(1);
  const auto den = args.formula(2);
  if (num == nullptr || den == nullptr)
    throw ex_parse("Both binomial coefficients must be not empty!");
  auto f = sptrOf<FracAtom>(num, den, false);
  return FracAtom::fence(f, "lparen", "rparen");
}

cmdmacro(frac) {
  const auto num = args.formula(1);
  const auto den = args.formula(2);
  if (num == nullptr || den == nullptr)
    throw ex_parse("Both numerator and denominator of a fraction can't be empty!");
  return sptrOf<FracAtom>(num, den, true);
}

cmdmacro(cfrac) {
  Alignment numAlign = Alignment::center;
  if (args.text(3) == "r") {
    numAlign = Alignment::right;
  } else if (args.text(3) == "l") {
    numAlign = Alignment::left;
  }
  const auto num = args.formula(1);
  const auto denom = args.formula(2);
  if (num == nullptr || denom == nullptr)
    throw ex_parse("Both numerator and denominator of a fraction can't be empty!");
  // amsmath: the numerator has a \strut (0.7 and 0.3 of 12pt), and the fraction a \kern-\nulldelimiterspace
  const auto strutted = sptrOf<RowAtom>();
  strutted->add(sptrOf<SpaceAtom>(UnitType::em, 0.f, 0.84f, 0.36f));
  strutted->add(num);
  const auto n = sptrOf<StyleAtom>(TexStyle::display, strutted);
  const auto d = sptrOf<StyleAtom>(TexStyle::display, denom);
  const auto f = sptrOf<FracAtom>(n, d, numAlign, Alignment::center);
  const auto row = sptrOf<RowAtom>();
  row->add(sptrOf<StyleAtom>(TexStyle::display, f));
  row->add(sptrOf<SpaceAtom>(UnitType::pt, -1.2f, 0.f, 0.f));
  return row;
}

// \genfrac{left}{right}{thickness}{style}{numerator}{denominator}, as amsmath reads it: an empty
// thickness is the default rule and 0pt none, and an empty style is the one the fraction stands in.
cmdmacro(genfrac) {
  bool rule = true;
  Dimen thickness;
  if (!args.text(3).empty()) {
    thickness = Units::getDimen(args.text(3));
    if (thickness.val <= 0.f) rule = false;
  }

  const auto num = args.formula(5);
  const auto den = args.formula(6);
  if (num == nullptr || den == nullptr) {
    throw ex_parse("Both numerator and denominator of a fraction can't be empty!");
  }

  auto fa = sptrOf<FracAtom>(num, den, rule, thickness);
  auto ra = sptrOf<RowAtom>();
  sptr<Atom> f = FracAtom::fence(fa, args.text(1), args.text(2));
  if (!args.text(4).empty()) {
    int style = 0;
    valueOf(args.text(4), style);
    style = std::max(0, std::min(3, style));
    f = sptrOf<StyleAtom>(static_cast<TexStyle>(style * 2), f);
  }
  ra->add(f);

  return ra;
}

}  // namespace microtex
