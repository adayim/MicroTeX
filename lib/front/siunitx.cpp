#include "front/siunitx.h"

#include <cctype>
#include <cstring>

namespace microtex::front::siunitx {

namespace {

std::string trim(const std::string& s) {
  std::size_t from = 0, to = s.size();
  while (from < to && std::isspace(static_cast<unsigned char>(s[from])) != 0) from++;
  while (to > from && std::isspace(static_cast<unsigned char>(s[to - 1])) != 0) to--;
  return s.substr(from, to - from);
}

bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

// --- numbers -----------------------------------------------------------------

struct Real {
  std::string sign, whole, fraction, uncertainty;
  char marker = '.';
  bool decimal = false;
  bool empty() const { return whole.empty() && fraction.empty(); }
};

/** siunitx groups the digits of a number of five or more in threes. */
std::string groupWhole(const std::string& d) {
  if (d.size() < 5) return d;
  std::string out;
  for (std::size_t i = 0; i < d.size(); i++) {
    if (i > 0 && (d.size() - i) % 3 == 0) out += "\\,";
    out += d[i];
  }
  return out;
}

std::string groupFraction(const std::string& d) {
  if (d.size() < 5) return d;
  std::string out;
  for (std::size_t i = 0; i < d.size(); i++) {
    if (i > 0 && i % 3 == 0) out += "\\,";
    out += d[i];
  }
  return out;
}

bool readReal(const std::string& s, std::size_t& i, Real& r) {
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) r.sign = std::string(1, s[i++]);
  while (i < s.size() && digit(s[i])) r.whole += s[i++];
  if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
    r.decimal = true;
    r.marker = s[i++];
    while (i < s.size() && digit(s[i])) r.fraction += s[i++];
  }
  if (r.empty()) return false;
  // A compact uncertainty: 1.23(4).
  if (i < s.size() && s[i] == '(') {
    const std::size_t close = s.find(')', i);
    if (close == std::string::npos) return false;
    r.uncertainty = s.substr(i + 1, close - i - 1);
    i = close + 1;
  }
  return true;
}

std::string showReal(const Real& r) {
  std::string out = r.sign.empty() ? "" : "{" + r.sign + "}";
  out += r.whole.empty() ? "0" : groupWhole(r.whole);
  if (r.decimal) out += (r.marker == ',' ? "{,}" : ".") + groupFraction(r.fraction);
  if (!r.uncertainty.empty()) out += "(" + r.uncertainty + ")";
  return out;
}

}  // namespace

std::string number(const std::string& text, std::string& problem) {
  std::string s;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) == 0) s += c;
  }
  const auto bad = [&] {
    problem = "`" + trim(text) + "' is not a number";
    return "\\mathrm{" + trim(text) + "}";
  };
  if (s.empty()) return "";
  std::size_t i = 0;
  Real a, b;
  const bool hasA = readReal(s, i, a);
  bool plusMinus = false;
  if (i < s.size() && (s.compare(i, 2, "+-") == 0 || s.compare(i, 4, "\\pm") == 0 ||
                       s.compare(i, 2, "\xc2\xb1") == 0)) {
    i += s.compare(i, 2, "+-") == 0 ? 2 : s.compare(i, 4, "\\pm") == 0 ? 4 : 2;
    plusMinus = true;
    if (!readReal(s, i, b)) return bad();
  }
  // An exponent: 1e3, 1.5E-4, and 1d2 as TeX users write it.
  std::string exponent, expSign;
  bool hasExp = false;
  if (i < s.size() && std::strchr("eEdD", s[i]) != nullptr) {
    std::size_t j = i + 1;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) expSign = s[j++] == '-' ? "-" : "";
    while (j < s.size() && digit(s[j])) exponent += s[j++];
    if (!exponent.empty()) {
      hasExp = true;
      i = j;
    }
  }
  if (i != s.size() || (!hasA && !hasExp)) return bad();
  std::string out;
  if (hasA) {
    out = showReal(a);
    if (plusMinus) out += "\\pm" + showReal(b);
    if (hasExp && plusMinus) out = "\\left(" + out + "\\right)";
    if (hasExp) out += "\\times";
  }
  if (hasExp) out += "10^{" + expSign + exponent + "}";
  return out;
}

