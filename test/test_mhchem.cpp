// A subset of mhchem (lib/front/mhchem.h): \ce's formulas, charges,
// coefficients, states, bonds and arrows, and \pu's number and unit.

#include <string>

#include "check.h"
#include "front/mhchem.h"

using namespace microtex::front::mhchem;

namespace {

std::string chem(const std::string& s) {
  std::string problem;
  return ce(s, problem);
}

std::string unitOf(const std::string& s) {
  std::string problem;
  return pu(s, {}, problem);
}

}  // namespace

TEST(mhchem_formulas_have_upright_elements_and_subscripted_counts) {
  CHECK_EQ(chem("H2O"), std::string("\\mathrm{H}_{2}\\mathrm{O}"));
  CHECK_EQ(chem("Ca(OH)2"), std::string("\\mathrm{Ca}(\\mathrm{O}\\mathrm{H})_{2}"));
  CHECK_EQ(chem("[Zn(OH)4]^2-"),
           std::string("[\\mathrm{Zn}(\\mathrm{O}\\mathrm{H})_{4}]^{2-}"));
}

TEST(mhchem_a_sign_after_a_formula_is_its_charge_and_between_spaces_an_operator) {
  CHECK_EQ(chem("Na+"), std::string("\\mathrm{Na}^{+}"));
  CHECK_EQ(chem("e-"), std::string("\\mathrm{e}^{-}"));
  CHECK_EQ(chem("SO4^2-"), std::string("\\mathrm{S}\\mathrm{O}_{4}^{2-}"));
  CHECK_EQ(chem("Fe^{3+}"), std::string("\\mathrm{Fe}^{3+}"));
  CHECK_EQ(chem("A + B"), std::string("\\mathrm{A}{}+{}\\mathrm{B}"));
}

TEST(mhchem_a_number_before_a_formula_is_a_coefficient) {
  CHECK_EQ(chem("2H2O"), std::string("2\\,\\mathrm{H}_{2}\\mathrm{O}"));
  CHECK_EQ(chem("2 H2O"), std::string("2\\,\\mathrm{H}_{2}\\mathrm{O}"));
  CHECK_EQ(chem("1/2 O2"), std::string("\\frac{1}{2}\\,\\mathrm{O}_{2}"));
  CHECK_EQ(chem("0.5 O2"), std::string("0.5\\,\\mathrm{O}_{2}"));
}

TEST(mhchem_arrows_are_long_and_stretch_over_their_text) {
  CHECK_EQ(chem("A -> B"), std::string("\\mathrm{A}\\longrightarrow\\mathrm{B}"));
  CHECK_EQ(chem("A <- B"), std::string("\\mathrm{A}\\longleftarrow\\mathrm{B}"));
  CHECK_EQ(chem("A <=> B"), std::string("\\mathrm{A}\\rightleftharpoons\\mathrm{B}"));
  CHECK_EQ(chem("A ->[x] B"), std::string("\\mathrm{A}\\xrightarrow{\\mathrm{x}}\\mathrm{B}"));
  CHECK_EQ(chem("A <=>[a][b] B"),
           std::string("\\mathrm{A}\\xrightleftharpoons[\\mathrm{b}]{\\mathrm{a}}\\mathrm{B}"));
}

TEST(mhchem_states_bonds_isotopes_and_precipitates) {
  CHECK_EQ(chem("H2O(l)"), std::string("\\mathrm{H}_{2}\\mathrm{O}\\mathrm{(l)}"));
  CHECK_EQ(chem("C-C"), std::string("\\mathrm{C}{-}\\mathrm{C}"));
  CHECK_EQ(chem("^{227}_{90}Th"), std::string("{}^{227}_{90}\\mathrm{Th}"));
  CHECK_EQ(chem("A v"), std::string("\\mathrm{A}\\downarrow"));
  CHECK_EQ(chem("A ^"), std::string("\\mathrm{A}\\uparrow"));
  CHECK_EQ(chem("CuSO4*5H2O"),
           std::string("\\mathrm{Cu}\\mathrm{S}\\mathrm{O}_{4}{\\cdot}5\\,\\mathrm{H}_{2}\\mathrm{O}"));
}

TEST(mhchem_math_and_commands_pass_through) {
  CHECK_EQ(chem("$x$"), std::string("x"));
  CHECK_EQ(chem("\\alpha"), std::string("\\alpha"));
  CHECK_EQ(chem("{A}"), std::string("{\\mathrm{A}}"));
}

TEST(mhchem_pu_is_a_number_and_an_upright_unit) {
  CHECK_EQ(unitOf("1.5 mol/L"), std::string("1.5\\,\\mathrm{mol}{/}\\mathrm{L}"));
  CHECK_EQ(unitOf("12 mol L-1"), std::string("12\\,\\mathrm{mol}\\,\\mathrm{L}^{-1}"));
  CHECK_EQ(unitOf("kJ.mol-1"), std::string("\\mathrm{kJ}\\,\\mathrm{mol}^{-1}"));
  CHECK_EQ(unitOf(""), std::string(""));
}
