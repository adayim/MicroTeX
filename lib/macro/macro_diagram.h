#ifndef MICROTEX_MACRO_DIAGRAM_H
#define MICROTEX_MACRO_DIAGRAM_H

// The two ways a commutative diagram is written: amscd's CD, whose arrows
// are `@>a>b>`, `@VaVbV` and kin between and under the objects, and
// tikz-cd's tikzcd, whose arrows are `\arrow[r, "f"]` commands inside the
// cells they start from. Both fill a DiagramAtom (atom/diagram_atom.h).

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "atom/atom_basic.h"
#include "atom/diagram_atom.h"
#include "env/units.h"
#include "macro/macro_args.h"
#include "macro/macro_env.h"

namespace microtex {

namespace diagram {

inline std::string trim(const std::string& s) {
  std::size_t from = 0, to = s.size();
  while (from < to && std::isspace(static_cast<unsigned char>(s[from])) != 0) from++;
  while (to > from && std::isspace(static_cast<unsigned char>(s[to - 1])) != 0) to--;
  return s.substr(from, to - from);
}

/** `s` cut at each `sep` that is outside braces, brackets and quotes. */
inline std::vector<std::string> splitTop(const std::string& s, char sep) {
  std::vector<std::string> parts{""};
  int brace = 0, bracket = 0;
  bool quote = false;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      parts.back() += c;
      parts.back() += s[++i];
      continue;
    }
    if (c == '"' && brace == 0) quote = !quote;
    if (!quote) {
      if (c == '{') brace++;
      if (c == '}') brace--;
      if (c == '[' && brace == 0) bracket++;
      if (c == ']' && brace == 0) bracket--;
      if (c == sep && brace == 0 && bracket <= 0) {
        parts.emplace_back();
        continue;
      }
    }
    parts.back() += c;
  }
  return parts;
}

/** The text of the group that opens at `s[i]` (a `{` or a `[`) and the
 *  index after its close; "" and `i` when it does not close. */
inline std::string groupAt(const std::string& s, std::size_t& i) {
  const char open = s[i], close = open == '{' ? '}' : ']';
  int depth = 0, brace = 0;
  bool quote = false;
  for (std::size_t j = i; j < s.size(); j++) {
    const char c = s[j];
    if (c == '\\') {
      j++;
      continue;
    }
    if (open == '[' && c == '"' && brace == 0) quote = !quote;
    if (quote) continue;
    if (open == '[') {
      if (c == '{') brace++;
      if (c == '}') brace--;
      if (brace != 0) continue;
    }
    if (c == open) depth++;
    if (c == close && --depth == 0) {
      const std::string inner = s.substr(i + 1, j - i - 1);
      i = j + 1;
      return inner;
    }
  }
  // A group left open runs to the end of the text, as TeX reads one at the end of its input. `i` is past it: a caller
  // that reads group after group made no progress when it stayed, and `\node[` at the end of a picture hung.
  const std::string rest = s.substr(std::min(i + 1, s.size()));
  i = s.size();
  return rest;
}

/** A length as TikZ reads it: a dimension, or a bare number, which is centimetres; not valid when it is neither. */
inline Dimen lengthOf(const std::string& text) {
  const std::string v = trim(text);
  // A length starts with its number, or is a length the document names (`\tabcolsep`).
  // `rule_thickness` and `2*rule_thickness`: tikz-cd's own function, the line width of its arrows.
  const std::size_t rt = v.find("rule_thickness");
  if (rt != std::string::npos) {
    const std::string factor = trim(v.substr(0, rt));
    char* stop = nullptr;
    const float k = factor.empty() ? 1.f : std::strtof(factor.c_str(), &stop);
    const bool plain = factor.empty() || (stop != factor.c_str() && trim(stop) == "*");
    return plain && trim(v.substr(rt + 14)).empty() ? Dimen{k, UnitType::tt} : Dimen();
  }
  if (v.empty() || (std::isdigit(static_cast<unsigned char>(v[0])) == 0 && std::string(".-+\\").find(v[0]) == std::string::npos)) {
    return Dimen();
  }
  char* end = nullptr;
  const float f = std::strtof(v.c_str(), &end);
  if (end != v.c_str() && trim(end).empty()) return {f, UnitType::cm};
  return Units::getDimen(v);
}

/** `(x,y)`, with a `+` or `++` before it or not, as two lengths. */
inline bool pointOf(const std::string& text, Dimen& x, Dimen& y) {
  std::string v = trim(text);
  while (!v.empty() && v[0] == '+') v.erase(0, 1);
  v = trim(v);
  if (v.size() < 5 || v.front() != '(' || v.back() != ')') return false;
  const std::vector<std::string> xy = splitTop(v.substr(1, v.size() - 2), ',');
  if (xy.size() != 2) return false;
  x = lengthOf(xy[0]);
  y = lengthOf(xy[1]);
  return x.isValid() && y.isValid();
}

/** The names of `row sep=` and `column sep=` sizes; a document can change them (`\tikzcdset{row sep/normal=1cm}`)
 *  and make new ones. */
struct Sizes {
  std::map<std::string, std::string> row, column;
};

/** A number or a tikz size name to a size in em. */
inline Dimen sepOf(const std::string& value, bool row, const Sizes& sizes) {
  struct Name {
    const char* name;
    const char* rowSize;
    const char* columnSize;
  };
  static const Name names[] = {
    {"tiny", "0.45em", "0.6em"},   {"small", "0.9em", "1.2em"}, {"scriptsize", "1.35em", "1.8em"},
    {"normal", "1.8em", "2.4em"},  {"large", "2.7em", "3.6em"}, {"huge", "3.6em", "4.8em"},
  };
  const std::string v = trim(value);
  const auto& own = row ? sizes.row : sizes.column;
  if (const auto it = own.find(v); it != own.end()) return lengthOf(it->second);
  for (const Name& n : names) {
    if (v == n.name) return Units::getDimen(row ? n.rowSize : n.columnSize);
  }
  // A size starts with its number.
  const char first = v.empty() ? 'x' : v[0];
  if (std::isdigit(static_cast<unsigned char>(first)) == 0 && first != '.' && first != '-' && first != '+') return Dimen();
  return Units::getDimen(v);
}

inline bool isDirection(const std::string& s) {
  if (s.empty()) return false;
  for (const char c : s) {
    if (c != 'r' && c != 'l' && c != 'u' && c != 'd') return false;
  }
  return true;
}

/** What the diagram says before its arrows are read: `every arrow` and the rest, which `\tikzcdset` and the
 *  diagram's options set. */
struct Defaults {
  /** The options every arrow, label and cell starts with (`arrows=`, `labels=`, `cells=`). */
  std::string arrows, labels, cells;
  bool mathMode = true;
  Sizes sizes;
  /** Styles the document made (`\tikzcdset{my arrow/.style={red, dashed}}`). */
  std::map<std::string, std::string> styles;
  /** `background color`, which `description` labels fill, when it is said. */
  bool hasBackground = false;
  color background = 0;
};

/** One label of an arrow as written, and what its own options say. */
struct LabelSpec {
  std::string text;
  /** The options written after the label's text (`"f"'{red}`), read once all the arrow's options are known. */
  std::string ownOptions;
  DiagramLabel style;
  /** Each `swap` (and `'`) takes the label to the other side. */
  bool swap = false;
  bool hasPos = false;
  bool mathMode = true;
};

inline bool compassOf(const std::string& s, DiagramLabel::Anchor& a) {
  using L = DiagramLabel::Anchor;
  static const std::pair<const char*, L> names[] = {
    {"center", L::center}, {"north", L::north}, {"south", L::south}, {"east", L::east}, {"west", L::west},
    {"north east", L::northEast}, {"north west", L::northWest}, {"south east", L::southEast}, {"south west", L::southWest},
    {"above", L::south}, {"below", L::north}, {"left", L::east}, {"right", L::west},
    {"above left", L::southEast}, {"above right", L::southWest}, {"below left", L::northEast}, {"below right", L::northWest}};
  for (const auto& n : names) {
    if (s == n.first) {
      a = n.second;
      return true;
    }
  }
  return false;
}

/** A key's value, with the braces it may be written in taken off. */
inline std::string unbraced(std::string v) {
  v = trim(v);
  if (!v.empty() && v[0] == '{') {
    std::size_t at = 0;
    return trim(groupAt(v, at));
  }
  return v;
}

