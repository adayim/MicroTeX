#include "front/mhchem.h"

#include <cctype>
#include <cstring>

namespace microtex::front::mhchem {

namespace {

bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string trimmed(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && isSpace(s[a])) a++;
  while (b > a && isSpace(s[b - 1])) b--;
  return s.substr(a, b - a);
}

/** One `\ce`'s text, read left to right: an item is a formula, a number
 *  before it, or an operator, and what follows an item with no space is
 *  part of it (a subscript, a charge, a bond). */
class Reader {
public:
  Reader(const std::string& text, std::string& problem) : s(text), problem(problem) {}

  std::string run() {
    while (more()) step();
    return out;
  }

private:
  const std::string s;
  std::string& problem;
  std::size_t i = 0;
  std::string out;
  /** The last thing written takes a subscript or a charge. */
  bool atom = false;
  /** At the start of an item, where a coefficient may be. */
  bool start = true;
  /** A space came right before this. */
  bool spaced = true;

  bool more() const { return i < s.size(); }
  char at(std::size_t k = 0) const { return i + k < s.size() ? s[i + k] : '\0'; }
  bool startsWith(const char* t) const { return s.compare(i, std::strlen(t), t) == 0; }

  /** The text between the `open` at the cursor and its `close`. */
  std::string balanced(char open, char close) {
    int depth = 0;
    const std::size_t from = i + 1;
    for (; i < s.size(); i++) {
      if (s[i] == '\\' && i + 1 < s.size()) {
        i++;
      } else if (s[i] == open) {
        depth++;
      } else if (s[i] == close && --depth == 0) {
        std::string inner = s.substr(from, i - from);
        i++;
        return inner;
      }
    }
    problem = std::string("missing '") + close + "'";
    return s.substr(from);
  }

  /** The text of a charge or a script, after `^` or `_`. */
  std::string script(bool charge) {
    if (at() == '{') return balanced('{', '}');
    std::string run;
    if (charge) {
      while (isDigit(at())) run += s[i++];
      while (at() == '+' || at() == '-') run += s[i++];
    } else {
      while (isDigit(at())) run += s[i++];
    }
    if (run.empty() && more()) run = std::string(1, s[i++]);
    return run;
  }

  /** The text of an arrow's `[...]`, read as a `\ce` of its own. */
  std::string label() {
    if (at() != '[') return "";
    return Reader(balanced('[', ']'), problem).run();
  }

  bool arrow() {
    struct Arrow {
      const char* text;
      const char* plain;
      const char* stretch;
    };
    static const Arrow arrows[] = {
      {"<=>>", "\\rightleftharpoons", "\\xrightleftharpoons"},
      {"<<=>", "\\rightleftharpoons", "\\xrightleftharpoons"},
      {"<=>", "\\rightleftharpoons", "\\xrightleftharpoons"},
      {"<->", "\\longleftrightarrow", "\\xleftrightarrow"},
      {"<-", "\\longleftarrow", "\\xleftarrow"},
      {"->", "\\longrightarrow", "\\xrightarrow"},
      {"<>", "\\leftrightarrow", "\\xleftrightarrow"},
    };
    for (const Arrow& a : arrows) {
      if (!startsWith(a.text)) continue;
      i += std::strlen(a.text);
      const std::string above = label();
      const std::string below = label();
      if (above.empty() && below.empty()) {
        out += a.plain;
      } else {
        out += a.stretch;
        if (!below.empty()) out += "[" + below + "]";
        out += "{" + above + "}";
      }
      atom = false;
      start = true;
      return true;
    }
    return false;
  }

  /** A coefficient: 2, 0.5 or 1/2, and the thin space after it. */
  void coefficient() {
    std::string num;
    while (isDigit(at()) || (at() == '.' && isDigit(at(1)))) num += s[i++];
    if (at() == '/' && isDigit(at(1))) {
      i++;
      std::string den;
      while (isDigit(at())) den += s[i++];
      out += "\\frac{" + num + "}{" + den + "}";
    } else {
      out += num;
    }
    out += "\\,";
    atom = false;
    start = false;
  }

  /** A charge: the run of signs at the cursor. */
  void charge() {
    std::string signs;
    while (at() == '+' || at() == '-') signs += s[i++];
    out += "^{" + signs + "}";
  }

