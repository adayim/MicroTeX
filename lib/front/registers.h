#pragma once

// The values of TeX's registers: \count (integers), \dimen (lengths) and \skip (glue), as the expander keeps them.
//
// A length here is a sum of three parts, in TeX points, ems and exs -- `Dim`. TeX knows the font while it reads `2em` and
// stores a size; the expander does not (the font is the render's), so it keeps what was written and adds, subtracts and scales
// the parts. A length that is only one part prints as it was written (`1.5em`), which is what the lowering wants; one made of
// several (`1em + 2pt`) is compared and printed with the nominal font of a 10pt page, em = 10pt and ex = 4.31pt (Latin Modern
// Roman at 10pt: the sizes the audits are set in). The unit table, the sp rounding and `print_scaled` are tex.web's (sections
// 448-458, 103).

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace microtex::front {

constexpr double kEmPt = 10.0;   // a nominal em, in points
constexpr double kExPt = 4.31;   // and a nominal ex

struct Dim {
  double pt = 0, em = 0, ex = 0;

  bool only(double Dim::*part) const {
    return (part == &Dim::pt || pt == 0) && (part == &Dim::em || em == 0) && (part == &Dim::ex || ex == 0);
  }
  double nominal() const { return pt + em * kEmPt + ex * kExPt; }
  /** The size in scaled points (1/65536 pt), as TeX holds it. */
  std::int64_t sp() const { return static_cast<std::int64_t>(std::llround(nominal() * 65536.0)); }
  Dim operator+(const Dim& o) const { return {pt + o.pt, em + o.em, ex + o.ex}; }
  Dim operator-(const Dim& o) const { return {pt - o.pt, em - o.em, ex - o.ex}; }
  Dim operator-() const { return {-pt, -em, -ex}; }
  Dim scaled(double f) const { return {pt * f, em * f, ex * f}; }
};

/** Glue: a natural size and how far it stretches and shrinks; `order` is 0 for points, 1 to 3 for fil, fill, filll. */
struct Glue {
  Dim natural, stretch, shrink;
  int stretchOrder = 0, shrinkOrder = 0;
};

/** A register: `c` count, `d` dimen, `s` skip, `t` toks, and its number. `unknown` is a value the expander could not work out (a length of
 *  the lowering's, `\linewidth`), which it leaves to the lowering: the register then stays a name in the output. */
using RegKey = std::pair<char, int>;
struct Reg {
  std::int64_t n = 0;
  Glue g;  // a \dimen's value is g.natural
  std::string text;  // a \toks register's tokens, as written
  bool unknown = false;
};

/** tex.web's print_scaled: the shortest decimal that reads back as the same number of scaled points. */
inline std::string printScaled(std::int64_t s) {
  std::string out;
  if (s < 0) {
    out += '-';
    s = -s;
  }
  out += std::to_string(s / 65536);
  out += '.';
  s = 10 * (s % 65536) + 5;
  std::int64_t delta = 10;
  do {
    if (delta > 65536) s = s + 32768 - 50000;  // round the last digit
    out += static_cast<char>('0' + s / 65536);
    s = 10 * (s % 65536);
    delta *= 10;
  } while (s > delta);
  return out;
}

/** What `\the` gives for a length: `5.0pt`, or `1.5em` when it is only ems. */
inline std::string theDim(const Dim& d) {
  const auto scaled = [](double v) { return printScaled(static_cast<std::int64_t>(std::llround(v * 65536.0))); };
  if (d.em != 0 && d.only(&Dim::em)) return scaled(d.em) + "em";
  if (d.ex != 0 && d.only(&Dim::ex)) return scaled(d.ex) + "ex";
  return printScaled(d.sp()) + "pt";
}

inline std::string theGlue(const Glue& g) {
  const auto part = [](const Dim& d, int order) {
    if (order == 0) return theDim(d);
    const std::string f = printScaled(static_cast<std::int64_t>(std::llround(d.pt * 65536.0)));
    return f + (order == 1 ? "fil" : order == 2 ? "fill" : "filll");
  };
  std::string out = theDim(g.natural);
  if (g.stretch.nominal() != 0 || g.stretchOrder != 0) out += " plus " + part(g.stretch, g.stretchOrder);
  if (g.shrink.nominal() != 0 || g.shrinkOrder != 0) out += " minus " + part(g.shrink, g.shrinkOrder);
  return out;
}

/** The length a unit stands for, per one of it (`2cm`: 2 times this); false for a word that is none. `mu` is math units, an
 *  eighteenth of an em. The second result says whether the unit is a font's (em, ex) for the part it belongs to. */
inline bool unitOf(const std::string& u, Dim& per) {
  per = Dim();
  if (u == "pt") per.pt = 1;
  else if (u == "pc") per.pt = 12;
  else if (u == "in") per.pt = 72.27;
  else if (u == "bp") per.pt = 72.27 / 72.0;
  else if (u == "cm") per.pt = 72.27 / 2.54;
  else if (u == "mm") per.pt = 72.27 / 25.4;
  else if (u == "dd") per.pt = 1238.0 / 1157.0;
  else if (u == "cc") per.pt = 12.0 * 1238.0 / 1157.0;
  else if (u == "sp") per.pt = 1.0 / 65536.0;
  else if (u == "em") per.em = 1;
  else if (u == "ex") per.ex = 1;
  else if (u == "mu") per.em = 1.0 / 18.0;
  else return false;
  return true;
}

}  // namespace microtex::front
