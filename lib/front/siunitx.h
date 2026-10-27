#ifndef GRIDMICROTEX_FRONT_SIUNITX_H
#define GRIDMICROTEX_FRONT_SIUNITX_H

// A subset of siunitx: \num, \si / \unit, \SI / \qty, \ang, the ranges and
// the lists, which format a number or a unit as LaTeX. What they read is the
// package's own reading of it: a number's sign, digits, decimal marker,
// uncertainty and exponent, and a unit written with siunitx's macros
// (\kilo\meter\per\second\squared) or as text (m.s^-1, kg m/s^2).

#include <map>
#include <string>
#include <vector>

namespace microtex::front::siunitx {

/** User units made with \DeclareSIUnit: name (without the backslash) to the
 *  text that stands for it. */
using UserUnits = std::map<std::string, std::string>;

/** What siunitx's options say of a number: `round-mode` (`places` or `figures`) with `round-precision` (and
 *  `round-pad`), and `uncertainty-mode=separate`, which keeps a `+-` as it is where the compact one writes it as the
 *  digits of the value (1.0(1)). The rest of the options change how it looks, which is left out. */
struct Options {
  enum class Round { none, places, figures } round = Round::none;
  int precision = 2;
  bool pad = true;
  bool separate = false;
  /** `table-format=-3.2` of an `S` column: room for a sign, 3 digits before the marker and 2 after it. -1 where
   *  not given; `tableExponent` says the format has an exponent part, which is not read. */
  bool tableSign = false;
  int tableIntegers = -1;
  int tableDecimals = -1;
  bool tableExponent = false;
};

/** The options of `[round-mode=places, round-precision=1]`, over those in `base`. */
Options parseOptions(const std::string& text, Options base = Options());

/** Whether parseOptions() reads the option `key`: the others change how a number looks, which is left out. */
bool readsOption(const std::string& key);

/** The keys of `text` that parseOptions() does not read. */
std::vector<std::string> unreadOptions(const std::string& text);

/** `text` as a number, in LaTeX for math mode. A problem is said in
 *  `problem` and the text is returned as it is written. */
std::string number(const std::string& text, std::string& problem, const Options& options = Options());

/** `text` as a unit, in LaTeX for math mode. */
std::string unit(const std::string& text, const UserUnits& declared, std::string& problem);

/** An angle: one number of degrees, or degrees;minutes;seconds. */
std::string angle(const std::string& text, std::string& problem);

/** The numbers of a list `a;b;c`, said as "a, b, and c". */
std::string numberList(const std::string& text, std::string& problem);

/** Two numbers (or quantities) said as a range, "a to b". */
std::string range(const std::string& from, const std::string& to);

}  // namespace microtex::front::siunitx

#endif
