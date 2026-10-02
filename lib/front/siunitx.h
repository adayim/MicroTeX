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

/** `text` as a number, in LaTeX for math mode. A problem is said in
 *  `problem` and the text is returned as it is written. */
std::string number(const std::string& text, std::string& problem);

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
