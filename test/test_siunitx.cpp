// siunitx's numbers and units (lib/front/siunitx.h).

#include <string>

#include "check.h"
#include "front/siunitx.h"

using namespace microtex::front::siunitx;

namespace {

std::string num(const std::string& s) {
  std::string problem;
  return number(s, problem);
}

std::string un(const std::string& s) {
  std::string problem;
  return unit(s, {}, problem);
}

}  // namespace

TEST(siunitx_numbers_are_grouped_from_five_digits_and_exponents_are_powers_of_ten) {
  CHECK_EQ(num("1234"), std::string("1234"));
  CHECK_EQ(num("12345"), std::string("12\\,345"));
  CHECK_EQ(num("1234567.12345"), std::string("1\\,234\\,567.123\\,45"));
  CHECK_EQ(num("1.5e-4"), std::string("1.5\\times10^{-4}"));
  CHECK_EQ(num("2E+3"), std::string("2\\times10^{3}"));
  CHECK_EQ(num("e3"), std::string("10^{3}"));
  CHECK_EQ(num(".5"), std::string("0.5"));
  CHECK_EQ(num("1,5"), std::string("1{,}5"));
  CHECK_EQ(num("-3.2(1)"), std::string("{-}3.2(1)"));
  CHECK_EQ(num("1.2+-0.3"), std::string("1.2(3)"));
  CHECK_EQ(num("1.2+-0.3e3"), std::string("\\left(1.2\\pm0.3\\right)\\times10^{3}"));
}

TEST(siunitx_a_number_that_is_not_one_is_said_and_returned_as_it_is) {
  std::string problem;
  CHECK_EQ(number("12x4", problem), std::string("\\mathrm{12x4}"));
  CHECK(problem.find("not a number") != std::string::npos);
  problem.clear();
  CHECK_EQ(number("1e", problem), std::string("\\mathrm{1e}"));
  CHECK(!problem.empty());
  problem.clear();
  CHECK_EQ(number("", problem), std::string(""));
  CHECK(problem.empty());
  CHECK_EQ(number("--1", problem), std::string("\\mathrm{--1}"));
}

TEST(siunitx_units_are_macros_or_text_with_per_a_negative_power) {
  CHECK_EQ(un("\\meter\\per\\second"), std::string("\\mathrm{m}\\,\\mathrm{s}^{-1}"));
  CHECK_EQ(un("\\kilo\\gram\\metre\\per\\second\\squared"),
           std::string("\\mathrm{kg}\\,\\mathrm{m}\\,\\mathrm{s}^{-2}"));
  CHECK_EQ(un("\\square\\metre"), std::string("\\mathrm{m}^{2}"));
  CHECK_EQ(un("\\metre\\tothe{4}"), std::string("\\mathrm{m}^{4}"));
  CHECK_EQ(un("\\metre\\of{eff}"), std::string("\\mathrm{m}_{\\mathrm{eff}}"));
  CHECK_EQ(un("m.s^{-1}"), std::string("\\mathrm{m}\\,\\mathrm{s}^{-1}"));
  CHECK_EQ(un("kg/m^3"), std::string("\\mathrm{kg}\\,\\mathrm{m}^{-3}"));
  CHECK_EQ(un("m/s^2"), std::string("\\mathrm{m}\\,\\mathrm{s}^{-2}"));
  CHECK_EQ(un("\\ohm"), std::string("\\mathrm{\\Omega}"));
  CHECK_EQ(un(""), std::string(""));
}

TEST(siunitx_an_unknown_unit_is_said_and_drawn_as_its_name) {
  std::string problem;
  CHECK_EQ(unit("\\wibble", {}, problem), std::string("\\mathrm{wibble}"));
  CHECK(problem.find("unknown unit") != std::string::npos);
  // One that was declared.
  problem.clear();
  CHECK_EQ(unit("\\foo\\per\\second", {{"foo", "bar"}}, problem),
           std::string("\\mathrm{bar}\\,\\mathrm{s}^{-1}"));
  CHECK(problem.empty());
}

TEST(siunitx_angles_ranges_and_lists) {
  std::string problem;
  CHECK_EQ(angle("12.5", problem), std::string("12.5{}^{\\circ}"));
  CHECK_EQ(angle("1;2;3", problem), std::string("1{}^{\\circ}2{}^{\\prime}3{}^{\\prime\\prime}"));
  CHECK_EQ(angle(";;30", problem), std::string("30{}^{\\prime\\prime}"));
  CHECK(problem.empty());
  CHECK_EQ(angle("1;2;3;4", problem), std::string("\\mathrm{1;2;3;4}"));
  CHECK(!problem.empty());
  problem.clear();
  CHECK_EQ(numberList("1;2;3", problem), std::string("1\\text{, }2\\text{, and }3"));
  CHECK_EQ(numberList("1;2", problem), std::string("1\\text{ and }2"));
  CHECK_EQ(range("1", "10"), std::string("1\\text{ to }10"));
}

TEST(siunitx_an_uncertainty_is_the_last_digits_of_the_value_and_options_round) {
  // pdflatex (siunitx 3): \num{1 +- 0.1} is 1.0(1), \num{22.25 +- 0.01} 22.25(1), widths within a point.
  CHECK_EQ(num("1 +- 0.1"), std::string("1.0(1)"));
  CHECK_EQ(num("1.234 +- 0.005"), std::string("1.234(5)"));
  CHECK_EQ(num("22.25 +- 0.01"), std::string("22.25(1)"));
  CHECK_EQ(num("10 +- 2"), std::string("10(2)"));
  CHECK_EQ(num("3 +- 0.5"), std::string("3.0(5)"));
  CHECK_EQ(num("1.2 +- 0.05"), std::string("1.20(5)"));
  // `uncertainty-mode=separate` keeps it as written.
  std::string problem;
  CHECK_EQ(number("1.0 +- 0.1", problem, parseOptions("uncertainty-mode=separate")), std::string("1.0\\pm0.1"));
  // round-mode: places, or figures, half up, padded with zeros (pdflatex: \num[round-mode=places]{12345} is 12 345.00).
  const auto round = [&](const char* options, const char* n) { return number(n, problem, parseOptions(options)); };
  CHECK_EQ(round("round-mode=places,round-precision=1", "3.14159"), std::string("3.1"));
  CHECK_EQ(round("round-mode=places,round-precision=2", "0.999"), std::string("1.00"));
  CHECK_EQ(round("round-mode=places", "12345"), std::string("12\\,345.00"));
  CHECK_EQ(round("round-mode=places,round-pad=false", "12345"), std::string("12\\,345"));
  CHECK_EQ(round("round-mode=places,round-precision=3", "1.5"), std::string("1.500"));
  CHECK_EQ(round("round-mode=figures,round-precision=3", "12345.678"), std::string("12\\,300"));
  CHECK_EQ(round("round-mode=figures,round-precision=2", "0.001234"), std::string("0.0012"));
}
