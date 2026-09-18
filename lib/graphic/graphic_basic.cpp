#include "graphic/graphic_basic.h"

#include "atom/atom_basic.h"

// "#RRGGBB", or "#AARRGGBB". Parsed a digit at a time into the unsigned
// colour: strtol() overflows on eight hex digits wherever long is 32 bits
// (Windows), which turned every colour with alpha of 0x80 or more black.
microtex::color microtex::decodeColor(const std::string& s) {
  const size_t n = s.size();
  if ((n != 7 && n != 9) || s[0] != '#') return black;
  color c = 0;
  for (size_t i = 1; i < n; i++) {
    const char ch = s[i];
    color d;
    if (ch >= '0' && ch <= '9') d = ch - '0';
    else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
    else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
    else return black;
    c = (c << 4) | d;
  }
  if (n == 7) c |= 0xff000000;
  return c;
}

microtex::color microtex::getColor(const std::string& name) {
  return ColorAtom::getColor(name);
}

bool microtex::Point::operator==(const Point& other) const {
  return x == other.x && y == other.y;
}

bool microtex::Rect::operator==(const Rect& other) const {
  return x == other.x && y == other.y && w == other.w && h == other.h;
}

// clang-format off
bool microtex::Stroke::operator==(const Stroke& other) const {
  return lineWidth == other.lineWidth
         && miterLimit == other.miterLimit
         && cap == other.cap
         && join == other.join;
}
// clang-format on