/** The part of a key before its `=`, and what follows it (unbraced); "" for both when the key has none. */
inline void keyValue(const std::string& key, std::string& name, std::string& value, bool& hasValue) {
  const std::size_t eq = key.find('=');
  name = trim(key.substr(0, eq));
  hasValue = eq != std::string::npos;
  value = hasValue ? unbraced(key.substr(eq + 1)) : "";
}

/** A colour named in a key: a name xcolor has, or a mix of names. */
inline bool colourOf(const std::string& text, color& out) {
  const std::string v = trim(text);
  if (v.empty()) return false;
  return (ColorAtom::hasName(v) || v.find('!') != std::string::npos) && ColorAtom::parseColor(v, out);
}

/** LaTeX's font sizes, as multiples of the text's own (`font=\Large`). */
inline bool fontSizeOf(const std::string& text, float& scale) {
  static const std::pair<const char*, float> sizes[] = {
    {"\\tiny", 0.5f},   {"\\scriptsize", 0.7f}, {"\\footnotesize", 0.8f}, {"\\small", 0.9f}, {"\\normalsize", 1.f},
    {"\\large", 1.2f},  {"\\Large", 1.44f},     {"\\LARGE", 1.728f},      {"\\huge", 2.074f}, {"\\Huge", 2.488f}};
  const std::string v = trim(text);
  for (const auto& s : sizes) {
    if (v == s.first) {
      scale = s.second;
      return true;
    }
  }
  return false;
}

/** `near start`, `swap`, `pos=0.3`, `above`, `inner sep=1ex` and kin: what a label's options (and those an
 *  arrow says of all its labels) can say. False for a key that is not one of them. */
inline bool labelKey(const std::string& raw, LabelSpec& l, const Defaults& def) {
  const std::string key = trim(raw);
  std::string name, value;
  bool hasValue = false;
  keyValue(key, name, value, hasValue);
  DiagramLabel& s = l.style;
  using Anchor = DiagramLabel::Anchor;
  const auto place = [&](float pos) {
    s.pos = pos;
    l.hasPos = true;
  };
  Anchor a;
  color ink = 0;
  float scale = 1.f;
  if (key == "swap" || key == "'") {
    l.swap = !l.swap;
  } else if (name == "auto") {
    l.swap = value == "right";
    s.anchor = Anchor::none;
  } else if (compassOf(name, a) && (name == "above" || name == "below" || name == "left" || name == "right" ||
                                    name == "above left" || name == "above right" || name == "below left" ||
                                    name == "below right")) {
    s.anchor = a;
    // `above=2pt`: that much farther.
    if (hasValue) {
      const Dimen by = lengthOf(value);
      if (by.isValid()) {
        const float sign = name.compare(0, 5, "below") == 0 || name == "left" ? -1.f : 1.f;
        const bool vertical = name.compare(0, 5, "above") == 0 || name.compare(0, 5, "below") == 0;
        Dimen d = by;
        d.val *= sign;
        (vertical ? s.yshift : s.xshift) = d;
      }
    }
  } else if (name == "anchor" && compassOf(value, a)) {
    s.anchor = a;
  } else if (key == "description") {
    s.side = DiagramLabel::Side::center;
    s.interrupts = true;
    if (def.hasBackground) {
      s.hasFill = true;
      s.fill = def.background;
    }
  } else if (key == "marking") {
    // Over the arrow, turned to it and set at the size of the text around.
    s.side = DiagramLabel::Side::center;
    s.fullSize = true;
    s.sloped = true;
  } else if (key == "very near start") {
    place(0.1f);
  } else if (key == "near start") {
    place(0.25f);
  } else if (key == "near end") {
    place(0.75f);
  } else if (key == "very near end") {
    place(0.9f);
  } else if (key == "at start") {
    place(0.f);
  } else if (key == "at end") {
    place(1.f);
  } else if (name == "pos" && hasValue) {
    place(static_cast<float>(std::atof(value.c_str())));
  } else if (key == "sloped") {
    s.sloped = true;
  } else if (name == "rotate" && hasValue) {
    s.rotate = static_cast<float>(std::atof(value.c_str()));
  } else if (name == "inner sep" && hasValue) {
    s.innerSep = lengthOf(value);
    return s.innerSep.isValid();
  } else if (name == "xshift" && hasValue) {
    s.xshift = lengthOf(value);
    return s.xshift.isValid();
  } else if (name == "yshift" && hasValue) {
    s.yshift = lengthOf(value);
    return s.yshift.isValid();
  } else if (name == "shift" && hasValue) {
    return pointOf(value, s.xshift, s.yshift);
  } else if (name == "font") {
    // `font=` (nothing) puts the label back at the text's size; a LaTeX size sets it.
    s.fullSize = true;
    if (fontSizeOf(value, scale)) s.fontScale = scale;
  } else if ((name == "text" || name == "color") && colourOf(value, ink)) {
    s.hasInk = true;
    s.ink = ink;
  } else if (key == "draw") {
    s.hasDraw = true;
  } else if (name == "draw" && hasValue && colourOf(value, ink)) {
    s.hasDraw = true;
    s.hasDrawInk = true;
    s.drawInk = ink;
  } else if (name == "fill" && hasValue && colourOf(value, ink)) {
    s.hasFill = true;
    s.fill = ink;
  } else if ((name == "name" || name == "alias") && hasValue) {
    s.name = value;
  } else if (key == "coordinate") {
    s.point = true;
  } else if (colourOf(key, ink)) {
    s.hasInk = true;
    s.ink = ink;
  } else {
    return false;
  }
  return true;
}

class Warned {
public:
  explicit Warned(CommandArgs& args) : _args(args) {}
  void once(const std::string& what) {
    if (_seen.insert(what).second) _args.warn(what);
  }

private:
  CommandArgs& _args;
  std::set<std::string> _seen;
};

/** A cell, or a node, an arrow starts or ends at, other than the one it is written in: `2-1` (row and column,
 *  from 1), a direction such as `rr`, or the name of a cell (`|[alias=Z]|`) or of a label. */
struct Target {
  bool set = false;
  bool absolute = false;
  int row = 0, col = 0;
  std::string name;
};

inline bool targetOf(const std::string& text, Target& t) {
  const std::string v = trim(text);
  const std::size_t dash = v.find('-');
  if (dash != std::string::npos && dash > 0 && dash + 1 < v.size()) {
    const std::string r = v.substr(0, dash), c = v.substr(dash + 1);
    const auto digits = [](const std::string& s) {
      for (const char ch : s) {
        if (std::isdigit(static_cast<unsigned char>(ch)) == 0) return false;
      }
      return !s.empty();
    };
    if (digits(r) && digits(c)) {
      t = {true, true, std::atoi(r.c_str()) - 1, std::atoi(c.c_str()) - 1, ""};
      return true;
    }
  }
  if (isDirection(v)) {
    t = {true, false, 0, 0, ""};
    for (const char ch : v) {
      t.row += ch == 'd' ? 1 : ch == 'u' ? -1 : 0;
      t.col += ch == 'r' ? 1 : ch == 'l' ? -1 : 0;
    }
    return true;
  }
  // Anything else is a name; a node's name has no spaces, commas or brackets.
  if (!v.empty() && v.find_first_of(" ,[]{}()") == std::string::npos) {
    t = {true, false, 0, 0, v};
    return true;
  }
  return false;
}

/** Where an arrow written in a cell goes: `dr`, `dc` rows and columns away,
 *  unless `to` says; and where it starts, if not in that cell. */
struct Placement {
  int dr = 0, dc = 0;
  Target from, to;
};

/** The keys that stand for an arrow's two tips: `hook`, `two heads`, `Rightarrow`, ... Each says what it sets at
 *  the start and at the end (tikz-cd's own definitions, tikzlibrarycd.code.tex), and leaves the rest. */
