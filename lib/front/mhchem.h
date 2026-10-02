#ifndef GRIDMICROTEX_FRONT_MHCHEM_H
#define GRIDMICROTEX_FRONT_MHCHEM_H

// A subset of mhchem, the part KaTeX has too: \ce for formulas, charges,
// stoichiometric coefficients, states, bonds, isotopes and reaction arrows
// (with their text), and \pu for a number and its unit. What they read is
// mhchem's own reading of it; what they make is LaTeX for math mode.

#include <string>

#include "front/siunitx.h"

namespace microtex::front::mhchem {

/** The text of a `\ce{...}`, as LaTeX for math mode. A problem is said in
 *  `problem`; what could not be read is passed through. */
std::string ce(const std::string& text, std::string& problem);

/** The text of a `\pu{...}`: a number and a unit. */
std::string pu(const std::string& text, const siunitx::UserUnits& declared, std::string& problem);

}  // namespace microtex::front::mhchem

#endif
