#ifndef MICROTEX_MACRO_DIAGRAM_H
#define MICROTEX_MACRO_DIAGRAM_H

// The two ways a commutative diagram is written: amscd's CD, whose arrows
// are `@>a>b>`, `@VaVbV` and kin between and under the objects, and
// tikz-cd's tikzcd, whose arrows are `\arrow[r, "f"]` commands inside the
// cells they start from. Both fill a DiagramAtom (atom/diagram_atom.h).

#include <cctype>
#include <cstdlib>
#include <functional>
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
  return "";
}

/** A number or a tikz size name to a size in em. */
inline Dimen sepOf(const std::string& value, bool row) {
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

/** One label of an arrow as written, and what its own options say. */
struct LabelSpec {
  std::string text;
  bool swap = false;
  bool described = false;
  bool hasPos = false;
  float pos = 0.5f;
};

/** `near start`, `swap`, `pos=0.3` and kin, which place a label. */
inline bool labelKey(const std::string& key, LabelSpec& l) {
  if (key == "swap" || key == "right" || key == "auto=right") {
    l.swap = true;
  } else if (key == "left" || key == "auto" || key == "auto=left") {
    l.swap = false;
  } else if (key == "description") {
    l.described = true;
  } else if (key == "very near start") {
    l.pos = 0.1f;
    l.hasPos = true;
  } else if (key == "near start") {
    l.pos = 0.25f;
    l.hasPos = true;
  } else if (key == "near end") {
    l.pos = 0.75f;
    l.hasPos = true;
  } else if (key == "very near end") {
    l.pos = 0.9f;
    l.hasPos = true;
  } else if (key == "at start") {
    l.pos = 0.f;
    l.hasPos = true;
  } else if (key == "at end") {
    l.pos = 1.f;
    l.hasPos = true;
  } else if (key.compare(0, 4, "pos=") == 0) {
    l.pos = static_cast<float>(std::atof(key.c_str() + 4));
    l.hasPos = true;
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

/** A cell an arrow starts or ends at, other than the one it is written in:
 *  `2-1` (row and column, from 1) or a direction such as `rr`. */
struct Target {
  bool set = false;
  bool absolute = false;
  int row = 0, col = 0;
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
    if (!digits(r) || !digits(c)) return false;
    t = {true, true, std::atoi(r.c_str()) - 1, std::atoi(c.c_str()) - 1};
    return true;
  }
  if (!isDirection(v)) return false;
  t = {true, false, 0, 0};
  for (const char ch : v) {
    t.row += ch == 'd' ? 1 : ch == 'u' ? -1 : 0;
    t.col += ch == 'r' ? 1 : ch == 'l' ? -1 : 0;
  }
  return true;
}

/** Where an arrow written in a cell goes: `dr`, `dc` rows and columns away,
 *  unless `to` says; and where it starts, if not in that cell. */
struct Placement {
  int dr = 0, dc = 0;
  Target from, to;
};

/** An arrow's options, as tikz-cd reads them: where it goes, how it is
 *  drawn, and its labels. `labelDefaults` are the options every label has
 *  (the diagram's `labels=`). */
inline void arrowOptions(CommandArgs& args, Warned& warned, const std::string& options,
                         DiagramArrow& arrow, Placement& place, const std::string& labelDefaults) {
  std::vector<LabelSpec> labels;
  bool swapAll = false, describeAll = false, posAll = false;
  float posDefault = 0.5f;
  for (const std::string& raw : splitTop(options, ',')) {
    const std::string key = trim(raw);
    if (key.empty()) continue;
    if (key[0] == '"') {
      // "text" then, perhaps, ' and the label's own options.
      std::size_t end = 1;
      while (end < key.size() && key[end] != '"') end += key[end] == '\\' ? 2 : 1;
      LabelSpec l;
      l.text = key.substr(1, end - 1);
      for (const std::string& k : splitTop(labelDefaults, ',')) {
        const std::string opt = trim(k);
        if (!opt.empty() && !labelKey(opt, l)) warned.once("label option " + opt + " is ignored");
      }
      std::string rest = trim(end < key.size() ? key.substr(end + 1) : "");
      if (!rest.empty() && rest[0] == '\'') {
        l.swap = true;
        rest = trim(rest.substr(1));
      }
      if (!rest.empty() && rest[0] == '{') {
        std::size_t at = 0;
        rest = groupAt(rest, at);
      }
      for (const std::string& k : splitTop(rest, ',')) {
        const std::string opt = trim(k);
        if (!opt.empty() && !labelKey(opt, l)) warned.once("label option " + opt + " is ignored");
      }
      labels.push_back(l);
      continue;
    }
    if (isDirection(key)) {
      for (const char c : key) {
        place.dr += c == 'd' ? 1 : c == 'u' ? -1 : 0;
        place.dc += c == 'r' ? 1 : c == 'l' ? -1 : 0;
      }
      continue;
    }
    // `name=value` keys.
    const std::size_t eq = key.find('=');
    const std::string name = trim(key.substr(0, eq));
    const std::string given = eq == std::string::npos ? "" : trim(key.substr(eq + 1));
    if (name == "from" || name == "to") {
      Target& t = name == "from" ? place.from : place.to;
      if (!targetOf(given, t)) {
        warned.once("\\arrow: " + key + " names a cell by row-column or direction only; it is ignored");
      }
      continue;
    }
    if (name == "color" || name == "draw") {
      if (given == "none") {
        arrow.phantom = true;
      } else if (ColorAtom::hasName(given) || (!given.empty() && given[0] == '#')) {
        arrow.hasColor = true;
        arrow.ink = ColorAtom::getColor(given);
      } else {
        warned.once("\\arrow: " + key + " is not a colour that is defined; it is ignored");
      }
      continue;
    }
    if (ColorAtom::hasName(key)) {
      arrow.hasColor = true;
      arrow.ink = ColorAtom::getColor(key);
      continue;
    }
    LabelSpec probe;
    const auto value = [&](const std::string& name, float fallback) {
      if (key.compare(0, name.size(), name) != 0) return -1.f;
      const std::string v = trim(key.substr(name.size()));
      if (v.empty()) return fallback;
      return v[0] == '=' ? static_cast<float>(std::atof(v.c_str() + 1)) : -1.f;
    };
    using A = DiagramArrow;
    if (key == "hook") {
      arrow.tail = A::Tail::hook;
    } else if (key == "hook'") {
      arrow.tail = A::Tail::hookBack;
    } else if (key == "two heads") {
      arrow.head = A::Head::two;
    } else if (key == "tail") {
      arrow.tail = A::Tail::tail;
    } else if (key == "mapsto" || key == "maps to") {
      // `mapsto` sets both ends, so a head chosen before it is gone.
      arrow.tail = A::Tail::bar;
      if (key == "mapsto") arrow.head = A::Head::one;
    } else if (key == "dashed" || key == "dashrightarrow") {
      arrow.dash = A::Dash::dashed;
    } else if (key == "dotted") {
      arrow.dash = A::Dash::dotted;
    } else if (key == "equal" || key == "equals") {
      arrow.doubled = true;
      arrow.head = A::Head::none;
    } else if (key == "Rightarrow") {
      arrow.doubled = true;
      arrow.head = A::Head::implies;
    } else if (key == "leftarrow" || key == "leftrightarrow") {
      arrow.tail = A::Tail::head;
      arrow.head = key == "leftarrow" ? A::Head::none : A::Head::one;
    } else if (key == "Leftarrow" || key == "Leftrightarrow") {
      arrow.doubled = true;
      arrow.tail = A::Tail::implies;
      arrow.head = key == "Leftarrow" ? A::Head::none : A::Head::implies;
    } else if (key.compare(0, 7, "shorten") == 0 && key.size() > 7) {
      // `shorten <=1ex`, `shorten >=1ex`, and `shorten=1ex` for both.
      const std::string rest = trim(key.substr(7));
      const bool start = rest.compare(0, 2, "<=") == 0, end = rest.compare(0, 2, ">=") == 0;
      const Dimen by = sepOf(rest.substr(start || end ? 2 : (rest.compare(0, 1, "=") == 0 ? 1 : 0)), true);
      if (!by.isValid()) {
        warned.once("\\arrow: " + key + " is not a size and is ignored");
      } else {
        if (!end) arrow.shortenStart = by;
        if (!start) arrow.shortenEnd = by;
      }
    } else if (key == "no head" || key == "dash") {
      arrow.head = A::Head::none;
      if (key == "dash") arrow.tail = A::Tail::none;
    } else if (key == "no tail") {
      arrow.tail = A::Tail::none;
    } else if (key == "rightarrow") {
      arrow.head = A::Head::one;
      arrow.tail = A::Tail::none;
    } else if (key == "to head") {
      arrow.head = A::Head::one;
    } else if (key == "phantom") {
      arrow.phantom = true;
    } else if (key == "crossing over") {
      arrow.crossing = true;
    } else if (key == "harpoon" || key == "harpoon'") {
      arrow.head = key == "harpoon" ? A::Head::harpoonLeft : A::Head::harpoonRight;
    } else if (key == "rightharpoonup" || key == "rightharpoondown") {
      arrow.tail = A::Tail::none;
      arrow.head = key == "rightharpoonup" ? A::Head::harpoonLeft : A::Head::harpoonRight;
    } else if (key == "squiggly" || key == "rightsquigarrow" || key == "leftsquigarrow" ||
               key == "leftrightsquigarrow") {
      arrow.squiggly = true;
      if (key == "rightsquigarrow") {
        arrow.tail = A::Tail::none;
        arrow.head = A::Head::one;
      } else if (key != "squiggly") {
        arrow.tail = A::Tail::head;
        arrow.head = key == "leftsquigarrow" ? A::Head::none : A::Head::one;
      }
    } else if (key == "hookrightarrow") {
      arrow.tail = A::Tail::hook;
      arrow.head = A::Head::one;
    } else if (key == "twoheadrightarrow") {
      arrow.tail = A::Tail::none;
      arrow.head = A::Head::two;
    } else if (key == "rightarrowtail") {
      arrow.tail = A::Tail::tail;
      arrow.head = A::Head::one;
    } else if (key == "dashleftarrow") {
      arrow.dash = A::Dash::dashed;
      arrow.tail = A::Tail::head;
      arrow.head = A::Head::none;
    } else if (key == "mapsfrom" || key == "Mapsto" || key == "Mapsfrom") {
      arrow.doubled = key != "mapsfrom";
      arrow.tail = key == "Mapsto" ? A::Tail::bar : key == "mapsfrom" ? A::Tail::head : A::Tail::implies;
      arrow.head = key == "Mapsto" ? A::Head::implies : A::Head::bar;
    } else if (key.compare(0, 9, "bend left") == 0 && value("bend left", 30.f) >= 0.f) {
      arrow.bend = value("bend left", 30.f);
    } else if (key.compare(0, 10, "bend right") == 0 && value("bend right", 30.f) >= 0.f) {
      arrow.bend = -value("bend right", 30.f);
    } else if (key.compare(0, 10, "shift left") == 0 && value("shift left", 1.f) >= 0.f) {
      arrow.shift = value("shift left", 1.f);
    } else if (key.compare(0, 11, "shift right") == 0 && value("shift right", 1.f) >= 0.f) {
      arrow.shift = -value("shift right", 1.f);
    } else if (labelKey(key, probe)) {
      // Said of the arrow, it is said of its labels.
      swapAll = swapAll || probe.swap;
      describeAll = describeAll || probe.described;
      if (probe.hasPos) {
        posAll = true;
        posDefault = probe.pos;
      }
    } else {
      warned.once("\\arrow: " + key + " is not supported and is ignored");
    }
  }
  for (const LabelSpec& l : labels) {
    DiagramLabel out;
    out.body = args.formulaChecked(l.text, true);
    if (arrow.hasColor && out.body != nullptr) out.body = sptrOf<ColorAtom>(out.body, TRANSPARENT, arrow.ink);
    const bool described = l.described || describeAll;
    out.side = described ? DiagramLabel::Side::center
               : (l.swap != swapAll) ? DiagramLabel::Side::right
                                     : DiagramLabel::Side::left;
    out.pos = l.hasPos ? l.pos : posAll ? posDefault : 0.5f;
    arrow.labels.push_back(out);
  }
}

}  // namespace diagram

// tikz-cd: [options] and then the cells, each with the arrows it starts.
inline cmdmacro(tikzcdATATenv) {
  using namespace diagram;
  std::string body = args.text(1);
  const std::string options = listPeelOptional(body);
  Warned warned(args);
  auto atom = sptrOf<DiagramAtom>();
  bool ampersand = false;
  // `arrows=` and `labels=` are options of every arrow and every label.
  std::string arrowDefaults, labelDefaults;
  std::function<void(const std::string&)> apply = [&](const std::string& list) {
    for (const std::string& raw : splitTop(list, ',')) {
      const std::string key = trim(raw);
      const std::size_t eq = key.find('=');
      const std::string name = trim(key.substr(0, eq));
      std::string value = eq == std::string::npos ? "" : trim(key.substr(eq + 1));
      if (!value.empty() && value[0] == '{') {
        std::size_t at = 0;
        value = groupAt(value, at);
      }
      if (key.empty()) continue;
      if (name == "row sep" || name == "column sep" || name == "sep") {
        const Dimen row = sepOf(value, true), col = sepOf(value, false);
        if (!row.isValid()) {
          warned.once(name + "=" + value + " is not a size: the default is kept");
          continue;
        }
        if (name != "column sep") atom->rowSep = row;
        if (name != "row sep") atom->colSep = col;
      } else if (name == "ampersand replacement") {
        ampersand = true;
      } else if (name == "cramped") {
        atom->cramped = true;
      } else if (name == "arrows") {
        arrowDefaults += "," + value;
      } else if (name == "labels") {
        labelDefaults += "," + value;
      } else if (name == "diagrams") {
        apply(value);
      } else if (name != "math mode") {
        warned.once("option " + name + " is not supported and is ignored");
      }
    }
  };
  apply(options);
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
  for (const std::string& line : rows) {
    if (trim(line).empty() && &line == &rows.back()) continue;  // a trailing \\ opens no row
    std::vector<sptr<Atom>> cells;
    int col = 0;
    for (const std::string& cell : splitTop(line, '&')) {
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
        // The older `\arrow[options]{rr}[label options]{label}`.
        skip();
        std::size_t old = at;
        if (old < cell.size() && cell[old] == '{') {
          const std::string dir = trim(groupAt(cell, old));
          if (isDirection(dir)) {
            opts += (opts.empty() ? "" : ",") + dir;
            at = old;
            skip();
            std::string labelOpts;
            if (at < cell.size() && cell[at] == '[') labelOpts = groupAt(cell, at);
            skip();
            if (at < cell.size() && cell[at] == '{') {
              const std::string text = groupAt(cell, at);
              opts += ",\"" + text + "\"" + (labelOpts.empty() ? "" : "{" + labelOpts + "}");
            }
          }
        }
        pending.push_back({row, col, opts});
        i = at;
      }
      const std::string text = trim(content);
      cells.push_back(text.empty() ? nullptr : args.formulaChecked(text, true));
      col++;
    }
    atom->cells.push_back(std::move(cells));
    row++;
  }
  for (const Pending& p : pending) {
    DiagramArrow arrow;
    Placement place;
    arrowOptions(args, warned, arrowDefaults + "," + p.options, arrow, place, labelDefaults);
    // The direction is from the cell the arrow is written in; `from` and
    // `to` name cells outright, or by a direction from it.
    arrow.row = place.from.set ? (place.from.absolute ? place.from.row : p.row + place.from.row) : p.row;
    arrow.col = place.from.set ? (place.from.absolute ? place.from.col : p.col + place.from.col) : p.col;
    arrow.toRow = place.to.set ? (place.to.absolute ? place.to.row : p.row + place.to.row) : p.row + place.dr;
    arrow.toCol = place.to.set ? (place.to.absolute ? place.to.col : p.col + place.to.col) : p.col + place.dc;
    const int cols = static_cast<int>(atom->cells.empty() ? 0 : atom->cells[0].size());
    int widest = cols;
    for (const auto& r : atom->cells) widest = std::max(widest, static_cast<int>(r.size()));
    if (arrow.row == arrow.toRow && arrow.col == arrow.toCol) {
      warned.once("\\arrow: an arrow with no direction goes nowhere and is not drawn");
      continue;
    }
    if (arrow.row < 0 || arrow.row >= static_cast<int>(atom->cells.size()) || arrow.col < 0 ||
        arrow.col >= widest) {
      warned.once("\\arrow: an arrow starts outside the diagram and is not drawn");
      continue;
    }
    if (arrow.toRow < 0 || arrow.toRow >= static_cast<int>(atom->cells.size()) ||
        arrow.toCol < 0 || arrow.toCol >= widest) {
      warned.once("\\arrow: an arrow points outside the diagram and is not drawn");
      continue;
    }
    atom->arrows.push_back(std::move(arrow));
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