inline bool tipKey(const std::string& key, DiagramArrow& arrow) {
  using A = DiagramArrow;
  struct Tips {
    const char* name;
    bool setTail, setHead;
    A::Tail tail;
    A::Head head;
    bool doubled;
  };
  static const Tips tips[] = {
    {"to head", false, true, A::Tail::none, A::Head::one, false},
    {"rightarrow", true, true, A::Tail::none, A::Head::one, false},
    {"leftarrow", true, true, A::Tail::head, A::Head::none, false},
    {"leftrightarrow", true, true, A::Tail::head, A::Head::one, false},
    {"Rightarrow", true, true, A::Tail::none, A::Head::implies, true},
    {"Leftarrow", true, true, A::Tail::implies, A::Head::none, true},
    {"Leftrightarrow", true, true, A::Tail::implies, A::Head::implies, true},
    {"maps to", true, false, A::Tail::bar, A::Head::one, false},
    {"mapsto", true, true, A::Tail::bar, A::Head::one, false},
    {"mapsfrom", true, true, A::Tail::head, A::Head::bar, false},
    {"Mapsto", true, true, A::Tail::bar, A::Head::implies, true},
    {"Mapsfrom", true, true, A::Tail::implies, A::Head::bar, true},
    {"hook", true, false, A::Tail::hook, A::Head::one, false},
    {"hook'", true, false, A::Tail::hookBack, A::Head::one, false},
    {"hookrightarrow", true, true, A::Tail::hook, A::Head::one, false},
    {"hookleftarrow", true, true, A::Tail::head, A::Head::hookBack, false},
    {"tail", true, false, A::Tail::tail, A::Head::one, false},
    {"rightarrowtail", true, true, A::Tail::tail, A::Head::one, false},
    {"leftarrowtail", true, true, A::Tail::head, A::Head::tail, false},
    {"two heads", false, true, A::Tail::none, A::Head::two, false},
    {"twoheadrightarrow", true, true, A::Tail::none, A::Head::two, false},
    {"twoheadleftarrow", true, true, A::Tail::two, A::Head::none, false},
    {"no head", false, true, A::Tail::none, A::Head::none, false},
    {"no tail", true, false, A::Tail::none, A::Head::none, false},
    {"dash", true, true, A::Tail::none, A::Head::none, false},
    {"equal", true, true, A::Tail::none, A::Head::none, true},
    {"equals", true, true, A::Tail::none, A::Head::none, true},
    {"harpoon", false, true, A::Tail::none, A::Head::harpoonLeft, false},
    {"harpoon'", false, true, A::Tail::none, A::Head::harpoonRight, false},
    {"rightharpoonup", true, true, A::Tail::none, A::Head::harpoonLeft, false},
    {"rightharpoondown", true, true, A::Tail::none, A::Head::harpoonRight, false},
    {"leftharpoonup", true, true, A::Tail::harpoonRight, A::Head::none, false},
    {"leftharpoondown", true, true, A::Tail::harpoonLeft, A::Head::none, false},
  };
  for (const Tips& t : tips) {
    if (key != t.name) continue;
    if (t.setTail) arrow.tail = t.tail;
    if (t.setHead) arrow.head = t.head;
    if (t.doubled) arrow.doubled = true;
    return true;
  }
  return false;
}

/** arrows.meta's `Latex`, `Stealth` with their options (`Latex[length=3mm, open, round]`); false when `spec` is
 *  not one of those or has an option that is not read. */
inline bool metaTipOf(const std::string& spec, MetaTip& tip) {
  std::string name = trim(spec), options;
  const std::size_t open = name.find('[');
  if (open != std::string::npos) {
    const std::size_t close = name.rfind(']');
    if (close == std::string::npos || close < open) return false;
    options = name.substr(open + 1, close - open - 1);
    name = trim(name.substr(0, open));
  }
  if (name == "Latex" || name == "LaTeX") {
    tip.kind = MetaTip::Kind::latex;
  } else if (name == "Stealth") {
    tip.kind = MetaTip::Kind::stealth;
  } else {
    return false;
  }
  std::size_t at = 0;
  while (at <= options.size() && !trim(options).empty()) {
    std::size_t comma = options.find(',', at);
    if (comma == std::string::npos) comma = options.size();
    const std::string item = trim(options.substr(at, comma - at));
    at = comma + 1;
    if (item.empty()) {
      if (comma >= options.size()) break;
      continue;
    }
    const std::size_t eq = item.find('=');
    const std::string key = trim(eq == std::string::npos ? item : item.substr(0, eq));
    const std::string value = eq == std::string::npos ? "" : trim(item.substr(eq + 1));
    if (key == "open") tip.open = value != "false";
    else if (key == "round") tip.round = value != "false";
    else if (key == "length") tip.length = Units::getDimen(value);
    else if (key == "width") tip.width = Units::getDimen(value);
    else if (key == "inset") tip.inset = Units::getDimen(value);
    else if (key == "line width") tip.lineWidth = Units::getDimen(value);
    else if (key == "scale") tip.scale = static_cast<float>(std::atof(value.c_str()));
    else return false;
    if (comma >= options.size()) break;
  }
  return tip.scale > 0.f;
}

/** The first `-` outside brackets and braces: where the tip at the start ends and the one at the end begins. */
inline std::size_t tipDash(const std::string& key) {
  int depth = 0;
  for (std::size_t i = 0; i < key.size(); i++) {
    if (key[i] == '[' || key[i] == '{') depth++;
    else if (key[i] == ']' || key[i] == '}') depth--;
    else if (key[i] == '-' && depth == 0) return i;
  }
  return std::string::npos;
}

/** TikZ's `->`, `<->`, `-{cm to}`, `{cm bar}-{cm to}`, `-Latex`, `Stealth-Stealth`: the tip at each end by name;
 *  false when not one of those or when a tip is not one of the Computer Modern ones the arrows have or arrows.meta's
 *  Latex and Stealth. */
inline bool tikzTips(const std::string& key, DiagramArrow& arrow, bool& known) {
  using A = DiagramArrow;
  known = true;
  const std::size_t dash = tipDash(key);
  if (dash == std::string::npos) return false;
  const std::string start = unbraced(key.substr(0, dash)), end = unbraced(key.substr(dash + 1));
  MetaTip metaStart, metaEnd;
  const bool startMeta = metaTipOf(start, metaStart), endMeta = metaTipOf(end, metaEnd);
  if (startMeta || endMeta) {
    // a meta tip at one end, and the other either meta too, none, or a plain `<`/`>`
    A::Head h = A::Head::none;
    A::Tail t = A::Tail::none;
    if (!startMeta && !start.empty()) {
      if (start != "<" && start != "to") { known = false; return false; }
      t = A::Tail::head;
    }
    if (!endMeta && !end.empty()) {
      if (end != ">" && end != "to") { known = false; return false; }
      h = A::Head::one;
    }
    arrow.head = h;
    arrow.tail = t;
    arrow.headTip = endMeta ? metaEnd : MetaTip();
    arrow.tailTip = startMeta ? metaStart : MetaTip();
    return true;
  }
  // A tip as it stands at the end, and as it stands at the start (drawn turned round).
  const auto headOf = [&](const std::string& n, A::Head& h) {
    if (n.empty()) h = A::Head::none;
    else if (n == ">" || n == "to" || n == "cm to" || n == "cm bold to") h = A::Head::one;
    else if (n == "cm double to") h = A::Head::two;
    else if (n == "cm implies") h = A::Head::implies;
    else if (n == "cm bar") h = A::Head::bar;
    else if (n == "cm left to") h = A::Head::harpoonLeft;
    else if (n == "cm right to") h = A::Head::harpoonRight;
    else if (n == "cm left hook") h = A::Head::hookBack;
    else if (n == "cm right hook") h = A::Head::hook;
    else if (n == "cm to reversed") h = A::Head::tail;
    else return false;
    return true;
  };
  const auto tailOf = [&](const std::string& n, A::Tail& t) {
    if (n.empty()) t = A::Tail::none;
    else if (n == "<" || n == "to" || n == "cm to" || n == "cm bold to") t = A::Tail::head;
    else if (n == "cm double to") t = A::Tail::two;
    else if (n == "cm implies") t = A::Tail::implies;
    else if (n == "cm bar") t = A::Tail::bar;
    else if (n == "cm left to") t = A::Tail::harpoonLeft;
    else if (n == "cm right to") t = A::Tail::harpoonRight;
    else if (n == "cm left hook") t = A::Tail::hookBack;
    else if (n == "cm right hook") t = A::Tail::hook;
    else if (n == "cm to reversed") t = A::Tail::tail;
    else return false;
    return true;
  };
  // `<` and `>` alone are the plain tips: `->`, `<-`, `<->`.
  std::string s = start == "<" ? "<" : start, e = end == ">" ? ">" : end;
  A::Head h;
  A::Tail t;
  if (!tailOf(s, t) || !headOf(e, h)) {
    known = false;
    return false;
  }
  arrow.tail = t;
  arrow.head = h;
  arrow.headTip = MetaTip();
  arrow.tailTip = MetaTip();
  return true;
}

/** TikZ's line widths: `thin` and the rest, in points. */
inline bool lineWidthName(const std::string& key, Dimen& out) {
  static const std::pair<const char*, float> names[] = {
    {"ultra thin", 0.1f}, {"very thin", 0.2f}, {"thin", 0.4f}, {"semithick", 0.6f},
    {"thick", 0.8f}, {"very thick", 1.2f}, {"ultra thick", 1.6f}};
  for (const auto& n : names) {
    if (key == n.first) {
      out = {n.second, UnitType::pt};
      return true;
    }
  }
  return false;
}

