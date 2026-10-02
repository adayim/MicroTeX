#ifndef MICROTEX_MACRO_MISC_H
#define MICROTEX_MACRO_MISC_H

#include <memory>

#include "atom/atom_basic.h"
#include "atom/atom_misc.h"
#include "atom/atom_scripts.h"
#include "atom/atom_sideset.h"
#include "core/formula.h"
#include "core/split.h"
#include "graphic/graphic.h"
#include "macro/macro.h"
#include "macro/macro_decl.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

namespace microtex {

// MicroTeX's switch for its old parser's definition checks. The front end
// follows LaTeX's rules whatever it says (\newcommand of a defined name
// warns), so it is read and does nothing; Stage 8 decides whether it stays.
inline cmdmacro(fatalIfCmdConflict) {
  return nullptr;
}

inline cmdmacro(breakEverywhere) {
  RowAtom::_breakEverywhere = args.text(1) == "true";
  return nullptr;
}

inline cmdmacro(st) {
  auto base = args.formula(1, args.isMathMode());
  return sptrOf<StrikeThroughAtom>(base);
}

inline cmdmacro(spATbreve) {
  auto* vra = new VRowAtom(args.formulaOf("\\displaystyle\\!\\breve{}"));
  vra->setRaise(UnitType::ex, 0.6f);
  return sptrOf<SmashedAtom>(sptr<Atom>(vra), "");
}

inline cmdmacro(clrlap) {
  return sptrOf<LapedAtom>(args.formula(1, true, true), args.text(0)[0]);
}

inline cmdmacro(mathclrlap) {
  return sptrOf<LapedAtom>(args.formula(1, true, true), args.text(0)[4]);
}

inline sptr<Atom> _cancel(int cancelType, CommandArgs& args) {
  auto base = args.formula(1);
  if (base == nullptr) throw ex_parse("Cancel content must not be empty!");
  return sptrOf<CancelAtom>(base, cancelType);
}

inline cmdmacro(cancel) {
  return _cancel(CancelAtom::SLASH, args);
}

inline cmdmacro(bcancel) {
  return _cancel(CancelAtom::BACKSLASH, args);
}

inline cmdmacro(xcancel) {
  return _cancel(CancelAtom::CROSS, args);
}

// cancel's \cancelto{value}{base}: the base struck by an arrow, with the
// value at its tip as a superscript.
inline cmdmacro(cancelto) {
  auto value = args.formula(1);
  auto base = args.formula(2);
  if (base == nullptr) throw ex_parse("Cancel content must not be empty!");
  return sptrOf<ScriptsAtom>(sptrOf<CancelAtom>(base, CancelAtom::ARROW), nullptr, value);
}

// ulem's: a strike-out and an underline that break with their text, which
// is text in text and math in math.
inline cmdmacro(sout) {
  return sptrOf<RuleDecorAtom>(args.formula(1, args.isMathMode()), false);
}

inline cmdmacro(uline) {
  return sptrOf<RuleDecorAtom>(args.formula(1, args.isMathMode()), true);
}

inline cmdmacro(underscore) {
  return SymbolAtom::get("_");
}

inline cmdmacro(nbsp) {
  return sptrOf<SpaceAtom>();
}

inline cmdmacro(joinrel) {
  return sptrOf<TypedAtom>(
    AtomType::relation,
    AtomType::relation,
    sptrOf<SpaceAtom>(UnitType::mu, -2.6f, 0.f, 0.f)
  );
}

inline cmdmacro(smash) {
  return sptrOf<SmashedAtom>(args.formula(1), args.text(2));
}

// As in LaTeX, a phantom of text is text, and of math is math.
inline cmdmacro(hphantom) {
  return sptrOf<PhantomAtom>(args.formula(1, args.isMathMode()), true, false, false);
}

inline cmdmacro(vphantom) {
  return sptrOf<PhantomAtom>(args.formula(1, args.isMathMode()), false, true, true);
}

inline cmdmacro(phantom) {
  return sptrOf<PhantomAtom>(args.formula(1, args.isMathMode()), true, true, true);
}

inline cmdmacro(surd) {
  return sptrOf<VCenterAtom>(SymbolAtom::get("surdsign"));
}

inline cmdmacro(lmoustache) {
  auto* s = new SymbolAtom(*(SymbolAtom::get("lmoustache")));
  auto b = sptrOf<BigSymbolAtom>(sptr<SymbolAtom>(s), 1);
  b->_type = AtomType::opening;
  return b;
}

inline cmdmacro(rmoustache) {
  auto* s = new SymbolAtom(*(SymbolAtom::get("rmoustache")));
  auto b = sptrOf<BigSymbolAtom>(sptr<SymbolAtom>(s), 1);
  b->_type = AtomType::closing;
  return b;
}

inline cmdmacro(breakmark) {
  // `\-` is TeX's discretionary hyphen: it offers a break and draws a
  // hyphen if that break is taken. It used to offer the break and draw
  // nothing, which is worse than not breaking at all.
  return sptrOf<HyphenMarkAtom>();
}

inline cmdmacro(nokern) {
  return sptrOf<NokernAtom>();
}

/**************************************** limits macros *******************************************/

/***************************************** implement at .cpp **************************************/

cmdmacro(longdiv);

cmdmacro(hvspace);

cmdmacro(rule);

cmdmacro(raisebox);

cmdmacro(romannumeral);

cmdmacro(zstack);

/**************************************** not implemented *****************************************/

inline cmdmacro(includegraphics) {
  return nullptr;
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_MISC_H
