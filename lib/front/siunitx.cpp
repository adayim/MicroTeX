#include "front/siunitx.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

/** `r` to `places` decimals or `figures` significant digits, as siunitx rounds (half up), padded with zeros when it
 *  has fewer. */
void roundReal(Real& r, const Options& o) {
  if (o.round == Options::Round::none || r.empty()) return;
  std::string d = r.whole + r.fraction;
  const int point = static_cast<int>(r.whole.size());
  int cut = 0;
  if (o.round == Options::Round::places) {
    cut = point + o.precision;
  } else {
    std::size_t first = 0;
    while (first < d.size() && d[first] == '0') first++;
    if (first == d.size()) return;
    cut = static_cast<int>(first) + o.precision;
  }
  if (cut <= 0) return;
  int newPoint = point;
  if (cut >= static_cast<int>(d.size())) {
    if (o.pad) d.append(static_cast<std::size_t>(cut) - d.size(), '0');
  } else {
    const bool up = d[static_cast<std::size_t>(cut)] >= '5';
    d.resize(static_cast<std::size_t>(cut));
    if (cut < point) d.append(static_cast<std::size_t>(point - cut), '0');
    if (up) {
      std::size_t k = static_cast<std::size_t>(cut);
      while (k > 0) {
        if (d[k - 1] == '9') {
          d[k - 1] = '0';
          k--;
        } else {
          d[k - 1]++;
          break;
        }
      }
      if (k == 0) {
        d.insert(d.begin(), '1');
        newPoint++;
      }
    }
  }
  r.whole = d.substr(0, static_cast<std::size_t>(newPoint));
  r.fraction = d.substr(static_cast<std::size_t>(newPoint));
  r.decimal = !r.fraction.empty();
}

/** `1 +- 0.1` as 1.0(1): the uncertainty in the last digits of the value. False when it is not written so. */
bool compactUncertainty(Real& a, const Real& b) {
  if (!a.uncertainty.empty() || !b.uncertainty.empty()) return false;
  const auto zeros = [](const std::string& s) { return s.find_first_not_of('0') == std::string::npos; };
  std::string unc;
  if (b.decimal && zeros(b.whole)) {
    const std::size_t want = std::max(a.fraction.size(), b.fraction.size());
    a.fraction.resize(want, '0');
    unc = b.fraction;
    unc.resize(want, '0');
  } else if (!b.decimal && !a.decimal) {
    unc = b.whole;
  } else {
    return false;
  }
  const std::size_t first = unc.find_first_not_of('0');
  a.uncertainty = first == std::string::npos ? "0" : unc.substr(first);
  a.decimal = !a.fraction.empty();
  return true;
}

}  // namespace

Options parseOptions(const std::string& text, Options o) {
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t j = i;
    int depth = 0;
    for (; j < text.size() && !(text[j] == ',' && depth == 0); j++) {
      if (text[j] == '{') depth++;
      if (text[j] == '}') depth--;
    }
    const std::string item = trim(text.substr(i, j - i));
    const std::size_t eq = item.find('=');
    const std::string key = trim(item.substr(0, eq));
    std::string value = eq == std::string::npos ? "" : trim(item.substr(eq + 1));
    if (value.size() >= 2 && value.front() == '{' && value.back() == '}') value = trim(value.substr(1, value.size() - 2));
    if (key == "round-mode") {
      o.round = value == "places" ? Options::Round::places : value == "figures" ? Options::Round::figures : Options::Round::none;
    } else if (key == "round-precision") {
      o.precision = std::atoi(value.c_str());
    } else if (key == "round-pad") {
      o.pad = value != "false";
    } else if (key == "uncertainty-mode") {
      o.separate = value == "separate";
    } else if (key == "table-format") {
      // [sign] integers [. decimals] [e exponent]: the numbers are how many places the column keeps for each
      // (`-6.5` is a sign, six digits, the marker and five digits), not digits of a number.
      std::size_t k = 0;
      o.tableSign = k < value.size() && (value[k] == '-' || value[k] == '+') && (++k, true);
      const auto count = [&] {
        int n = 0;
        while (k < value.size() && std::isdigit(static_cast<unsigned char>(value[k]))) {
          n = std::min(n * 10 + (value[k] - '0'), 1000);
          k++;
        }
        return n;
      };
      o.tableIntegers = count();
      o.tableDecimals = 0;
      if (k < value.size() && (value[k] == '.' || value[k] == ',')) {
        k++;
        o.tableDecimals = count();
      }
      o.tableExponent = k < value.size();
    }
    i = j + 1;
  }
  return o;
}

std::vector<std::string> unreadOptions(const std::string& text) {
  std::vector<std::string> keys;
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t j = i;
    int depth = 0;
    for (; j < text.size() && !(text[j] == ',' && depth == 0); j++) {
      if (text[j] == '{') depth++;
      if (text[j] == '}') depth--;
    }
    const std::string item = trim(text.substr(i, j - i));
    const std::string key = trim(item.substr(0, item.find('=')));
    if (!key.empty() && !readsOption(key)) keys.push_back(key);
    i = j + 1;
  }
  return keys;
}

bool readsOption(const std::string& key) {
  return key == "round-mode" || key == "round-precision" || key == "round-pad" || key == "uncertainty-mode" ||
         key == "table-format";
}

std::string number(const std::string& text, std::string& problem, const Options& options) {
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
  // A comparator in front (input-comparators: <, >, =, \approx, \ge, \geq, \gg, \le, \leq, \ll, \sim) is set
  // as an ordinary symbol, close to the number (LuaLaTeX: \num{< 0.1} is \mathord{<}0.1).
  std::string comparator;
  static const char* const comparators[] = {"\\approx", "\\geq", "\\leq", "\\gg", "\\ll", "\\sim", "\\ge", "\\le",
                                            "<", ">", "=", "\xe2\x89\xa4", "\xe2\x89\xa5"};
  for (const char* c : comparators) {
    const std::size_t n = std::strlen(c);
    if (s.compare(0, n, c) != 0) continue;
    const std::string mark = c;
    comparator = "\\mathord{" + (mark == "\xe2\x89\xa4" ? std::string("\\leq") : mark == "\xe2\x89\xa5" ? std::string("\\geq") : mark) + "}";
    i = n;
    break;
  }
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
  if (i < s.size() && s[i] != '\0' && std::strchr("eEdD", s[i]) != nullptr) {
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
    roundReal(a, options);
    if (plusMinus && !options.separate && !hasExp) {
      // 1 +- 0.1 is 1.0(1); the uncertainty is not rounded on its own.
      Real value = a;
      if (compactUncertainty(value, b)) {
        plusMinus = false;
        a = value;
      }
    }
    if (plusMinus) roundReal(b, options);
    out = showReal(a);
    if (plusMinus) out += "\\pm" + showReal(b);
    if (hasExp && plusMinus) out = "\\left(" + out + "\\right)";
    if (hasExp) out += "\\times";
  }
  if (hasExp) out += "10^{" + expSign + exponent + "}";
  return comparator + out;
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
    // TeX ignores NUL; and strchr() finds it in any set, as the string's own
    // end, so a run of text would stop on it without moving on.
    if (c == '\0') {
      i++;
      continue;
    }
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