/** An arrow's options, as tikz-cd reads them: where it goes, how it is
 *  drawn, and its labels. The diagram's `arrows=` come first. */
inline void arrowOptions(CommandArgs& args, Warned& warned, const std::string& options, DiagramArrow& arrow,
                         Placement& place, const Defaults& def) {
  using A = DiagramArrow;
  int expansions = 0;
  std::vector<LabelSpec> labels;
  // What the arrow says of all its labels, applied before each label's own.
  std::vector<std::string> pathLabelKeys;
  bool arrowMath = def.mathMode;
  std::function<void(const std::string&)> keys = [&](const std::string& list) {
    for (const std::string& raw : splitTop(list, ',')) {
      const std::string key = trim(raw);
      if (key.empty()) continue;
      if (key[0] == '"') {
        // "text" then, perhaps, ' and the label's own options.
        std::size_t end = 1;
        while (end < key.size() && key[end] != '"') end += key[end] == '\\' ? 2 : 1;
        LabelSpec l;
        l.text = key.substr(1, end - 1);
        l.mathMode = arrowMath;
        std::string rest = trim(end < key.size() ? key.substr(end + 1) : "");
        if (!rest.empty() && rest[0] == '\'') {
          rest = trim(rest.substr(1));
          l.style.anchor = DiagramLabel::Anchor::none;
          l.swap = true;
        }
        std::string own;
        if (!rest.empty() && rest[0] == '{') {
          std::size_t at = 0;
          own = groupAt(rest, at);
        } else {
          own = rest;
        }
        // The options a label has are the diagram's, the arrow's and its own, in that order. Its text and its
        // own options are kept to be read when the arrow's are all known.
        l.style.name = "";
        labels.push_back(l);
        labels.back().style.body = nullptr;
        labels.back().ownOptions = own;
        continue;
      }
      if (isDirection(key)) {
        for (const char c : key) {
          place.dr += c == 'd' ? 1 : c == 'u' ? -1 : 0;
          place.dc += c == 'r' ? 1 : c == 'l' ? -1 : 0;
        }
        continue;
      }
      std::string name, value;
      bool hasValue = false;
      keyValue(key, name, value, hasValue);
      if (name == "from" || name == "to") {
        Target& t = name == "from" ? place.from : place.to;
        if (!targetOf(value, t)) warned.once("\\arrow: " + key + " is not a cell or a node name and is ignored");
        continue;
      }
      if (def.styles.count(key) != 0 && expansions++ < 64) {
        keys(def.styles.at(key));
        continue;
      }
      color ink = 0;
      if (name == "color" || name == "draw") {
        if (value == "none") {
          arrow.phantom = true;
        } else if (hasValue && colourOf(value, ink)) {
          arrow.hasColor = true;
          arrow.ink = ink;
        } else if (hasValue) {
          warned.once("\\arrow: " + key + " is not a colour that is defined; it is ignored");
        }
        continue;
      }
      // A key alone is a colour when it is a defined name or an xcolor mix; a word of hex digits or a number is a
      // key, not a colour.
      if (colourOf(key, ink)) {
        arrow.hasColor = true;
        arrow.ink = ink;
        continue;
      }
      const auto number = [&](const std::string& n, float fallback) {
        if (key.compare(0, n.size(), n) != 0) return -1.f;
        const std::string v = trim(key.substr(n.size()));
        if (v.empty()) return fallback;
        return v[0] == '=' ? static_cast<float>(std::atof(v.c_str() + 1)) : -1.f;
      };
      if (tipKey(key, arrow)) continue;
      if (key == "dashed" || key == "dashrightarrow" || key == "dashleftarrow") {
        arrow.dash = A::Dash::dashed;
        if (key == "dashrightarrow") tipKey("rightarrow", arrow);
        if (key == "dashleftarrow") tipKey("leftarrow", arrow);
        continue;
      }
      if (key == "dotted") {
        arrow.dash = A::Dash::custom;
        arrow.pattern = {Dimen{1.f, UnitType::none}, Dimen{2.f, UnitType::pt}};
        continue;
      }
      {
        // TikZ's dash styles, in points: on and off.
        struct Dash {
          const char* name;
          float on, off;
          bool dot;
        };
        static const Dash dashes[] = {{"densely dotted", 0.f, 1.f, true}, {"loosely dotted", 0.f, 4.f, true},
                                      {"densely dashed", 3.f, 2.f, false}, {"loosely dashed", 3.f, 6.f, false}};
        bool found = false;
        for (const Dash& dd : dashes) {
          if (key != dd.name) continue;
          arrow.dash = A::Dash::custom;
          arrow.pattern = {dd.dot ? Dimen{1.f, UnitType::none} : Dimen{dd.on, UnitType::pt}, Dimen{dd.off, UnitType::pt}};
          found = true;
        }
        if (found) continue;
      }
      if (name == "dash pattern" && hasValue) {
        // `on 2pt off 3pt`, and more pairs.
        std::vector<Dimen> pattern;
        std::string word;
        bool ok = true;
        std::string text = value + " ";
        for (const char c : text) {
          if (c != ' ') {
            word += c;
            continue;
          }
          if (word.empty() || word == "on" || word == "off") {
            word.clear();
            continue;
          }
          const Dimen v = lengthOf(word);
          if (!v.isValid()) ok = false;
          pattern.push_back(v);
          word.clear();
        }
        if (ok && pattern.size() >= 2) {
          arrow.dash = A::Dash::custom;
          arrow.pattern = pattern;
        } else {
          warned.once("\\arrow: " + key + " is not a dash pattern and is ignored");
        }
        continue;
      }
      if (Dimen w; lineWidthName(key, w)) {
        arrow.lineWidth = w;
        continue;
      }
      if (name == "line width" && hasValue) {
        const Dimen w = lengthOf(value);
        char* stop = nullptr;
        const std::string bare = trim(value);
        std::strtof(bare.c_str(), &stop);
        if (w.isValid() && !(stop != bare.c_str() && *stop == '\0')) {
          arrow.lineWidth = w;
        } else if (w.isValid()) {
          // A bare number is not a length here: TikZ reads `line width=1` as 1pt.
          arrow.lineWidth = {w.val, UnitType::pt};
        } else {
          warned.once("\\arrow: " + key + " is not supported and is ignored");
        }
        continue;
      }
      // The strokes are drawn with round caps (and joins), which is what `line cap=round` asks for.
      if ((name == "line cap" || name == "line join") && hasValue && value == "round") continue;
      if (key == "double") {
        arrow.doubled = true;
        arrow.doubleInner = {0.6f, UnitType::pt};
        continue;
      }
      if (name == "double distance" && hasValue) {
        arrow.doubleInner = lengthOf(value);
        arrow.doubled = arrow.doubleInner.isValid();
        continue;
      }
      if (key.compare(0, 7, "shorten") == 0 && key.size() > 7) {
        // `shorten <=1ex`, `shorten >=1ex`, and `shorten=1ex` for both.
        const std::string rest = trim(key.substr(7));
        const bool start = rest.compare(0, 2, "<=") == 0, end = rest.compare(0, 2, ">=") == 0;
        const Dimen by = lengthOf(rest.substr(start || end ? 2 : (rest.compare(0, 1, "=") == 0 ? 1 : 0)));
        if (!by.isValid()) {
          warned.once("\\arrow: " + key + " is not a size and is ignored");
        } else {
          if (!end) arrow.shortenStart = by;
          if (!start) arrow.shortenEnd = by;
        }
        continue;
      }
      if (key == "phantom") {
        arrow.phantom = true;
        continue;
      }
      if (key == "crossing over") {
        arrow.crossing = true;
        continue;
      }
      if (key == "squiggly" || key == "rightsquigarrow" || key == "leftsquigarrow" || key == "leftrightsquigarrow") {
        arrow.squiggly = true;
        if (key == "rightsquigarrow") {
          arrow.tail = A::Tail::none;
          arrow.head = A::Head::one;
        } else if (key != "squiggly") {
          arrow.tail = A::Tail::head;
          arrow.head = key == "leftsquigarrow" ? A::Head::none : A::Head::one;
        }
        continue;
      }
      if (name == "math mode") {
        arrowMath = !hasValue || value != "false";
        continue;
      }
      if (key.compare(0, 9, "bend left") == 0 && number("bend left", 30.f) >= 0.f) {
        arrow.bend = number("bend left", 30.f);
        continue;
      }
      if (key.compare(0, 10, "bend right") == 0 && number("bend right", 30.f) >= 0.f) {
        arrow.bend = -number("bend right", 30.f);
        continue;
      }
      // Shifts: of the arrow and its labels, by a dimension or, with no unit, by that many of the default.
      if (name == "shift left" || name == "shift right") {
        const float sign = name == "shift left" ? 1.f : -1.f;
        const Dimen v = hasValue ? Units::getDimen(value) : Dimen();
        if (v.isValid()) {
          arrow.shiftLength = {sign * v.val, v.unit};
        } else {
          arrow.shift = sign * (hasValue ? static_cast<float>(std::atof(value.c_str())) : 1.f);
        }
        continue;
      }
      if ((name == "xshift" || name == "yshift") && hasValue) {
        (name == "xshift" ? arrow.xshift : arrow.yshift) = lengthOf(value);
        if (!(name == "xshift" ? arrow.xshift : arrow.yshift).isValid()) warned.once("\\arrow: " + key + " is not a length and is ignored");
        continue;
      }
      if (name == "shift" && hasValue) {
        if (!pointOf(value, arrow.xshift, arrow.yshift)) warned.once("\\arrow: " + key + " is not a point and is ignored");
        continue;
      }
      if (name == "start anchor" || name == "end anchor") {
        // `[xshift=-1ex]north east`: the transformations the point is given, then its name.
        const bool start = name == "start anchor";
        std::string v = trim(value);
        Dimen dx, dy;
        if (!v.empty() && v[0] == '[') {
          std::size_t at = 0;
          const std::string transforms = groupAt(v, at);
          v = trim(v.substr(at));
          for (const std::string& t : splitTop(transforms, ',')) {
            std::string n, tv;
            bool hv = false;
            keyValue(trim(t), n, tv, hv);
            if (n == "xshift" && hv) dx = lengthOf(tv);
            else if (n == "yshift" && hv) dy = lengthOf(tv);
            else if (n == "shift" && hv) pointOf(tv, dx, dy);
            else warned.once("\\arrow: " + name + " transformation " + trim(t) + " is not supported and is ignored");
          }
        }
        static const char* const known[] = {"center", "north", "south", "east", "west", "north east", "north west",
                                            "south east", "south west", "real center", "real east", "real west",
                                            "base", "base east", "base west", "mid", "mid east", "mid west"};
        bool ok = v.empty() || std::isdigit(static_cast<unsigned char>(v[0])) != 0 || v[0] == '-';
        for (const char* k : known) ok = ok || v == k;
        if (!ok) {
          warned.once("\\arrow: " + name + "=" + v + " is not an anchor and is ignored");
          continue;
        }
        (start ? arrow.startAnchor : arrow.endAnchor) = v;
        (start ? arrow.startDx : arrow.endDx) = dx;
        (start ? arrow.startDy : arrow.endDy) = dy;
        continue;
      }
      if (name == "out" && hasValue) {
        arrow.hasOut = true;
        arrow.outAngle = static_cast<float>(std::atof(value.c_str()));
        continue;
      }
      if (name == "in" && hasValue) {
        arrow.hasIn = true;
        arrow.inAngle = static_cast<float>(std::atof(value.c_str()));
        continue;
      }
      if (name == "looseness" && hasValue) {
        arrow.looseness = static_cast<float>(std::atof(value.c_str()));
        // Alone, it bends the arrow at TikZ's default angles.
        if (!arrow.hasOut && !arrow.hasIn && arrow.bend == 0.f && !arrow.hasControls) {
          arrow.hasOut = arrow.hasIn = true;
          arrow.outAngle = 45.f;
          arrow.inAngle = 135.f;
        }
        continue;
      }
      if ((name == "max distance" || name == "distance") && hasValue) {
        arrow.maxDistance = lengthOf(value);
        if (name == "distance") arrow.minDistance = arrow.maxDistance;
        continue;
      }
      if (name == "min distance" && hasValue) {
        arrow.minDistance = lengthOf(value);
        continue;
      }
      if (name == "controls" && hasValue) {
        const std::size_t and_ = value.find(" and ");
        bool ok = and_ != std::string::npos;
        if (ok) {
          ok = pointOf(value.substr(0, and_), arrow.control1x, arrow.control1y) &&
               pointOf(value.substr(and_ + 5), arrow.control2x, arrow.control2y);
        }
        if (ok) arrow.hasControls = true;
        else warned.once("\\arrow: " + key + " is not two relative points and is ignored");
        continue;
      }
      if (name == "loop" || key.compare(0, 5, "loop ") == 0) {
        // `loop`: a curve from the node to itself, out and back in at angles that depend on the side.
        const std::string side = trim(key.substr(4));
        struct Loop {
          const char* name;
          float out, in;
        };
        static const Loop loops[] = {{"right", 15.f, -15.f}, {"above", 105.f, 75.f}, {"left", 195.f, 165.f}, {"below", 285.f, 255.f}, {"", 45.f, 135.f}};
        bool found = false;
        for (const Loop& lp : loops) {
          if (side != lp.name) continue;
          arrow.hasOut = arrow.hasIn = true;
          arrow.outAngle = lp.out;
          arrow.inAngle = lp.in;
          arrow.looseness = 8.f;
          arrow.minDistance = {5.f, UnitType::mm};
          arrow.shortenEnd = {1.f, UnitType::pt};
          if (!side.empty()) pathLabelKeys.push_back(side);
          found = true;
        }
        if (!found) warned.once("\\arrow: " + key + " is not a kind of loop and is ignored");
        continue;
      }
      if (name == "rounded corners") {
        arrow.rounded = true;
        if (hasValue) arrow.cornerRadius = lengthOf(value);
        continue;
      }
      if (name == "to path" && hasValue) {
        const std::string path = trim(value);
        const auto target = [&](std::size_t from) { return trim(path.substr(from)) == "(\\tikztotarget)"; };
        if (path.compare(0, 2, "|-") == 0 && target(2)) arrow.route = A::Route::verticalFirst;
        else if (path.compare(0, 2, "-|") == 0 && target(2)) arrow.route = A::Route::horizontalFirst;
        else if (path.compare(0, 2, "--") == 0 && target(2)) arrow.route = A::Route::direct;
        else warned.once("\\arrow: " + key + " is not supported and is ignored");
        continue;
      }
      // The tips by name: `->`, `<->`, `-{cm bar}`; pgf's `arrows=-Stealth` says the same.
      {
        bool known = true;
        if (name == "arrows" && hasValue) {
          const std::string spec = unbraced(trim(value));
          if (spec.find('-') != std::string::npos && tikzTips(spec, arrow, known)) continue;
          if (!known) {
            warned.once("\\arrow: " + key + " is not supported and is ignored");
            continue;
          }
        }
        if (key.find('-') != std::string::npos && tikzTips(key, arrow, known)) continue;
        if (!known && key.find('-') != std::string::npos && (key[0] == '-' || key[0] == '<' || key[0] == '{') &&
            key.compare(0, 8, "shorten ") != 0) {
          warned.once("\\arrow: " + key + " is not supported and is ignored");
          continue;
        }
      }
      // What an arrow says of its labels.
      LabelSpec probe;
      if (name != "rotate" && name != "draw" && name != "fill" && name != "name" && name != "alias" && key != "coordinate" &&
          labelKey(key, probe, def)) {
        pathLabelKeys.push_back(key);
        continue;
      }
      warned.once("\\arrow: " + key + " is not supported and is ignored");
    }
  };
  keys(def.arrows + "," + options);

  for (LabelSpec& l : labels) {
    const std::string own = l.ownOptions;
    const auto apply = [&](const std::string& list) {
      for (const std::string& k : splitTop(list, ',')) {
        const std::string opt = trim(k);
        if (opt.empty()) continue;
        if (!labelKey(opt, l, def)) warned.once("label option " + opt + " is ignored");
      }
    };
    // A swap the label's text asked for (the `'`) counts with the others.
    const bool quoted = l.swap;
    l.swap = false;
    apply(def.labels);
    for (const std::string& k : pathLabelKeys) labelKey(k, l, def);
    l.swap = l.swap != quoted;
    apply(own);
    DiagramLabel out = l.style;
    // Said of the arrow, an ink is the labels': a colour the arrow has is theirs, unless they have their own.
    out.pos = l.hasPos ? out.pos : 0.5f;
    if (arrow.phantom) {
      // A phantom arrow's labels are on its path at the size of the text.
      out.side = DiagramLabel::Side::center;
      out.fullSize = true;
    } else if (out.side != DiagramLabel::Side::center) {
      out.side = l.swap ? DiagramLabel::Side::right : DiagramLabel::Side::left;
    }
    out.body = args.formulaChecked(l.text, l.mathMode);
    // The script size is the math font's: text is set as it is around.
    if (!l.mathMode) out.fullSize = true;
    if (out.body != nullptr) {
      const bool inked = out.hasInk;
      if (inked) {
        out.body = sptrOf<ColorAtom>(out.body, TRANSPARENT, out.ink);
      } else if (arrow.hasColor) {
        out.body = sptrOf<ColorAtom>(out.body, TRANSPARENT, arrow.ink);
      }
    }
    arrow.labels.push_back(out);
  }
}

