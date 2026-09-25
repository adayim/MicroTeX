#include "macro/macro_fonts.h"

#include "microtex.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"

namespace microtex {

cmdmacro(intertext) {
  ArrayFormula* arr = args.alignmentHere();
  if (arr == nullptr) throw ex_parse("Command \\intertext must used in array environment!");

  std::string str(args.text(1));
  replaceAll(str, "^{\\prime}", "\'");
  replaceAll(str, "^{\\prime\\prime}", "\'\'");

  auto a = args.formulaOf(str, false);
  sptr<Atom> ra = sptrOf<FontStyleAtom>(FontStyle::rm, false, a);
  ra->_type = AtomType::interText;
  arr->add(ra);
  arr->addRow();

  return nullptr;
}

cmdmacro(mathversion) {
  auto mathStyle = MathStyle::TeX;
  const auto& options = parseOption(args.text(2));
  const auto it = options.find("math-style");
  if (it != options.end()) {
    const auto& value = it->second;
    if (value == "TeX") {
      mathStyle = MathStyle::TeX;
    } else if (value == "ISO") {
      mathStyle = MathStyle::ISO;
    } else if (value == "French") {
      mathStyle = MathStyle::French;
    } else if (value == "upright") {
      mathStyle = MathStyle::upright;
    }
  }
  MicroTeX::setDefaultMathFont(args.text(1));
  return sptrOf<MathFontAtom>(mathStyle, args.text(1));
}

}  // namespace microtex