std::string angle(const std::string& text, std::string& problem) {
  std::vector<std::string> parts{""};
  for (const char c : text) {
    if (c == ';') {
      parts.emplace_back();
    } else {
      parts.back() += c;
    }
  }
  if (parts.size() > 3) {
    problem = "an angle has degrees, minutes and seconds only";
    return "\\mathrm{" + trim(text) + "}";
  }
  static const char* marks[] = {"{}^{\\circ}", "{}^{\\prime}", "{}^{\\prime\\prime}"};
  std::string out;
  for (std::size_t k = 0; k < parts.size(); k++) {
    if (trim(parts[k]).empty()) continue;
    std::string why;
    out += number(parts[k], why) + marks[k];
    if (!why.empty()) problem = why;
  }
  return out;
}

std::string numberList(const std::string& text, std::string& problem) {
  std::vector<std::string> items{""};
  for (const char c : text) {
    if (c == ';') {
      items.emplace_back();
    } else {
      items.back() += c;
    }
  }
  std::string out;
  for (std::size_t k = 0; k < items.size(); k++) {
    std::string why;
    if (k > 0) out += items.size() == 2 ? "\\text{ and }" : k + 1 == items.size() ? "\\text{, and }" : "\\text{, }";
    out += number(items[k], why);
    if (!why.empty()) problem = why;
  }
  return out;
}

std::string range(const std::string& from, const std::string& to) {
  return from + "\\text{ to }" + to;
}

// --- units -------------------------------------------------------------------