/** What a cell's node says of itself: `|[draw, blue, name=Z]|`, and `cells=`. */
inline void cellKey(const std::string& raw, DiagramCell& cell, Warned& warned, bool direct = false) {
  const std::string key = trim(raw);
  if (key.empty()) return;
  std::string name, value;
  bool hasValue = false;
  keyValue(key, name, value, hasValue);
  color ink = 0;
  // What `cells=` and `every cell` say of a cell's shape (its outline, its fill, its turn) is not read by tikz-cd
  // there -- only through `nodes={...}` -- so it is not here either.
  if (direct && (key == "draw" || name == "draw" || name == "fill" || name == "rotate")) return;
  if ((name == "name" || name == "alias") && hasValue) {
    cell.name = value;
  } else if (key == "draw") {
    cell.hasDraw = true;
  } else if (name == "draw" && hasValue && colourOf(value, ink)) {
    cell.hasDraw = cell.hasDrawInk = true;
    cell.drawInk = ink;
  } else if (name == "fill" && hasValue && colourOf(value, ink)) {
    cell.hasFill = true;
    cell.fill = ink;
  } else if ((name == "text" || name == "color") && colourOf(value, ink)) {
    cell.hasInk = true;
    cell.ink = ink;
  } else if (name == "rotate" && hasValue) {
    cell.rotate = static_cast<float>(std::atof(value.c_str()));
  } else if (name == "inner sep" && hasValue) {
    cell.innerX = cell.innerY = lengthOf(value);
  } else if (name == "inner xsep" && hasValue) {
    cell.innerX = lengthOf(value);
  } else if (name == "inner ysep" && hasValue) {
    cell.innerY = lengthOf(value);
  } else if (key == "circle") {
    cell.shape = DiagramCell::Shape::circle;
  } else if (key == "ellipse") {
    cell.shape = DiagramCell::Shape::ellipse;
  } else if (key == "rectangle") {
    cell.shape = DiagramCell::Shape::rectangle;
  } else if (name == "minimum size" && hasValue) {
    cell.minWidth = cell.minHeight = lengthOf(value);
  } else if (name == "minimum width" && hasValue) {
    cell.minWidth = lengthOf(value);
  } else if (name == "minimum height" && hasValue) {
    cell.minHeight = lengthOf(value);
  } else if (name == "font" && hasValue) {
    cell.font = value;
  } else if (colourOf(key, ink)) {
    cell.hasInk = true;
    cell.ink = ink;
  } else if (name == "nodes" && hasValue) {
    for (const std::string& k : splitTop(value, ',')) cellKey(k, cell, warned, false);
  } else {
    warned.once("cell option " + key + " is not supported and is ignored");
  }
}

}  // namespace diagram

