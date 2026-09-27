#include "macro/macro_misc.h"

#include "atom/atom_font.h"
#include "atom/atom_zstack.h"
#include "env/env.h"
#include "env/units.h"
#include "graphic/graphic.h"

using namespace microtex;
using namespace std;

namespace microtex {

cmdmacro(longdiv) {
  long dividend = 0;
  valueOf(args.text(1), dividend);
  long divisor = 0;
  valueOf(args.text(2), divisor);
  if (divisor == 0) throw ex_parse("Divisor must not be 0.");
  // Long division of whole numbers: its digits are read off the quotient.
  if (divisor < 0 || dividend < 0) throw ex_parse("Dividend and divisor must not be negative.");
  return sptrOf<LongDivAtom>(divisor, dividend);
}

cmdmacro(hvspace) {
  auto [value, unit] = Units::getDimen(args.text(1));
  return args.text(0)[0] == 'h' ? sptrOf<SpaceAtom>(unit, value, 0.f, 0.f)
                           : sptrOf<SpaceAtom>(unit, 0.f, value, 0.f);
}

cmdmacro(rule) {
  auto w = Units::getDimen(args.text(1));
  auto h = Units::getDimen(args.text(2));
  auto r = Units::getDimen(args.text(3));

  return sptrOf<RuleAtom>(w, h, -r);
}

cmdmacro(raisebox) {
  auto r = Units::getDimen(args.text(1));
  auto h = Units::getDimen(args.text(3));
  auto d = Units::getDimen(args.text(4));
  return sptrOf<RaiseAtom>(orEmpty(args.formula(2, args.isMathMode())), -r, h, d);
}

cmdmacro(romannumeral) {
  static const int numbers[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  static const string letters[] =
    {"M", "CM", "D", "CD", "C", "XC", "", "XL", "X", "IX", "V", "IV", "I"};
  string roman;

  int num;
  std::string text = args.text(1);
  valueOf(trim(text), num);
  for (int i = 0; i < 13; i++) {
    while (num >= numbers[i]) {
      roman += letters[i];
      num -= numbers[i];
    }
  }

  if (args.text(0)[0] == 'r') {
    toLower(roman);
  }

  return sptrOf<FontStyleAtom>(
    FontStyle::rm,
    args.isMathMode(),
    args.formulaOf(roman, args.isMathMode())
  );
}

cmdmacro(zstack) {
  auto halign = Alignment::left;
  if (args.text(1) == "c") {
    halign = Alignment::center;
  } else if (args.text(1) == "r") {
    halign = Alignment::right;
  }
  const auto& h = Units::getDimen(args.text(2));
  const ZStackArgs hargs{halign, h};

  auto valign = Alignment::top;
  if (args.text(3) == "c") {
    valign = Alignment::center;
  } else if (args.text(3) == "b") {
    valign = Alignment::bottom;
  } else if (args.text(3) == "B") {
    valign = Alignment::none;
  }
  const auto& v = Units::getDimen(args.text(4));
  const ZStackArgs& vargs{valign, v};

  const auto atom = args.formula(5);
  const auto anchor = args.formula(6);

  return sptrOf<ZStackAtom>(hargs, vargs, atom, anchor);
}

}  // namespace microtex
