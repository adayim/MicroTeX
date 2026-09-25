#include "macro/macro_colors.h"

#include "utils/exceptions.h"
#include "utils/string_utils.h"

namespace microtex {

cmdmacro(definecolor) {
  color c = TRANSPARENT;
  const auto& cs = args.text(3);
  if (args.text(2) == "gray") {
    float f = 0;
    valueOf(args.text(3), f);
    c = rgb(f, f, f);
  } else if (args.text(2) == "rgb" || args.text(2) == "RGB") {
    // xcolor's RGB is the same three components, from 0 to 255.
    StrTokenizer stok(cs, ":,");
    if (stok.count() != 3) throw ex_parse("RGB color must have three components!");
    float r, g, b;
    std::string R = stok.next(), G = stok.next(), B = stok.next();
    valueOf(trim(R), r);
    valueOf(trim(G), g);
    valueOf(trim(B), b);
    const float scale = args.text(2) == "RGB" ? 255.f : 1.f;
    c = rgb(r / scale, g / scale, b / scale);
  } else if (args.text(2) == "HTML") {
    // xcolor's HTML: six hexadecimal digits, RRGGBB.
    std::string hex = cs;
    trim(hex);
    if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
      throw ex_parse("HTML color must be six hexadecimal digits!");
    }
    const auto part = [&](std::size_t i) {
      return static_cast<float>(std::stoi(hex.substr(i, 2), nullptr, 16)) / 255.f;
    };
    c = rgb(part(0), part(2), part(4));
  } else if (args.text(2) == "cmyk") {
    StrTokenizer stok(cs, ":,");
    if (stok.count() != 4) throw ex_parse("CMYK color must have four components!");
    float cmyk[4];
    for (float& i : cmyk) {
      std::string X = stok.next();
      valueOf(trim(X), i);
    }
    float k = 1 - cmyk[3];
    c = rgb(k * (1 - cmyk[0]), k * (1 - cmyk[1]), k * (1 - cmyk[2]));
  } else {
    throw ex_parse("Invalid color model!");
  }

  ColorAtom::defineColor(args.text(1), c);
  return nullptr;
}

}  // namespace microtex