// tikz-cd: [options] and then the cells, each with the arrows it starts.
inline cmdmacro(tikzcdATATenv) {
  using namespace diagram;
  std::string body = args.text(1);
  // What `\tikzcdset` said before this diagram, which its own options follow.
  std::string sets;
  for (;;) {
    std::size_t i = 0;
    while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i])) != 0) i++;
    if (body.compare(i, 9, "\\gmcdset{") != 0) break;
    i += 8;
    sets += groupAt(body, i) + ",";
    body.erase(0, i);
  }
  const std::string options = listPeelOptional(body);
  Warned warned(args);
  auto atom = sptrOf<DiagramAtom>();
  Defaults def;
  bool ampersand = false, rowSet = false, colSet = false;
  std::string diagramAfter;
  std::function<void(const std::string&)> apply = [&](const std::string& list) {
    for (const std::string& raw : splitTop(list, ',')) {
      const std::string key = trim(raw);
      if (key.empty()) continue;
      std::string name, value;
      bool hasValue = false;
      keyValue(key, name, value, hasValue);
      // A key with a path: `row sep/normal`, `every arrow/.append style`, `my style/.style`.
      const std::size_t slash = name.find('/');
      if (slash != std::string::npos) {
        const std::string head = trim(name.substr(0, slash)), tail = trim(name.substr(slash + 1));
        const bool append = tail == ".append style", style = tail == ".style";
        if (head == "row sep" || head == "column sep") {
          const std::string size = tail.size() > 9 && tail.compare(tail.size() - 9, 9, "/.initial") == 0 ? tail.substr(0, tail.size() - 9) : tail;
          (head == "row sep" ? def.sizes.row : def.sizes.column)[trim(size)] = value;
        } else if (head == "every matrix" || head == "every node") {
          warned.once("option " + name + " is not supported and is ignored");
        } else if ((head == "every arrow" || head == "every label" || head == "every cell" || head == "every diagram") && (append || style)) {
          std::string& into = head == "every arrow" ? def.arrows : head == "every label" ? def.labels : head == "every cell" ? def.cells : diagramAfter;
          into = (style ? "" : into + ",") + value;
          if (head == "every diagram") apply(value);
        } else if (style || append) {
          def.styles[head] = (append && def.styles.count(head) != 0 ? def.styles[head] + "," : "") + value;
        } else {
          warned.once("option " + name + " is not supported and is ignored");
        }
        continue;
      }
      color ink = 0;
      if (name == "row sep" || name == "column sep" || name == "sep") {
        const Dimen row = sepOf(value, true, def.sizes), col = sepOf(value, false, def.sizes);
        // Only the sizes the key is for have to be sizes: a `column sep` name is not a `row sep` one.
        if ((name != "column sep" && !row.isValid()) || (name != "row sep" && !col.isValid())) {
          warned.once(name + "=" + value + " is not a size: the default is kept");
          continue;
        }
        if (name != "column sep") atom->rowSep = row, rowSet = true;
        if (name != "row sep") atom->colSep = col, colSet = true;
      } else if (name == "ampersand replacement") {
        ampersand = true;
      } else if (name == "cramped") {
        atom->cramped = true;
      } else if (name == "arrows") {
        def.arrows += "," + value;
      } else if (name == "labels") {
        def.labels += "," + value;
      } else if (name == "cells") {
        def.cells += "," + value;
      } else if (name == "diagrams") {
        apply(value);
      } else if (name == "math mode") {
        def.mathMode = !hasValue || value != "false";
      } else if (name == "background color" && hasValue && colourOf(value, ink)) {
        def.hasBackground = true;
        def.background = ink;
      } else if (name == "crossing over clearance" && hasValue) {
        atom->crossingClearance = lengthOf(value);
      } else if (name == "center yshift" && hasValue) {
        atom->centerShift = lengthOf(value);
      } else if (name == "arrow style") {
        if (value != "Computer Modern") warned.once("arrow style=" + value + " is drawn as the default, Computer Modern's");
      } else if (def.styles.count(key) != 0) {
        apply(def.styles.at(key));
      } else {
        warned.once("option " + name + " is not supported and is ignored");
      }
    }
  };
  // As in tikz-cd, `\tikzcdset` of a row or column separation is undone by every diagram, which says `normal`.
  std::string kept;
  for (const std::string& raw : splitTop(sets, ',')) {
    const std::string n = trim(trim(raw).substr(0, trim(raw).find('=')));
    if (n != "row sep" && n != "column sep" && n != "sep") kept += raw + ",";
  }
  apply(kept + options);
  // What no key said is `normal`, which a document may have changed.
  if (!rowSet) {
    const Dimen d = sepOf("normal", true, def.sizes);
    if (d.isValid()) atom->rowSep = d;
  }
  if (!colSet) {
    const Dimen d = sepOf("normal", false, def.sizes);
    if (d.isValid()) atom->colSep = d;
  }
  if (ampersand) {
    for (std::size_t p = body.find("\\&"); p != std::string::npos; p = body.find("\\&", p)) {
      body.replace(p, 2, "&");
    }
  }

  // Rows at `\\`, cells at `&`; an arrow is a command in the cell it leaves.
  struct Pending {
    int row, col;
    std::string options;
  };
  std::vector<Pending> pending;
  int row = 0;
  std::vector<std::string> rows;
  {
    std::string cur;
    int brace = 0;
    for (std::size_t i = 0; i < body.size(); i++) {
      const char c = body[i];
      if (c == '\\' && i + 1 < body.size()) {
        if (body[i + 1] == '\\' && brace == 0) {
          rows.push_back(cur);
          cur.clear();
          i++;
          continue;
        }
        cur += c;
        cur += body[++i];
        continue;
      }
      if (c == '{') brace++;
      if (c == '}') brace--;
      cur += c;
    }
    rows.push_back(cur);
  }
  static const struct {
    const char* name;
    const char* direction;
  } commands[] = {{"arrow", ""},  {"ar", ""},    {"rar", "r"},   {"lar", "l"},   {"dar", "d"},
                  {"uar", "u"},   {"urar", "ur"}, {"ular", "ul"}, {"drar", "dr"}, {"dlar", "dl"}};
  std::map<std::string, std::pair<int, int>> aliases;
  const auto extraOf = [&](std::string& text, Dimen& out) {
    // `[1.5em]` right after `&` or `\\` is extra space there, as pgf's matrices read it.
    std::size_t at = 0;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])) != 0) at++;
    if (at >= text.size() || text[at] != '[') return;
    std::size_t end = at;
    const std::string inner = groupAt(text, end);
    const Dimen d = lengthOf(inner);
    if (d.isValid()) {
      out = d;
      text.erase(0, end);
    }
  };
  for (std::size_t r = 0; r < rows.size(); r++) {
    std::string line = rows[r];
    // A body with nothing in it has no row; a `\\` at the end of one that has opens one, as pgf's matrix does
    // (LuaLaTeX: `A \arrow[r] & B \\` is 26.8pt tall, a row more than without the `\\`).
    if (trim(line).empty() && rows.size() == 1) continue;
    atom->rowExtra.emplace_back();
    // `[len]` is read after a `\\`: the first row has none before it
    if (r > 0) extraOf(line, atom->rowExtra.back());
    std::vector<sptr<Atom>> cells;
    std::vector<DiagramCell> cellOptions;
    int col = 0;
    for (std::string cell : splitTop(line, '&')) {
      if (col > 0) {
        if (static_cast<int>(atom->columnExtra.size()) <= col) atom->columnExtra.resize(col + 1);
        extraOf(cell, atom->columnExtra[col]);
      }
      std::string content;
      for (std::size_t i = 0; i < cell.size();) {
        if (cell[i] != '\\') {
          content += cell[i++];
          continue;
        }
        std::size_t j = i + 1;
        while (j < cell.size() && std::isalpha(static_cast<unsigned char>(cell[j])) != 0) j++;
        const std::string name = cell.substr(i + 1, j - i - 1);
        const char* direction = nullptr;
        for (const auto& c : commands) {
          if (name == c.name) direction = c.direction;
        }
        if (direction == nullptr) {
          content += cell.substr(i, std::max<std::size_t>(j - i, 2));
          i = std::max<std::size_t>(j, i + 2);
          continue;
        }
        std::size_t at = j;
        const auto skip = [&] {
          while (at < cell.size() && std::isspace(static_cast<unsigned char>(cell[at])) != 0) at++;
        };
        skip();
        std::string opts = direction;
        if (at < cell.size() && cell[at] == '[') {
          const std::string inner = groupAt(cell, at);
          opts += (opts.empty() ? "" : ",") + inner;
        }
        // The older `\arrow[options]{rr}[label options]{label}[label options]{label}`: a target (`\arrow` has
        // it; `\rar` and kin, whose target is in their name, do not) and then labels, each with its own options.
        skip();
        const bool named = std::string(direction).empty();
        if (at < cell.size() && cell[at] == '{') {
          std::size_t old = at;
          const std::string first = trim(groupAt(cell, old));
          Target probe;
          if (named && (targetOf(first, probe) || first.empty())) {
            // An empty `{}` after the options is no target: tikz-cd takes it as the empty group it is.
            if (!first.empty()) opts += (opts.empty() ? "" : ",") + std::string(isDirection(first) ? "" : "to=") + first;
            at = old;
          }
        }
        for (;;) {
          skip();
          std::string labelOpts;
          std::size_t look = at;
          if (look < cell.size() && cell[look] == '[') {
            labelOpts = groupAt(cell, look);
            while (look < cell.size() && std::isspace(static_cast<unsigned char>(cell[look])) != 0) look++;
          }
          if (look < cell.size() && cell[look] == '{') {
            const std::string text = groupAt(cell, look);
            opts += ",\"" + text + "\"" + (labelOpts.empty() ? "" : "{" + labelOpts + "}");
            at = look;
            continue;
          }
          break;
        }
        pending.push_back({row, col, opts});
        i = at;
      }
      // `|[draw, name=Z]| text`: the options of the node.
      DiagramCell node;
      std::string text = trim(content);
      if (text.compare(0, 2, "|[") == 0) {
        const std::size_t close = text.find("]|");
        if (close != std::string::npos) {
          for (const std::string& k : splitTop(text.substr(2, close - 2), ',')) cellKey(k, node, warned);
          text = trim(text.substr(close + 2));
        }
      }
      for (const std::string& k : splitTop(def.cells, ',')) {
        DiagramCell defaults;
        cellKey(k, defaults, warned, true);
        // The diagram's `cells=` are the starting point; the node's own options win.
        if (defaults.hasDraw && !node.hasDraw) { node.hasDraw = true; node.hasDrawInk = defaults.hasDrawInk; node.drawInk = defaults.drawInk; }
        if (defaults.hasFill && !node.hasFill) { node.hasFill = true; node.fill = defaults.fill; }
        if (defaults.hasInk && !node.hasInk) { node.hasInk = true; node.ink = defaults.ink; }
        if (defaults.rotate != 0.f && node.rotate == 0.f) node.rotate = defaults.rotate;
        if (defaults.innerX.isValid() && !node.innerX.isValid()) node.innerX = defaults.innerX;
        if (defaults.innerY.isValid() && !node.innerY.isValid()) node.innerY = defaults.innerY;
        if (defaults.shape != DiagramCell::Shape::rectangle && node.shape == DiagramCell::Shape::rectangle) node.shape = defaults.shape;
        if (defaults.minWidth.isValid() && !node.minWidth.isValid()) node.minWidth = defaults.minWidth;
        if (defaults.minHeight.isValid() && !node.minHeight.isValid()) node.minHeight = defaults.minHeight;
        if (!defaults.font.empty() && node.font.empty()) node.font = defaults.font;
      }
      if (!node.font.empty() && !text.empty()) text = "{" + node.font + " " + text + "}";
      if (!node.name.empty()) aliases[node.name] = {row, col};
      sptr<Atom> content_atom = text.empty() ? nullptr : args.formulaChecked(text, def.mathMode);
      if (content_atom != nullptr && node.hasInk) content_atom = sptrOf<ColorAtom>(content_atom, TRANSPARENT, node.ink);
      cells.push_back(content_atom);
      cellOptions.push_back(node);
      col++;
    }
    atom->cells.push_back(std::move(cells));
    atom->cellOptions.push_back(std::move(cellOptions));
    row++;
  }
  for (const Pending& p : pending) {
    DiagramArrow arrow;
    Placement place;
    arrowOptions(args, warned, p.options, arrow, place, def);
    // The direction is from the cell the arrow is written in; `from` and
    // `to` name cells outright, or by a direction from it, or a node by its name.
    const auto resolve = [&](const Target& t, int& r, int& c, std::string& name, int dr, int dc) {
      if (t.set && !t.name.empty()) {
        const auto it = aliases.find(t.name);
        if (it != aliases.end()) {
          r = it->second.first;
          c = it->second.second;
        } else {
          name = t.name;
          r = c = 0;
        }
        return;
      }
      r = t.set ? (t.absolute ? t.row : p.row + t.row) : p.row + dr;
      c = t.set ? (t.absolute ? t.col : p.col + t.col) : p.col + dc;
    };
    resolve(place.from, arrow.row, arrow.col, arrow.fromName, 0, 0);
    resolve(place.to, arrow.toRow, arrow.toCol, arrow.toName, place.dr, place.dc);
    const int cols = static_cast<int>(atom->cells.empty() ? 0 : atom->cells[0].size());
    int widest = cols;
    for (const auto& r : atom->cells) widest = std::max(widest, static_cast<int>(r.size()));
    const bool loops = arrow.hasOut || arrow.hasIn || arrow.hasControls;
    if (arrow.row == arrow.toRow && arrow.col == arrow.toCol && arrow.fromName.empty() && arrow.toName.empty() && !loops) {
      warned.once("\\arrow: an arrow with no direction goes nowhere and is not drawn");
      continue;
    }
    if (arrow.fromName.empty() && (arrow.row < 0 || arrow.row >= static_cast<int>(atom->cells.size()) || arrow.col < 0 ||
                                   arrow.col >= widest)) {
      warned.once("\\arrow: an arrow starts outside the diagram and is not drawn");
      continue;
    }
    if (arrow.toName.empty() && (arrow.toRow < 0 || arrow.toRow >= static_cast<int>(atom->cells.size()) ||
                                 arrow.toCol < 0 || arrow.toCol >= widest)) {
      warned.once("\\arrow: an arrow points outside the diagram and is not drawn");
      continue;
    }
    atom->arrows.push_back(std::move(arrow));
  }
  // A name that is not a cell's is a label's, which another arrow made.
  std::set<std::string> labelNames;
  for (const DiagramArrow& a : atom->arrows) {
    for (const DiagramLabel& l : a.labels) {
      if (!l.name.empty()) labelNames.insert(l.name);
    }
  }
  for (std::size_t k = 0; k < atom->arrows.size();) {
    const DiagramArrow& a = atom->arrows[k];
    const std::string* missing = !a.fromName.empty() && labelNames.count(a.fromName) == 0 ? &a.fromName
                               : !a.toName.empty() && labelNames.count(a.toName) == 0     ? &a.toName
                                                                                          : nullptr;
    if (missing == nullptr) {
      k++;
      continue;
    }
    warned.once("\\arrow: " + *missing + " is not a cell (row-column or direction), a named cell or a named label; the arrow is not drawn");
    atom->arrows.erase(atom->arrows.begin() + static_cast<std::ptrdiff_t>(k));
  }
  return atom;
}