namespace {

struct Name {
  const char* name;
  const char* symbol;
};

const Name kPrefixes[] = {
  {"yocto", "y"}, {"zepto", "z"}, {"atto", "a"},   {"femto", "f"}, {"pico", "p"},
  {"nano", "n"},  {"micro", "\xc2\xb5"}, {"milli", "m"}, {"centi", "c"}, {"deci", "d"},
  {"deca", "da"}, {"hecto", "h"}, {"kilo", "k"},   {"mega", "M"},  {"giga", "G"},
  {"tera", "T"},  {"peta", "P"},  {"exa", "E"},    {"zetta", "Z"}, {"yotta", "Y"},
  {"kibi", "Ki"}, {"mebi", "Mi"}, {"gibi", "Gi"},  {"tebi", "Ti"}, {"pebi", "Pi"},
};

const Name kUnits[] = {
  {"ampere", "A"},     {"candela", "cd"},   {"kelvin", "K"},      {"kilogram", "kg"},
  {"gram", "g"},       {"meter", "m"},      {"metre", "m"},       {"mole", "mol"},
  {"second", "s"},     {"becquerel", "Bq"}, {"degreeCelsius", "{}^{\\circ}C"},
  {"celsius", "{}^{\\circ}C"},              {"coulomb", "C"},     {"farad", "F"},
  {"gray", "Gy"},      {"hertz", "Hz"},     {"henry", "H"},       {"joule", "J"},
  {"lumen", "lm"},     {"lux", "lx"},       {"newton", "N"},      {"ohm", "\\Omega"},
  {"pascal", "Pa"},    {"radian", "rad"},   {"siemens", "S"},     {"sievert", "Sv"},
  {"steradian", "sr"}, {"tesla", "T"},      {"volt", "V"},        {"watt", "W"},
  {"weber", "Wb"},     {"day", "d"},        {"degree", "{}^{\\circ}"},
  {"hectare", "ha"},   {"hour", "h"},       {"litre", "L"},       {"liter", "L"},
  {"arcminute", "{}^{\\prime}"},            {"arcsecond", "{}^{\\prime\\prime}"},
  {"minute", "min"},   {"tonne", "t"},      {"astronomicalunit", "au"},
  {"bel", "B"},        {"decibel", "dB"},   {"dalton", "Da"},     {"electronvolt", "eV"},
  {"neper", "Np"},     {"angstrom", "\xc3\x85"}, {"bar", "bar"},  {"barn", "b"},
  {"percent", "\\%"},  {"byte", "B"},       {"bit", "bit"},       {"atomicmassunit", "u"},
  {"gal", "Gal"},      {"molar", "M"},      {"mmHg", "mmHg"},     {"knot", "kn"},
};

const char* find(const Name* table, std::size_t n, const std::string& name) {
  for (std::size_t i = 0; i < n; i++) {
    if (name == table[i].name) return table[i].symbol;
  }
  return nullptr;
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

struct Item {
  std::string symbol;
  std::string power;
  std::string below;  // a subscript, \of{...}
  bool per = false;
};

std::string negate(const std::string& p) {
  if (p.empty()) return "-1";
  return p[0] == '-' ? p.substr(1) : "-" + p;
}

/** The group or the single character at s[i], and its end. */
std::string argument(const std::string& s, std::size_t& i) {
  while (i < s.size() && s[i] == ' ') i++;
  if (i >= s.size()) return "";
  if (s[i] != '{') return std::string(1, s[i++]);
  int depth = 0;
  const std::size_t from = i + 1;
  for (; i < s.size(); i++) {
    if (s[i] == '{') depth++;
    if (s[i] == '}' && --depth == 0) {
      const std::string inner = s.substr(from, i - from);
      i++;
      return inner;
    }
  }
  return s.substr(from);
}

}  // namespace

std::string unit(const std::string& text, const UserUnits& declared, std::string& problem) {
  std::vector<Item> items;
  std::string prefix, before;
  bool per = false;
  const std::string& s = text;
  std::size_t i = 0;
  const auto unknown = [&](const std::string& name) {
    problem = "unknown unit \\" + name;
  };
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\\') {
      std::size_t j = i + 1;
      while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])) != 0) j++;
      const std::string name = s.substr(i + 1, j - i - 1);
      i = j;
      if (name.empty()) {  // a control symbol: `\,` and kin are spaces
        i = std::min(s.size(), i + 1);
        continue;
      }
      if (name == "per") {
        per = true;
      } else if (name == "square") {
        before = "2";
      } else if (name == "cubic") {
        before = "3";
      } else if (name == "raiseto") {
        before = argument(s, i);
      } else if (name == "squared" || name == "cubed" || name == "tothe") {
        const std::string p = name == "squared" ? "2" : name == "cubed" ? "3" : argument(s, i);
        if (!items.empty()) items.back().power = p;
      } else if (name == "of") {
        const std::string q = argument(s, i);
        if (!items.empty()) items.back().below = q;
      } else if (const char* pre = find(kPrefixes, COUNT(kPrefixes), name)) {
        prefix += pre;
      } else {
        const char* sym = find(kUnits, COUNT(kUnits), name);
        std::string symbol = sym != nullptr ? sym : "";
        const auto user = declared.find(name);
        if (user != declared.end()) symbol = user->second;
        if (sym == nullptr && user == declared.end()) {
          unknown(name);
          symbol = name;
        }
        Item item;
        item.symbol = prefix + symbol;
        item.power = before;
        item.per = per;
        items.push_back(item);
        prefix.clear();
        before.clear();
      }
      continue;
    }
    if (c == '^' || c == '_') {
      i++;
      const std::string a = argument(s, i);
      if (!items.empty()) (c == '^' ? items.back().power : items.back().below) = a;
      continue;
    }
    if (c == '/') {
      per = true;
      i++;
      continue;
    }
    if (c == '.' || c == '~' || c == ' ' || c == '{' || c == '}') {
      i++;
      continue;
    }
    // A unit written as text: letters up to the next separator.
    std::size_t j = i;
    while (j < s.size() && std::strchr("\\^_/.~ {}", s[j]) == nullptr) j++;
    Item item;
    item.symbol = s.substr(i, j - i);
    item.per = per;
    items.push_back(item);
    i = j;
  }
  std::string out;
  for (const Item& it : items) {
    if (!out.empty()) out += "\\,";
    out += "\\mathrm{" + it.symbol + "}";
    if (!it.below.empty()) out += "_{\\mathrm{" + it.below + "}}";
    const std::string p = it.per ? negate(it.power) : it.power;
    if (!p.empty()) out += "^{" + p + "}";
  }
  return out;
}

}  // namespace microtex::front::siunitx