  void step() {
    const char c = at();
    if (isSpace(c)) {
      i++;
      spaced = true;
      start = true;
      atom = false;
      return;
    }
    const bool wasSpaced = spaced;
    spaced = false;
    if (c == '$') {
      const std::size_t j = s.find('$', i + 1);
      const std::size_t end = j == std::string::npos ? s.size() : j;
      out += s.substr(i + 1, end - i - 1);
      i = j == std::string::npos ? s.size() : j + 1;
      atom = true;
      start = false;
    } else if (c == '\\') {
      std::size_t j = i + 1;
      if (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) {
        while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) j++;
      } else if (j < s.size()) {
        j++;
      }
      out += s.substr(i, j - i);
      i = j;
      while (at() == '{') out += "{" + balanced('{', '}') + "}";
      atom = true;
      start = false;
    } else if (c == '{') {
      out += "{" + Reader(balanced('{', '}'), problem).run() + "}";
      atom = true;
      start = false;
    } else if ((c == '<' || c == '-') && arrow()) {
      // done
    } else if (c == '+') {
      const char n = at(1);
      if (atom && !wasSpaced && (n == '\0' || isSpace(n) || n == ')' || n == ']' || n == '}' || n == ',' || n == '+' || n == '-')) {
        charge();
      } else {
        i++;
        out += "{}+{}";
        atom = false;
        start = true;
      }
    } else if (c == '-') {
      const char n = at(1);
      if (atom && !wasSpaced && (isUpper(n) || isLower(n) || isDigit(n) || n == '(' || n == '[')) {
        i++;
        out += "{-}";
        atom = false;
      } else if (atom && !wasSpaced) {
        charge();
      } else {
        i++;
        out += "{}-{}";
        atom = false;
        start = true;
      }
    } else if (c == '=' && atom && !wasSpaced && at(1) != '\0' && !isSpace(at(1))) {
      i++;
      out += "{=}";
      atom = false;
    } else if (c == '#') {
      i++;
      out += "{\\equiv}";
      atom = false;
    } else if (c == '*' || (c == '.' && atom)) {
      i++;
      out += "{\\cdot}";
      atom = false;
      start = true;
    } else if (isDigit(c)) {
      if (start) {
        coefficient();
      } else if (atom) {
        std::string run;
        while (isDigit(at())) run += s[i++];
        out += "_{" + run + "}";
      } else {
        out += s[i++];
      }
    } else if (c == '^') {
      i++;
      const char n = at();
      if (start && wasSpaced && (n == '\0' || isSpace(n))) {
        out += "\\uparrow";
        atom = false;
      } else {
        const std::string sup = script(true);
        if (start || out.empty()) out += "{}";
        out += "^{" + sup + "}";
        atom = true;
        start = false;
      }
    } else if (c == '_') {
      i++;
      const std::string sub = script(false);
      if (start || out.empty()) out += "{}";
      out += "_{" + sub + "}";
      atom = true;
      start = false;
    } else if (c == '(') {
      // A state: (aq), (s), (l), (g).
      std::size_t j = i + 1;
      while (j < s.size() && (isLower(s[j]) || s[j] == '.' || s[j] == ',') && j - i < 6) j++;
      if (j < s.size() && s[j] == ')' && j > i + 1) {
        out += "\\mathrm{" + s.substr(i, j - i + 1) + "}";
        i = j + 1;
        atom = false;
      } else {
        i++;
        out += "(";
        atom = false;
      }
      start = false;
    } else if (c == ')' || c == ']') {
      i++;
      out += c;
      atom = true;
      start = false;
    } else if (isUpper(c)) {
      std::string word(1, s[i++]);
      while (isLower(at())) word += s[i++];
      out += "\\mathrm{" + word + "}";
      atom = true;
      start = false;
    } else if (isLower(c)) {
      const char n = at(1);
      if (c == 'v' && start && wasSpaced && (n == '\0' || isSpace(n))) {
        i++;
        out += "\\downarrow";
        atom = false;
      } else {
        std::string word;
        while (isLower(at())) word += s[i++];
        out += "\\mathrm{" + word + "}";
        atom = true;
        start = false;
      }
    } else {
      i++;
      out += c == ',' ? std::string("{,}") : std::string(1, c);
      atom = false;
      start = c == '/';
    }
  }
};

}  // namespace

std::string ce(const std::string& text, std::string& problem) {
  Reader r(text, problem);
  return r.run();
}

/** A `\pu`'s unit as mhchem sets it: upright, the exponents raised, spaces
 *  and dots a thin space, and a slash as it is written. */
std::string unitText(const std::string& u) {
  std::string out;
  std::size_t i = 0;
  const auto at = [&](std::size_t k) { return k < u.size() ? u[k] : '\0'; };
  while (i < u.size()) {
    const char c = u[i];
    if (isSpace(c) || c == '.') {
      while (isSpace(at(i)) || at(i) == '.') i++;
      out += "\\,";
    } else if (c == '/') {
      out += "{/}";
      i++;
    } else if (c == '\\') {
      std::size_t j = i + 1;
      while (std::isalpha(static_cast<unsigned char>(at(j)))) j++;
      out += u.substr(i, j - i);
      i = j;
    } else if (isUpper(c) || isLower(c)) {
      std::string word;
      while (isUpper(at(i)) || isLower(at(i))) word += u[i++];
      out += "\\mathrm{" + word + "}";
      if (at(i) == '^') i++;
      std::string exp;
      if (at(i) == '{') {
        std::size_t j = i + 1;
        while (j < u.size() && u[j] != '}') exp += u[j++];
        i = j + 1;
      } else {
        if (at(i) == '-' || at(i) == '+') exp += u[i++];
        while (isDigit(at(i))) exp += u[i++];
      }
      if (!exp.empty()) out += "^{" + exp + "}";
    } else {
      out += c;
      i++;
    }
  }
  return out;
}

std::string pu(const std::string& text, const siunitx::UserUnits&, std::string& problem) {
  const std::string t = trimmed(text);
  if (t.empty()) return "";
  std::size_t j = 0;
  while (j < t.size() && !isSpace(t[j])) j++;
  const std::string first = t.substr(0, j);
  const char c = first[0];
  const bool numeric = isDigit(c) || c == '.' || c == '+' || c == '-';
  if (!numeric) return unitText(t);
  const std::string number = siunitx::number(first, problem);
  const std::string rest = trimmed(t.substr(j));
  if (rest.empty()) return number;
  return number + "\\," + unitText(rest);
}

}  // namespace microtex::front::mhchem
