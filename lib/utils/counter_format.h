#ifndef MICROTEX_COUNTER_FORMAT_H
#define MICROTEX_COUNTER_FORMAT_H

#include <cctype>
#include <string>

namespace microtex {

inline std::string listRoman(int n) {
  static const int v[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
  static const char* s[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
  std::string r;
  for (int k = 0; k < 13 && n > 0; k++) {
    while (n >= v[k]) {
      r += s[k];
      n -= v[k];
    }
  }
  return r;
}

inline std::string listAlph(int n) {
  std::string r;
  while (n > 0) {
    n--;
    r = static_cast<char>('a' + n % 26) + r;
    n /= 26;
  }
  return r;
}

inline std::string listToUpper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

/** A counter's value as LaTeX's \arabic, \roman, \Roman, \alph, \Alph and
 *  \fnsymbol write it, by the command's name. Empty for another name. */
inline std::string formatCounter(const std::string& style, int n) {
  if (style == "arabic") return std::to_string(n);
  if (style == "roman") return listRoman(n);
  if (style == "Roman") return listToUpper(listRoman(n));
  if (style == "alph") return listAlph(n);
  if (style == "Alph") return listToUpper(listAlph(n));
  if (style == "fnsymbol") {
    // \fnsymbol counts 1 to 9: * † ‡ § ¶ ‖ ** †† ‡‡.
    static const char* marks[] = {"*",        "†",       "‡",       "§",       "¶",
                                  "‖",   "**",           "††", "‡‡"};
    return n >= 1 && n <= 9 ? marks[n - 1] : std::string();
  }
  return std::string();
}

}  // namespace microtex

#endif  // MICROTEX_COUNTER_FORMAT_H