// amscd: the rows of an object row are the objects, with an arrow between
// each two of them; a row of vertical arrows has one at each column, `@.`
// for none.
inline cmdmacro(CDATATenv) {
  using namespace diagram;
  const std::string body = args.text(1);
  Warned warned(args);
  auto atom = sptrOf<DiagramAtom>();
  // As amscd sets them: no space in a cell but the \enskip an arrow leaves,
  // two baselineskips between objects, and 2.5pc least for an arrow.
  atom->colSep = Units::getDimen("0em");
  atom->rowSep = Units::getDimen("0.7ex");
  atom->innerX = Units::getDimen("0.5em");
  atom->innerY = Units::getDimen("0ex");
  atom->texRows = true;

  using A = DiagramArrow;
  // The arrow `@X a X b X` starts at `at` (just after the X) and reads two
  // labels, the first up to an `X` outside braces.
  struct Read {
    std::string first, second;
    std::size_t end;
  };
  const auto labels = [&](const std::string& s, std::size_t at, char x) {
    Read r;
    std::string* into[2] = {&r.first, &r.second};
    int brace = 0;
    int which = 0;
    std::size_t i = at;
    for (; i < s.size() && which < 2; i++) {
      const char c = s[i];
      if (c == '\\' && i + 1 < s.size()) {
        *into[which] += c;
        *into[which] += s[++i];
        continue;
      }
      if (c == '{') brace++;
      if (c == '}') brace--;
      if (c == x && brace == 0) {
        which++;
        continue;
      }
      *into[which] += c;
    }
    r.end = i;
    return r;
  };
  const auto label = [&](const std::string& text, DiagramLabel::Side side) {
    DiagramLabel l;
    l.body = args.formulaChecked(trim(text), true);
    l.side = side;
    return l;
  };

  std::vector<std::string> lines;
  {
    std::string cur;
    for (std::size_t i = 0; i < body.size(); i++) {
      if (body[i] == '\\' && i + 1 < body.size()) {
        if (body[i + 1] == '\\') {
          lines.push_back(cur);
          cur.clear();
          i++;
          continue;
        }
        cur += body[i];
        cur += body[++i];
        continue;
      }
      cur += body[i];
    }
    lines.push_back(cur);
  }

  struct Vertical {
    int row;  // the object row above the arrow
    int col;
    char kind;  // V, A, |
    std::string first, second;
  };
  std::vector<Vertical> verticals;
  int objectRows = 0;
  for (const std::string& line : lines) {
    if (trim(line).empty()) continue;
    // Objects are the text between the `@` items; the items of an object
    // row join them, those of an arrow row stand for a column each.
    std::vector<std::string> gaps{""};
    struct Item {
      char kind;
      std::string first, second;
    };
    std::vector<Item> items;
    for (std::size_t i = 0; i < line.size();) {
      if (line[i] != '@') {
        gaps.back() += line[i++];
        continue;
      }
      if (i + 1 >= line.size()) {
        i++;
        continue;
      }
      const char k = line[i + 1];
      if (k == '@') {
        gaps.back() += '@';
        i += 2;
        continue;
      }
      Item it;
      it.kind = k;
      std::size_t end = i + 2;
      if (k == '>' || k == '<' || k == ')' || k == '(' || k == 'V' || k == 'A') {
        const char x = k == ')' ? '>' : k == '(' ? '<' : k;
        const Read r = labels(line, i + 2, x);
        it.first = r.first;
        it.second = r.second;
        end = r.end;
      } else if (k != '=' && k != '|' && k != '.') {
        warned.once(std::string("@") + k + " is not a CD arrow and is ignored");
        i += 2;
        continue;
      }
      items.push_back(it);
      gaps.emplace_back();
      i = end;
    }
    const bool arrowRow = !items.empty() && (items[0].kind == 'V' || items[0].kind == 'A' ||
                                             items[0].kind == '|' || items[0].kind == '.');
    if (arrowRow) {
      if (objectRows == 0) {
        warned.once("a row of vertical arrows needs an object row above it");
        continue;
      }
      for (std::size_t c = 0; c < items.size(); c++) {
        const Item& it = items[c];
        if (it.kind == 'V' || it.kind == 'A' || it.kind == '|') {
          verticals.push_back({objectRows - 1, static_cast<int>(c), it.kind, it.first, it.second});
        }
      }
      continue;
    }
    std::vector<sptr<Atom>> cells;
    for (const std::string& g : gaps) {
      const std::string t = trim(g);
      cells.push_back(t.empty() ? nullptr : args.formulaChecked(t, true));
    }
    const int r = objectRows++;
    for (std::size_t c = 0; c < items.size(); c++) {
      const Item& it = items[c];
      A arrow;
      arrow.row = r;
      arrow.col = static_cast<int>(c);
      arrow.toRow = r;
      arrow.toCol = static_cast<int>(c) + 1;
      arrow.minLength = Units::getDimen("2.5pc");
      arrow.fitLabels = true;
      if (it.kind == '>' || it.kind == ')') {
        arrow.labels = {label(it.first, DiagramLabel::Side::left), label(it.second, DiagramLabel::Side::right)};
      } else if (it.kind == '<' || it.kind == '(') {
        // Leftwards, above is to the right of the way it goes.
        std::swap(arrow.col, arrow.toCol);
        arrow.labels = {label(it.first, DiagramLabel::Side::right), label(it.second, DiagramLabel::Side::left)};
      } else if (it.kind == '=') {
        arrow.doubled = true;
        arrow.head = A::Head::none;
      } else {
        warned.once(std::string("@") + it.kind + " is not allowed in a row of objects");
        continue;
      }
      atom->arrows.push_back(std::move(arrow));
    }
    atom->cells.push_back(std::move(cells));
  }
  for (const Vertical& v : verticals) {
    if (v.row + 1 >= objectRows) {
      warned.once("a row of vertical arrows needs an object row below it");
      continue;
    }
    A arrow;
    arrow.row = v.row;
    arrow.col = v.col;
    arrow.toRow = v.row + 1;
    arrow.toCol = v.col;
    arrow.fixedLength = Units::getDimen("1.8em");
    if (v.kind == 'V') {
      // Down: the left label is to the right of the way it goes.
      arrow.labels = {label(v.first, DiagramLabel::Side::right), label(v.second, DiagramLabel::Side::left)};
    } else if (v.kind == 'A') {
      std::swap(arrow.row, arrow.toRow);
      arrow.labels = {label(v.first, DiagramLabel::Side::left), label(v.second, DiagramLabel::Side::right)};
    } else {
      arrow.doubled = true;
      arrow.head = A::Head::none;
    }
    atom->arrows.push_back(std::move(arrow));
  }
  return atom;
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_DIAGRAM_H
