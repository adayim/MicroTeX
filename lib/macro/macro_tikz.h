#ifndef MICROTEX_MACRO_TIKZ_H
#define MICROTEX_MACRO_TIKZ_H

// tikzpicture: the picture's source read into a TikzAtom (atom/tikz_atom.h). The reader follows TikZ's
// own grammar -- a statement is a command, its options, and a path of coordinates and operations up to a
// `;` -- and what it does not know it says once and leaves out: a picture that is mostly drawn is more use
// than none.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "atom/atom_basic.h"
#include "atom/tikz_atom.h"
#include "env/units.h"
#include "macro/macro_args.h"
#include "macro/macro_diagram.h"
#include "macro/macro_env.h"

namespace microtex {

namespace tikz {

using diagram::groupAt;
using diagram::splitTop;
using diagram::trim;

inline bool isWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

/** pgfmath, as far as a coordinate needs it: numbers, + - * / ^, brackets and the usual functions. */
class Expr {
public:
  explicit Expr(const std::string& s) : _s(s) {}

  /** The value, or false when the text is not an expression this reads whole. */
  bool value(double& out) {
    _i = 0;
    _ok = true;
    out = sum();
    skip();
    return _ok && _i == _s.size();
  }

private:
  std::string _s;
  std::size_t _i = 0;
  bool _ok = true;

  void skip() {
    while (_i < _s.size() && std::isspace(static_cast<unsigned char>(_s[_i])) != 0) _i++;
  }

  double sum() {
    double v = product();
    for (;;) {
      skip();
      if (_i < _s.size() && (_s[_i] == '+' || _s[_i] == '-')) {
        const char op = _s[_i++];
        const double w = product();
        v = op == '+' ? v + w : v - w;
      } else {
        return v;
      }
    }
  }

  double product() {
    double v = power();
    for (;;) {
      skip();
      if (_i < _s.size() && (_s[_i] == '*' || _s[_i] == '/')) {
        const char op = _s[_i++];
        const double w = power();
        if (op == '*') {
          v *= w;
        } else if (std::fabs(w) < 1e-12) {
          _ok = false;
        } else {
          v /= w;
        }
      } else {
        return v;
      }
    }
  }

  double power() {
    double v = unary();
    skip();
    if (_i < _s.size() && _s[_i] == '^') {
      _i++;
      v = std::pow(v, power());
    }
    return v;
  }

  double unary() {
    skip();
    if (_i < _s.size() && (_s[_i] == '-' || _s[_i] == '+')) {
      const char op = _s[_i++];
      const double v = unary();
      return op == '-' ? -v : v;
    }
    return atom();
  }

  /** A trailing `r` says the value is in radians (`sin(\x r)`); the functions take degrees. */
  /** What a unit is in centimetres (a coordinate reads a length so). */
  static bool unitFactor(const std::string& unit, double& factor) {
    static const std::map<std::string, double> cm = {
      {"cm", 1.0}, {"mm", 0.1}, {"in", 2.54}, {"pt", 2.54 / 72.27}, {"bp", 2.54 / 72.0}, {"pc", 12 * 2.54 / 72.27},
      {"dd", 1238.0 / 1157.0 * 2.54 / 72.27}, {"cc", 12 * 1238.0 / 1157.0 * 2.54 / 72.27},
      {"sp", 2.54 / 72.27 / 65536.0}, {"em", 10 * 2.54 / 72.27}, {"ex", 4.3 * 2.54 / 72.27}};
    const auto it = cm.find(unit);
    if (it == cm.end()) return false;
    factor = it->second;
    return true;
  }

  /** After a bracketed value or a number, a word that says what it is: `r` for radians (the functions take degrees), or a
   *  unit of length (`(\x) cm`, which is what a plot's variable becomes once it has a number). */
  double radiansSuffix(double v) {
    std::size_t j = _i;
    while (j < _s.size() && std::isspace(static_cast<unsigned char>(_s[j])) != 0) j++;
    std::size_t k = j;
    while (k < _s.size() && std::isalpha(static_cast<unsigned char>(_s[k])) != 0) k++;
    const std::string word = _s.substr(j, k - j);
    double f = 0;
    if (word == "r") {
      _i = k;
      return v * 180.0 / 3.14159265358979323846;
    }
    if (!word.empty() && unitFactor(word, f)) {
      _i = k;
      return v * f;
    }
    return v;
  }

  double atom() {
    skip();
    if (_i >= _s.size()) {
      _ok = false;
      return 0;
    }
    if (_s[_i] == '(') {
      _i++;
      const double v = sum();
      skip();
      if (_i < _s.size() && _s[_i] == ')') {
        _i++;
      } else {
        _ok = false;
      }
      return radiansSuffix(v);
    }
    if (std::isdigit(static_cast<unsigned char>(_s[_i])) != 0 || _s[_i] == '.') {
      char* end = nullptr;
      const double v = std::strtod(_s.c_str() + _i, &end);
      _i = static_cast<std::size_t>(end - _s.c_str());
      // A unit on the number (`3mm`, `\i*3mm`): pgfmath calls the result a dimension, and a coordinate reads a bare
      // number as centimetres, so every length here is centimetres.
      std::size_t j = _i;
      std::string unit;
      while (j < _s.size() && std::isalpha(static_cast<unsigned char>(_s[j])) != 0) unit += _s[j++];
      if (unit == "r") {
        _i = j;
        return v * 180.0 / 3.14159265358979323846;
      }
      if (!unit.empty()) {
        double factor = 0;
        if (!unitFactor(unit, factor)) {
          _ok = false;
          return 0;
        }
        _i = j;
        return v * factor;
      }
      return radiansSuffix(v);
    }
    if (std::isalpha(static_cast<unsigned char>(_s[_i])) != 0) {
      std::string name;
      while (_i < _s.size() && std::isalpha(static_cast<unsigned char>(_s[_i])) != 0) name += _s[_i++];
      if (name == "pi") return 3.14159265358979323846;
      if (name == "e") return 2.71828182845904523536;
      skip();
      if (_i >= _s.size() || _s[_i] != '(') {
        _ok = false;
        return 0;
      }
      _i++;
      std::vector<double> args;
      skip();
      if (_i < _s.size() && _s[_i] != ')') {
        args.push_back(sum());
        skip();
        while (_i < _s.size() && _s[_i] == ',') {
          _i++;
          args.push_back(sum());
          skip();
        }
      }
      if (_i < _s.size() && _s[_i] == ')') {
        _i++;
      } else {
        _ok = false;
      }
      const double a = args.empty() ? 0.0 : args[0], b = args.size() > 1 ? args[1] : 0.0;
      const double toRad = 3.14159265358979323846 / 180.0;
      if (name == "sin") return std::sin(a * toRad);
      if (name == "cos") return std::cos(a * toRad);
      if (name == "tan") return std::tan(a * toRad);
      if (name == "sqrt") {
        if (a < 0) _ok = false;   // not a number: the point is left out, as for 1/0
        return std::sqrt(std::max(0.0, a));
      }
      if (name == "abs") return std::fabs(a);
      if (name == "min") return std::min(a, b);
      if (name == "max") return std::max(a, b);
      if (name == "floor") return std::floor(a);
      if (name == "ceil") return std::ceil(a);
      if (name == "round") return std::round(a);
      if (name == "exp") return std::exp(a);
      if (name == "ln") return std::log(a);
      if (name == "pow") return std::pow(a, b);
      if (name == "asin") return std::asin(a) / toRad;
      if (name == "acos") return std::acos(a) / toRad;
      if (name == "atan") return std::atan(a) / toRad;
      if (name == "atan2") return std::atan2(a, b) / toRad;
      if (name == "deg") return a / toRad;            // radians to degrees
      if (name == "rad") return a * toRad;            // degrees to radians
      if (name == "sinh") return std::sinh(a);
      if (name == "cosh") return std::cosh(a);
      if (name == "tanh") return std::tanh(a);
      if (name == "log10") return std::log10(a);
      if (name == "log2") return std::log2(a);
      if (name == "mod") return std::fabs(b) < 1e-12 ? (_ok = false, 0.0) : a - b * std::floor(a / b);
      _ok = false;
      return 0;
    }
    _ok = false;
    return 0;
  }
};

class Reader {
public:
  /** The most points one plot has, samples, corners of a polygon: a path that long is no picture, and a loop to a billion is a hang. */
  static constexpr double kMaxPoints = 20000;

  Reader(CommandArgs& args, TikzAtom& pic) : _args(args), _pic(pic) {
    // pgf's own styles that a picture may change.
    _styles["edge from parent"] = "draw";
  }

  /** The picture: its options, then its statements. */
  void picture(std::string body) {
    // The styles the input defined before the picture, which the parser puts in front of its text: read first, so that the
    // picture's options (which come first in TikZ) may use them, and are still the first thing read as options.
    // (They were put before `[scale=2]`, which then was not an option list: the picture was dropped.)
    std::size_t from = 0;
    for (;;) {
      std::size_t j = from;
      skipSpace(body, j);
      if (body.compare(j, 8, "\\tikzset") != 0) break;
      j += 8;
      skipSpace(body, j);
      if (j >= body.size() || body[j] != '{') break;
      const std::string keys = groupAt(body, j);
      Style ignored;
      applyOptions(ignored, keys, false, 0);
      from = j;
    }
    body.erase(0, from);
    const std::string options = listPeelOptional(body);
    Style root;
    root.draw = Paint();
    applyOptions(root, options, false, 0);
    run(body, root, 0);
  }

private:
  CommandArgs& _args;
  TikzAtom& _pic;
  std::set<std::string> _warned;
  std::set<std::string> _defined;                  // the nodes read so far
  std::map<std::string, std::string> _styles;      // name -> its options
  std::map<std::string, std::string> _appended;
  int _nodes = 0;
  int _clips = 0;                                  // clip paths made so far: their ids
  int _treeLevel = 0;                             // how deep in a tree the options being read are
  int _treeNames = 0;                              // parents of trees that had no name
  std::string _nodeExtra;                          // a style a node takes after `every node`: `every child node`
  std::vector<PathItem> _edges;                    // the edges from parents, drawn after the path that grew them

  void warn(const std::string& what) {
    if (_warned.insert(what).second) _args.warn(what);
  }

  // --- numbers and lengths ----------------------------------------------------------------------

  static bool isUnit(const std::string& u) {
    static const std::set<std::string> units = {"pt", "cm", "mm", "in", "bp", "em", "ex", "pc", "sp", "dd", "cc", "mu", "px"};
    return units.count(u) > 0;
  }

  /** A length as TikZ reads one: `2cm`, `1.5`, `\w*2` once it is a number -- a bare number is centimetres. */
  Dimen dimen(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty()) return Dimen();
    // A number and a unit.
    std::size_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0 || s[i] == '.')) i++;
    if (i > 0 && i < s.size()) {
      const std::string unit = trim(s.substr(i));
      if (isUnit(unit)) return Units::getDimen(trim(s.substr(0, i)) + unit);
    }
    double v = 0;
    Expr e(s);
    if (e.value(v)) return {static_cast<float>(v), UnitType::cm};
    return Dimen();
  }

  bool number(const std::string& text, double& out) {
    Expr e(trim(text));
    return e.value(out);
  }

  // --- colours ----------------------------------------------------------------------------------

  bool colour(const std::string& text, color& out) {
    return ColorAtom::parseColor(trim(text), out);
  }

  // --- options ----------------------------------------------------------------------------------

  static std::string unbrace(const std::string& v) {
    const std::string t = trim(v);
    if (!t.empty() && t[0] == '{') {
      std::size_t at = 0;
      return trim(groupAt(t, at));
    }
    return t;
  }

  void applyOptions(Style& st, const std::string& list, bool node, int depth) {
    if (depth > 12) {
      warn("tikz: styles nest too deeply: left out");
      return;
    }
    // The options are split at commas, but a point such as `(1,2)` in `fit=(1,2)(3,4)` keeps its own.
    std::vector<std::string> pieces;
    {
      std::string held;
      for (const std::string& raw : splitTop(list, ',')) {
        held += held.empty() ? raw : "," + raw;
        if (std::count(held.begin(), held.end(), '(') <= std::count(held.begin(), held.end(), ')')) {
          pieces.push_back(held);
          held.clear();
        }
      }
      if (!held.empty()) pieces.push_back(held);
    }
    for (const std::string& raw : pieces) {
      const std::string item = trim(raw);
      if (item.empty()) continue;
      // The first `=` outside braces and brackets: `-{Stealth[length=4mm]}` is a key.
      std::size_t eq = std::string::npos;
      for (std::size_t k = 0, nest = 0; k < item.size() && eq == std::string::npos; k++) {
        if (item[k] == '{' || item[k] == '[') nest++;
        if ((item[k] == '}' || item[k] == ']') && nest > 0) nest--;
        if (item[k] == '=' && nest == 0) eq = k;
      }
      // A key with no value, or `key=value`; `/.style` keys have a slash before the dot.
      const std::string key = trim(item.substr(0, eq));
      const bool hasValue = eq != std::string::npos;
      option(st, key, hasValue ? unbrace(item.substr(eq + 1)) : std::string(), hasValue, node, depth);
    }
  }

  /** The arrow tip a name stands for: `>` and `<` are whatever `>=` says when the path is laid out. */
  bool tipOf(const std::string& name, Tip& tip) {
    const std::string n = trim(name);
    if (n == ">" || n == "<") {
      tip = Tip();
      tip.kind = Tip::Kind::to;
      tip.defaultTip = true;
      return true;
    }
    return tipByName(n, tip);
  }

  /** `->`, `<-`, `-stealth`, `{Stealth[length=3mm]}-{Latex}`: the tips at the two ends. */
  bool arrowSpec(Style& st, const std::string& key) {
    // Split at the first `-` outside braces.
    int brace = 0;
    std::size_t cut = std::string::npos;
    for (std::size_t i = 0; i < key.size(); i++) {
      if (key[i] == '{' || key[i] == '[') brace++;
      if (key[i] == '}' || key[i] == ']') brace--;
      if (key[i] == '-' && brace == 0) {
        cut = i;
        break;
      }
    }
    if (cut == std::string::npos) return false;
    const std::string left = trim(key.substr(0, cut)), right = trim(key.substr(cut + 1));
    Tip a, b;
    if (!left.empty() && !tipOf(left, a)) return false;
    if (!right.empty() && !tipOf(right, b)) return false;
    st.tipStart = left.empty() ? Tip() : a;
    st.tipEnd = right.empty() ? Tip() : b;
    return true;
  }

  Dist distOf(const std::string& value) {
    Dist d;
    const auto and_ = value.find(" and ");
    if (and_ != std::string::npos) {
      d.pair = true;
      d.y = dimen(value.substr(0, and_));
      d.x = dimen(value.substr(and_ + 5));
    } else {
      d.y = d.x = dimen(value);
    }
    return d;
  }

  void lineWidth(Style& st, double pt) { st.lineWidth = Dimen{static_cast<float>(pt), UnitType::pt}; }

  void dash(Style& st, std::initializer_list<const char*> parts) {
    std::vector<Dimen> d;
    for (const char* p : parts) {
      if (std::string(p) == "w") {
        d.push_back(st.lineWidth ? *st.lineWidth : Dimen{0.4f, UnitType::pt});
      } else {
        d.push_back(Units::getDimen(p));
      }
    }
    st.dash = d;
    st.dashPhase = Dimen{0.f, UnitType::pt};
  }

  void option(Style& st, const std::string& key, const std::string& value, bool hasValue, bool node, int depth) {
    // `name/.style=`: a style of the picture's own.
    const auto slash = key.find("/.");
    if (slash != std::string::npos) {
      const std::string name = trim(key.substr(0, slash)), what = key.substr(slash + 2);
      if (what == "style" || what == "append style") {
        std::string& to = _styles[name];
        to = what == "style" ? value : (to.empty() ? value : to + "," + value);
        return;
      }
      if (what == "default" || what == "estyle") return;
      warn("tikz: " + key + " is not supported: left out");
      return;
    }
    // A style by name.
    const auto defined = _styles.find(key);
    if (!hasValue && defined != _styles.end()) {
      applyOptions(st, defined->second, node, depth + 1);
      return;
    }
    if (hasValue && defined != _styles.end()) {
      std::string text = defined->second;
      for (std::size_t p = text.find("#1"); p != std::string::npos; p = text.find("#1", p + value.size())) {
        text.replace(p, 2, value);
      }
      applyOptions(st, text, node, depth + 1);
      return;
    }
    color c = 0;
    // The drawing options.
    if (key == "draw") {
      if (!hasValue) {
        st.draw.mode = Paint::Mode::current;
      } else if (value == "none") {
        st.draw.mode = Paint::Mode::none;
      } else if (colour(value, c)) {
        st.draw = {Paint::Mode::color, c};
      } else {
        warn("tikz: draw=" + value + " is not a colour: left out");
      }
    } else if (key == "fill") {
      if (!hasValue) {
        st.fill.mode = Paint::Mode::current;
      } else if (value == "none") {
        st.fill.mode = Paint::Mode::none;
      } else if (colour(value, c)) {
        st.fill = {Paint::Mode::color, c};
      } else {
        warn("tikz: fill=" + value + " is not a colour: left out");
      }
    } else if (key == "filldraw") {
      st.fill.mode = st.draw.mode = Paint::Mode::current;
    } else if (key == "color") {
      if (colour(value, c)) {
        st.current = c;
      } else {
        warn("tikz: color=" + value + " is not a colour: left out");
      }
    } else if (key == "text") {
      if (colour(value, c)) st.textColor = c;
    } else if (key == "line width") {
      const Dimen d = dimen(value);
      if (d.isValid() && d.val < 0) {
        warn("tikz: line width=" + value + " is negative: left out");   // grid would not draw it at all
      } else if (d.isValid()) {
        st.lineWidth = d.unit == UnitType::cm && trim(value).find("cm") == std::string::npos ? Dimen{d.val, UnitType::pt} : d;
      }
    } else if (key == "ultra thin") {
      lineWidth(st, 0.1);
    } else if (key == "very thin") {
      lineWidth(st, 0.2);
    } else if (key == "thin") {
      lineWidth(st, 0.4);
    } else if (key == "semithick") {
      lineWidth(st, 0.6);
    } else if (key == "thick") {
      lineWidth(st, 0.8);
    } else if (key == "very thick") {
      lineWidth(st, 1.2);
    } else if (key == "ultra thick") {
      lineWidth(st, 1.6);
    } else if (key == "solid") {
      st.dash = std::vector<Dimen>();
    } else if (key == "dashed") {
      dash(st, {"3pt", "3pt"});
    } else if (key == "dotted") {
      dash(st, {"w", "2pt"});
    } else if (key == "densely dashed") {
      dash(st, {"3pt", "2pt"});
    } else if (key == "loosely dashed") {
      dash(st, {"3pt", "6pt"});
    } else if (key == "densely dotted") {
      dash(st, {"w", "1pt"});
    } else if (key == "loosely dotted") {
      dash(st, {"w", "4pt"});
    } else if (key == "dash pattern") {
      // on 4pt off 1pt on ...
      std::vector<Dimen> d;
      std::string v = value;
      for (std::size_t p = 0; p < v.size();) {
        while (p < v.size() && std::isspace(static_cast<unsigned char>(v[p])) != 0) p++;
        std::string word;
        while (p < v.size() && std::isspace(static_cast<unsigned char>(v[p])) == 0) word += v[p++];
        if (word == "on" || word == "off" || word.empty()) continue;
        d.push_back(dimen(word));
      }
      st.dash = d;
    } else if (key == "dash phase") {
      st.dashPhase = dimen(value);
    } else if (key == "line cap") {
      st.cap = value == "round" ? CAP_ROUND : value == "rect" ? CAP_SQUARE : CAP_BUTT;
    } else if (key == "line join") {
      st.join = value == "round" ? JOIN_ROUND : value == "bevel" ? JOIN_BEVEL : JOIN_MITER;
    } else if (key == "opacity" || key == "draw opacity" || key == "fill opacity" || key == "text opacity") {
      double v = 1;
      if (number(value, v)) {
        if (key != "fill opacity" && key != "text opacity") st.drawOpacity = static_cast<float>(v);
        if (key != "draw opacity" && key != "text opacity") st.fillOpacity = static_cast<float>(v);
        if (key == "text opacity" || key == "opacity") st.textOpacity = static_cast<float>(v);
      }
    } else if (key == "rounded corners") {
      st.rounded = hasValue ? dimen(value) : Dimen{4.f, UnitType::pt};
      if (hasValue && st.rounded->unit == UnitType::cm && trim(value).find("cm") == std::string::npos) {
        st.rounded = Dimen{st.rounded->val, UnitType::pt};
      }
    } else if (key == "sharp corners") {
      st.rounded = Dimen{0.f, UnitType::pt};
    } else if (key == "scale" || key == "xscale" || key == "yscale" || key == "rotate") {
      double v = 1;
      if (number(value, v)) {
        XStep s;
        s.kind = key == "scale" ? XStep::Kind::scale : key == "xscale" ? XStep::Kind::xscale
                 : key == "yscale" ? XStep::Kind::yscale : XStep::Kind::rotate;
        s.v = static_cast<float>(v);
        st.transform.push_back(s);
      }
    } else if (key == "shift" && hasValue) {
      // `shift={(1,2)}` (the braces were taken off), or `shift=(1,2)`: a move of everything after it.
      const std::string v = trim(value);
      std::size_t at = 0;
      Coord c;
      if (!v.empty() && v[0] == '(') c = coord(parenAt(v, at));
      if (c.kind == Coord::Kind::xy && c.x.isValid() && c.y.isValid()) {
        XStep s;
        s.kind = XStep::Kind::shift;
        s.dx = c.x;
        s.dy = c.y;
        st.transform.push_back(s);
      } else {
        warn("tikz: shift=" + value + " is not a point (x,y): left out");
      }
    } else if (key == "xshift" || key == "yshift") {
      XStep s;
      s.kind = key == "xshift" ? XStep::Kind::xshift : XStep::Kind::yshift;
      s.dx = dimen(value);
      st.transform.push_back(s);
    } else if (key == ">") {
      st.defaultTip = value;
    } else if (key == "node distance") {
      st.nodeDist = distOf(value);
    } else if (key == "every node" || key == "every path" || key == "every picture") {
      // Set through `every node/.style`; a bare one has nothing to say.
    } else if (key == "step" || key == "xstep" || key == "ystep") {
      const Dimen d = dimen(value);
      if (key != "ystep") st.xstep = d;
      if (key != "xstep") st.ystep = d;
      if (key == "step") st.step = d;
    } else if (key == "shorten >" || key == "shorten <") {
      (key == "shorten >" ? st.shortenEnd : st.shortenStart) = dimen(value);
    } else if (key == "bend left" || key == "bend right") {
      double a = 30;
      if (hasValue) number(value, a);
      st.to.bend = static_cast<float>(key == "bend left" ? a : -a);
    } else if (key == "out" || key == "in") {
      double a = 0;
      if (number(value, a)) (key == "out" ? st.to.out : st.to.in) = static_cast<float>(a);
    } else if (key == "looseness") {
      double v = 1;
      if (number(value, v)) st.to.looseness = static_cast<float>(v);
    } else if (key == "min distance") {
      st.to.minDistance = dimen(value);
    } else if (key == "loop above" || key == "loop below" || key == "loop left" || key == "loop right") {
      // pgf's: above is out=105 in=75, and looseness 8, 5mm at least, with an arrow and a 1pt shorter end.
      const bool above = key == "loop above", below = key == "loop below", lefty = key == "loop left";
      st.to.out = static_cast<float>(above ? 105 : below ? 285 : lefty ? 195 : 15);
      st.to.in = static_cast<float>(above ? 75 : below ? 255 : lefty ? 165 : -15);
      st.to.loop = true;
      st.to.looseness = 8.f;
      st.to.minDistance = Dimen{5.f, UnitType::mm};
      Tip arrow;
      arrow.kind = Tip::Kind::to;
      arrow.defaultTip = true;
      st.tipEnd = arrow;
      st.shortenEnd = Dimen{1.f, UnitType::pt};
    } else if ((node || key == "label distance" || key == "pin distance" || key == "pin edge" || key == "every label" || key == "every pin") && applyNodeOption(st, key, value, hasValue, c)) {
      // read
    } else if (key == "decorate") {
      st.decorate = !hasValue || value != "false";
    } else if (key == "decoration" && hasValue) {
      decorationOf(st, value);
    } else if (key == "level distance" && hasValue) {
      st.levelDistance = dimen(value);
    } else if (key == "sibling distance" && hasValue) {
      st.siblingDistance = dimen(value);
    } else if ((key == "grow" || key == "grow'") && hasValue) {
      growOf(st, value, key == "grow'");
    } else if (key == "missing") {
      st.missing = !hasValue || value != "false";
    } else if (key == "edge from parent path" && hasValue) {
      st.edgePath = value;
    } else if ((key == "parent anchor" || key == "child anchor") && hasValue) {
      (key == "parent anchor" ? st.parentAnchor : st.childAnchor) = trim(value) == "border" ? std::string() : trim(value);
    } else if (key == "on background layer" || key == "on main layer" || key == "on foreground layer") {
      st.layer = key == "on background layer" ? -1 : key == "on main layer" ? 0 : 1;
    } else if (key == "domain" && hasValue) {
      st.domain = value;
    } else if (key == "samples" && hasValue) {
      double v = 0;
      if (number(value, v)) st.samples = static_cast<int>(std::min(std::max(v, 2.0), kMaxPoints));
    } else if (key == "samples at" && hasValue) {
      st.samplesAt = value;
    } else if (key == "smooth" || key == "smooth cycle" || key == "sharp plot" || key == "sharp cycle" || key == "const plot" ||
               key == "xcomb" || key == "ycomb") {
      st.plotHandler = key == "sharp plot" ? std::string() : key;
    } else if (key == "tension" && hasValue) {
      double v = 0;
      if (number(value, v)) st.tension = v;
    } else if (key == "variable" && hasValue) {
      st.plotVariable = trim(value);
    } else if (key == "range" || key == "xrange" || key == "yrange") {
      // gnuplot's clipping of `plot function`, which is not read
    } else if (key == "cycle") {
    } else if (key == "clip") {
      st.clipPath = !hasValue || trim(value) != "false";
    } else if (!hasValue && !key.empty() && arrowSpec(st, key)) {
      // read
    } else if (!hasValue && colour(key, c) && ColorAtom::hasName(key.substr(0, key.find('!')))) {
      st.current = c;
    } else if (!node && applyNodeOption(st, key, value, hasValue, c)) {
      // A path option that is a node's, `above` on a node on it, say.
    } else {
      warn("tikz: option " + key + (hasValue ? "=" + value : std::string()) + " is not supported: left out");
    }
  }

  /** The options of a node alone; false when `key` is not one. */
  bool applyNodeOption(Style& st, const std::string& key, const std::string& value, bool hasValue, color&) {
    // The shapes that are drawn; any other is said, with its name, where it is asked for.
    static const std::set<std::string> shapes = {"rectangle", "circle", "ellipse", "diamond", "coordinate", "rounded rectangle",
                                                 "regular polygon", "star", "isosceles triangle"};
    static const std::set<std::string> known = {"trapezium", "cylinder", "semicircle", "kite", "dart", "circular sector",
                                                "cross out", "strike out", "chamfered rectangle", "rectangle split"};
    if ((key == "regular polygon sides" || key == "star points") && hasValue) {
      double v = 0;
      if (number(value, v)) (key == "star points" ? st.starPoints : st.polySides) = static_cast<int>(std::min(std::max(v, key == "star points" ? 2.0 : 3.0), 1000.0));
      return true;
    }
    if (key == "star point ratio" && hasValue) {
      double v = 0;
      if (number(value, v) && v > 0) st.starRatio = v;
      st.starByHeight = false;
      return true;
    }
    if (key == "star point height" && hasValue) {
      st.starHeight = dimen(value);
      st.starByHeight = true;
      return true;
    }
    if (key == "shape border rotate" && hasValue) {
      double v = 0;
      if (number(value, v)) st.borderRotate = static_cast<float>(v);
      return true;
    }
    if (key == "isosceles triangle apex angle" && hasValue) {
      double v = 0;
      if (number(value, v) && v > 0 && v < 180) st.apexAngle = static_cast<float>(v);
      return true;
    }
    if (key == "shape border uses incircle") {
      st.borderIncircle = !hasValue || trim(value) != "false";
      return true;
    }
    if (shapes.count(key) > 0 && !hasValue) {
      st.shape = key;
      return true;
    }
    if (known.count(key) > 0 && !hasValue) {
      warn("tikz: the shape " + key + " is not drawn yet: a rectangle is");
      return true;
    }
    if (key == "shape") {
      if (shapes.count(value) > 0) {
        st.shape = value;
      } else {
        warn("tikz: the shape " + value + " is not drawn yet: a rectangle is");
      }
      return true;
    }
    if (key == "matrix of nodes" || key == "matrix of math nodes") {
      st.matrixKind = key == "matrix of nodes" ? 1 : 2;
      return true;
    }
    if ((key == "row sep" || key == "column sep") && hasValue) {
      // `1cm`, or `{1cm, between origins}`: a length, and how it is measured.
      const bool row = key == "row sep";
      for (const std::string& part : splitTop(value, ',')) {
        const std::string p = trim(part);
        if (p == "between origins" || p == "between borders") {
          (row ? st.rowBetweenOrigins : st.columnBetweenOrigins) = p == "between origins";
        } else if (!p.empty()) {
          (row ? st.rowSep : st.columnSep) = dimen(p);
        }
      }
      return true;
    }
    if (key == "nodes" && hasValue) {
      st.nodesStyle = value;
      return true;
    }
    if (key == "cells" && hasValue) {
      st.cellsStyle = value;
      return true;
    }
    if (key == "nodes in empty cells") {
      st.emptyNodes = !hasValue || value != "false";
      return true;
    }
    if (key == "matrix anchor" && hasValue) {
      st.anchor = trim(value);
      return true;
    }
    if (key == "left delimiter" || key == "right delimiter" || key == "above delimiter" || key == "below delimiter") {
      warn("tikz: " + key + " is not drawn: left out");
      return true;
    }
    if (key == "fit" && hasValue) {
      // `fit=(a)(b)`: the points, one after the other.
      st.fit.clear();
      std::size_t i = 0;
      for (;;) {
        skipSpace(value, i);
        if (i >= value.size() || value[i] != '(') break;
        st.fit.push_back(coord(parenAt(value, i)));
      }
      return true;
    }
    if ((key == "label" || key == "pin") && hasValue) {
      st.labelSpecs.emplace_back(key == "pin", value);
      return true;
    }
    if ((key == "label distance" || key == "pin distance") && hasValue) {
      (key == "label distance" ? st.labelDistance : st.pinDistance) = dimen(value);
      return true;
    }
    if (key == "pin edge" && hasValue) {
      st.pinEdge = value;
      return true;
    }
    if (key == "rounded rectangle arc length" && hasValue) {
      // An end is an arc of that many degrees over the whole height: none at 0 and a full circle at 360, which have no size.
      const double a = std::atof(value.c_str());
      if (a > 0.0 && a < 360.0) {
        st.arcLength = static_cast<float>(a);
      } else {
        warn("tikz: rounded rectangle arc length=" + value + " is not between 0 and 360 degrees: left out");
      }
      return true;
    }
    if ((key == "aspect" || key == "shape aspect") && hasValue) {
      st.aspect = static_cast<float>(std::atof(value.c_str()));
      return true;
    }
    if (key == "inner sep") {
      st.innerX = st.innerY = dimen(value);
      return true;
    }
    if (key == "inner xsep" || key == "inner ysep") {
      (key == "inner xsep" ? st.innerX : st.innerY) = dimen(value);
      return true;
    }
    if (key == "outer sep") {
      st.outerSep = dimen(value);
      return true;
    }
    if (key == "minimum width" || key == "minimum height" || key == "minimum size") {
      const Dimen d = dimen(value);
      if (key != "minimum height") st.minWidth = d;
      if (key != "minimum width") st.minHeight = d;
      return true;
    }
    if (key == "text width") {
      st.textWidth = dimen(value);
      return true;
    }
    if (key == "align") {
      st.align = value == "left" ? 'l' : value == "right" ? 'r' : value == "justify" ? 'j' : 'c';
      return true;
    }
    if (key == "anchor") {
      st.anchor = value;
      return true;
    }
    if (key == "font") {
      st.font = value;
      return true;
    }
    if (key == "sloped") {
      st.sloped = true;
      return true;
    }
    if (key == "auto") {
      st.autoSide = true;
      return true;
    }
    if (key == "swap") {
      st.swap = true;
      return true;
    }
    static const std::map<std::string, double> positions = {
      {"midway", 0.5}, {"near start", 0.25}, {"near end", 0.75}, {"very near start", 0.125}, {"very near end", 0.875},
      {"at start", 0.0}, {"at end", 1.0}, {"pos", 0.5}};
    const auto p = positions.find(key);
    if (p != positions.end()) {
      double v = p->second;
      if (key == "pos") number(value, v);
      st.pos = static_cast<float>(v);
      return true;
    }
    static const std::set<std::string> places = {"above", "below", "left", "right", "above left", "above right", "below left", "below right"};
    if (places.count(key) > 0) {
      st.placement = key;
      // `above=of a`, `right=2cm of a`, `above=1mm`.
      if (hasValue) {
        std::string v = value;
        const auto of = v.rfind("of ");
        if (of != std::string::npos && (of == 0 || std::isspace(static_cast<unsigned char>(v[of - 1])) != 0)) {
          st.placeOf = trim(v.substr(of + 3));
          v = trim(v.substr(0, of));
        }
        if (!v.empty()) st.placeDist = distOf(v);
      }
      return true;
    }
    const auto old = key.rfind(" of");
    if (old != std::string::npos && old + 3 == key.size() && hasValue && places.count(key.substr(0, old)) > 0) {
      st.placement = key.substr(0, old);
      st.placeOf = value;
      st.placeCentres = true;
      return true;
    }
    return false;
  }

  // --- coordinates ------------------------------------------------------------------------------

  /** The text between a coordinate's brackets as a Coord. */
  Coord coord(const std::string& inside) {
    Coord c;
    const std::string s = trim(inside);
    if (!s.empty() && s[0] == '$') {
      c.kind = Coord::Kind::calc;
      c.calc = calcOf(s);
      return c;
    }
    // `(a |- b)` and `(a -| b)`: the corner the vertical line from one makes with the horizontal line from the other.
    for (const char* op : {"|-", "-|"}) {
      const auto cut = s.find(op);
      if (cut != std::string::npos && cut > 0) {
        c.kind = Coord::Kind::cross;
        c.first = std::make_shared<Coord>(coord(s.substr(0, cut)));
        c.second = std::make_shared<Coord>(coord(s.substr(cut + 2)));
        c.xFromFirst = std::string(op) == "|-";
        return c;
      }
    }
    // `a:r`, an angle and a radius; not `cs:`.
    const auto colon = s.find(':');
    if (colon != std::string::npos && s.find(',') == std::string::npos && s.compare(0, 3, "cs:") != 0) {
      double a = 0;
      if (number(s.substr(0, colon), a)) {
        c.kind = Coord::Kind::polar;
        c.angle = static_cast<float>(a);
        const std::string rest = trim(s.substr(colon + 1));
        const auto and_ = rest.find(" and ");
        c.r = dimen(and_ == std::string::npos ? rest : rest.substr(0, and_));
        if (and_ != std::string::npos) c.r2 = dimen(rest.substr(and_ + 5));
        return c;
      }
    }
    const auto parts = splitTop(s, ',');
    if (parts.size() == 2) {
      const Dimen x = dimen(parts[0]), y = dimen(parts[1]);
      if (x.isValid() && y.isValid()) {
        c.kind = Coord::Kind::xy;
        c.x = x;
        c.y = y;
        return c;
      }
    }
    // A node, and perhaps an anchor.
    c.kind = Coord::Kind::node;
    const auto dot = s.find('.');
    c.name = trim(dot == std::string::npos ? s : s.substr(0, dot));
    if (dot != std::string::npos) c.anchor = trim(s.substr(dot + 1));
    refer(c.name);
    return c;
  }

  /** TikZ needs a node to exist before a coordinate names it; the layout leaves a path out when it does not, and
   *  cannot say so, so it is said here, once. A name `m-1-2` is a cell of the matrix `m`, and `p-1` a child of `p`. */
  void refer(const std::string& name) {
    if (name.empty() || name.compare(0, 8, "current ") == 0 || _defined.count(name) > 0) return;
    for (const std::string& d : _defined) {
      if (name.size() > d.size() && name.compare(0, d.size(), d) == 0 && name[d.size()] == '-') return;
    }
    warn("tikz: `(" + name + ")' is not a node drawn before it or a coordinate this reads: the path is left out");
  }

  /** The text of `($ ... $)` as tikzlibrarycalc reads it: terms joined by `+` and `-`, each an optional factor
   *  and `*`, a coordinate, and `!` steps (`!0.3!(b)`, `!1cm!(b)`, `!(p)!(b)`, a target turned by `!30:(b)`). */
  std::shared_ptr<CalcExpr> calcOf(const std::string& source) {
    auto out = std::make_shared<CalcExpr>();
    std::string e = trim(source);
    if (!e.empty() && e.front() == '$') e.erase(0, 1);
    if (!e.empty() && e.back() == '$') e.pop_back();
    const auto bad = [&](const std::string& why) {
      warn("tikz: in a calculation, " + why + ": the rest of it is left out");
    };
    // A point, and the angle in front of it if it has one, from `i`.
    const auto target = [&](std::size_t& i, CalcStep& step) {
      skipSpace(e, i);
      const std::size_t paren = e.find('(', i), colon = e.find(':', i);
      if (colon != std::string::npos && paren != std::string::npos && colon < paren) {
        double a = 0;
        if (!number(e.substr(i, colon - i), a)) return false;
        step.rotated = true;
        step.angle = static_cast<float>(a);
        i = colon + 1;
        skipSpace(e, i);
      }
      if (i >= e.size() || e[i] != '(') return false;
      std::size_t at = i;
      step.to = coord(parenAt(e, at));
      i = at;
      return true;
    };
    std::size_t i = 0;
    for (;;) {
      skipSpace(e, i);
      if (i >= e.size()) break;
      CalcTerm term;
      if (e[i] == '+') {
        i++;
      } else if (e[i] == '-') {
        term.factor = -1;
        i++;
      }
      skipSpace(e, i);
      if (i < e.size() && e[i] != '(') {
        // The factor, up to the `*` that is followed by a point.
        std::size_t star = std::string::npos;
        int depth = 0;
        for (std::size_t k = i; k < e.size() && star == std::string::npos; k++) {
          if (e[k] == '(') depth++;
          if (e[k] == ')') depth--;
          if (e[k] != '*' || depth != 0) continue;
          std::size_t j = k + 1;
          skipSpace(e, j);
          if (j < e.size() && e[j] == '(') star = k;
        }
        double f = 1;
        if (star == std::string::npos || !number(e.substr(i, star - i), f)) {
          bad("a factor must be a number and `*` before a point");
          break;
        }
        term.factor *= f;
        i = star + 1;
        skipSpace(e, i);
      }
      if (i >= e.size() || e[i] != '(') {
        bad("a point is expected");
        break;
      }
      std::size_t at = i;
      term.first = coord(parenAt(e, at));
      i = at;
      bool ok = true;
      for (;;) {
        skipSpace(e, i);
        if (i >= e.size() || e[i] != '!') break;
        i++;
        skipSpace(e, i);
        CalcStep step;
        if (i < e.size() && e[i] == '(') {
          step.kind = CalcStep::Kind::project;
          std::size_t from = i;
          step.via = coord(parenAt(e, from));
          i = from;
          skipSpace(e, i);
          if (i >= e.size() || e[i] != '!') {
            ok = false;
            break;
          }
          i++;
        } else {
          const std::size_t bang = e.find('!', i);
          if (bang == std::string::npos) {
            ok = false;
            break;
          }
          const std::string amount = trim(e.substr(i, bang - i));
          i = bang + 1;
          // A length (`1cm`) walks along the line; a number is how far of it.
          std::size_t k = 0;
          if (k < amount.size() && (amount[k] == '+' || amount[k] == '-')) k++;
          while (k < amount.size() && (std::isdigit(static_cast<unsigned char>(amount[k])) != 0 || amount[k] == '.')) k++;
          if (k > 0 && k < amount.size() && isUnit(trim(amount.substr(k)))) {
            step.kind = CalcStep::Kind::distance;
            step.distance = dimen(amount);
          } else if (!number(amount, step.fraction)) {
            ok = false;
            break;
          }
        }
        if (!target(i, step)) {
          ok = false;
          break;
        }
        term.steps.push_back(std::move(step));
      }
      if (!ok) {
        bad("a `!` is not followed by a number, a length or a point, and a point");
        break;
      }
      out->terms.push_back(std::move(term));
    }
    return out;
  }

  /** `decoration={brace, amplitude=10pt, aspect=0.5, mirror, raise=4pt}`: the brace is drawn; any other name is said. */
  void decorationOf(Style& st, const std::string& value) {
    for (const std::string& part : splitTop(value, ',')) {
      const std::string item = trim(part);
      const std::size_t eq = item.find('=');
      const std::string key = trim(item.substr(0, eq)), v = eq == std::string::npos ? "" : trim(item.substr(eq + 1));
      if (item.empty()) continue;
      if (eq == std::string::npos && key != "mirror") {
        if (key == "brace") {
          st.decoration = key;
        } else {
          warn("tikz: the decoration " + key + " is not drawn: the path is drawn as it stands");
        }
      } else if (key == "amplitude") {
        st.amplitude = dimen(v);
      } else if (key == "raise") {
        st.raise = dimen(v);
      } else if (key == "aspect") {
        double a = 0.5;
        if (number(v, a)) st.braceAspect = static_cast<float>(a);
      } else if (key == "mirror") {
        st.mirror = eq == std::string::npos || v != "false";
      } else {
        warn("tikz: decoration option " + key + " is not supported: left out");
      }
    }
  }

  // --- trees ------------------------------------------------------------------------------------

  /** `grow=down`, `grow=right`, `grow=30`, `grow=north east`: the way a tree grows. */
  void growOf(Style& st, const std::string& value, bool swap) {
    static const std::map<std::string, float> names = {
      {"down", -90.f},       {"up", 90.f},         {"left", 180.f},        {"right", 0.f},
      {"south", -90.f},      {"north", 90.f},      {"west", 180.f},        {"east", 0.f},
      {"north east", 45.f},  {"north west", 135.f}, {"south east", -45.f},  {"south west", -135.f}};
    const std::string v = trim(value);
    double angle = 0;
    const auto it = names.find(v);
    if (it != names.end()) {
      angle = it->second;
    } else if (!number(v, angle)) {
      warn("tikz: grow=" + v + " is not a direction: left out");
      return;
    }
    st.growAngle = static_cast<float>(angle);
    st.growSwap = swap;
    st.growLevel = _treeLevel;
  }

  /** The index of `needle` as a word of `text` outside every group, bracket and parenthesis, from `from`. */
  static std::size_t findTop(const std::string& text, const std::string& needle, std::size_t from = 0) {
    int depth = 0;
    for (std::size_t k = from; k < text.size(); k++) {
      const char c = text[k];
      if (c == '{' || c == '[' || c == '(') depth++;
      if (c == '}' || c == ']' || c == ')') depth--;
      if (depth == 0 && text.compare(k, needle.size(), needle) == 0 &&
          (k == 0 || std::isalpha(static_cast<unsigned char>(text[k - 1])) == 0) &&
          (k + needle.size() >= text.size() || std::isalpha(static_cast<unsigned char>(text[k + needle.size()])) == 0)) {
        return k;
      }
    }
    return std::string::npos;
  }

  /** The `child` items that follow a node, with the options between them: the node's children, as TikZ grows them
   *  (`tikz.code.tex`, `\tikz@children`). Each child's body is a node and what follows it -- its own children, its
   *  `edge from parent` -- and each edge becomes a path of its own, drawn after the one the tree is on. */
  void readTree(Node& parent, const Style& scope, int level, const std::string& t, std::size_t& i) {
    struct Source {
      std::string options, body;
    };
    const std::size_t start = i;
    const int was = _treeLevel;
    Style ctx = scope;
    std::vector<Source> kids;
    const auto atWord = [&](std::size_t k, const char* w) {
      const std::size_t n = std::string(w).size();
      return t.compare(k, n, w) == 0 && (k + n >= t.size() || std::isalpha(static_cast<unsigned char>(t[k + n])) == 0);
    };
    for (;;) {
      const std::size_t save = i;
      skipSpace(t, i);
      if (i < t.size() && t[i] == '[') {
        // Options for the rest of the path, if a child comes after them.
        std::size_t at = i;
        const std::string o = groupAt(t, at);
        std::size_t k = at;
        skipSpace(t, k);
        if (k < t.size() && (t[k] == '[' || atWord(k, "child"))) {
          _treeLevel = level - 1;
          applyOptions(ctx, o, false, 0);
          i = at;
          continue;
        }
        i = save;
        break;
      }
      if (i < t.size() && atWord(i, "child")) {
        i += 5;
        skipSpace(t, i);
        std::string options;
        if (i < t.size() && t[i] == '[') {
          std::size_t at = i;
          options = groupAt(t, at);
          i = at;
        }
        skipSpace(t, i);
        if (atWord(i, "foreach")) {
          // child foreach \x in {a,b} {body}: one child for each value.
          i += 7;
          skipSpace(t, i);
          std::vector<std::string> vars;
          while (i < t.size() && t[i] == '\\') {
            i++;
            vars.push_back(word(t, i));
            skipSpace(t, i);
            if (i < t.size() && t[i] == '/') i++;
            skipSpace(t, i);
          }
          if (t.compare(i, 2, "in") == 0 && !vars.empty()) {
            i += 2;
            skipSpace(t, i);
            std::string list, body;
            if (i < t.size() && t[i] == '{') {
              std::size_t at = i;
              list = groupAt(t, at);
              i = at;
            }
            skipSpace(t, i);
            if (i < t.size() && t[i] == '{') {
              std::size_t at = i;
              body = groupAt(t, at);
              i = at;
            }
            for (const std::vector<std::string>& values : foreachValues(list, vars.size())) {
              std::string one = body;
              for (std::size_t v = 0; v < vars.size(); v++) {
                one = replaceControl(one, vars[v], v < values.size() ? values[v] : "");
              }
              kids.push_back({options, one});
            }
          } else {
            warn("tikz: child foreach is not in the form `\\x in {list}': left out");
          }
          continue;
        }
        std::string body;
        if (i < t.size() && t[i] == '{') {
          std::size_t at = i;
          body = groupAt(t, at);
          i = at;
        }
        kids.push_back({options, body});
        continue;
      }
      i = save;
      break;
    }
    if (kids.empty()) {
      i = start;
      _treeLevel = was;
      return;
    }
    // The styles of the level, then each child's own.
    _treeLevel = level;
    applyStyleNamed(ctx, "level " + std::to_string(level));
    if (_styles.count("level") > 0) option(ctx, "level", std::to_string(level), true, false, 0);
    if (parent.name.empty()) parent.name = "tikz@tree" + std::to_string(++_treeNames);
    _defined.insert(parent.name);
    const int count = static_cast<int>(kids.size());
    for (int j = 0; j < count; j++) {
      Child c;
      c.level = level;
      c.style = ctx;
      applyStyleNamed(c.style, "every child");
      applyOptions(c.style, kids[static_cast<std::size_t>(j)].options, false, 0);
      c.missing = c.style.missing;
      c.style.missing = false;
      if (!c.missing) readChild(c, parent.name, j + 1, kids[static_cast<std::size_t>(j)].body);
      parent.children.push_back(std::move(c));
    }
    _treeLevel = was;
  }

  /** One child's body: `node ... child ... edge from parent ...`; a body with no node is a point. */
  void readChild(Child& c, const std::string& parentName, int index, const std::string& source) {
    // `edge from parent` ends what the node and its children are, and starts the edge's own options and labels.
    std::string body = source, edgeText;
    const std::size_t edgeAt = findTop(source, "edge");
    if (edgeAt != std::string::npos) {
      std::size_t k = edgeAt + 4;
      skipSpace(source, k);
      if (source.compare(k, 4, "from") == 0) {
        k += 4;
        skipSpace(source, k);
        if (source.compare(k, 6, "parent") == 0) {
          body = source.substr(0, edgeAt);
          edgeText = source.substr(k + 6);
          // Children written after the edge's labels belong to the node as well.
          const std::size_t childAt = findTop(edgeText, "child");
          if (childAt != std::string::npos) {
            body += " " + edgeText.substr(childAt);
            edgeText = edgeText.substr(0, childAt);
          }
        }
      }
    }
    std::size_t b = 0;
    skipSpace(body, b);
    const bool isNode = body.compare(b, 4, "node") == 0, isCoordinate = body.compare(b, 10, "coordinate") == 0;
    Node cn;
    if (isNode || isCoordinate) {
      b += isNode ? 4 : 10;
      _nodeExtra = "every child node";
      cn = nodeOf(isCoordinate, body, b, c.style, false, c.style);
      _nodeExtra.clear();
    } else {
      cn.style = c.style;
      cn.style.shape = "coordinate";
    }
    if (cn.name.empty()) cn.name = parentName + "-" + std::to_string(index);
    _defined.insert(cn.name);
    readTree(cn, c.style, c.level + 1, body, b);
    // The edge: a path from the parent to the child, with labels.
    PathItem e;
    e.style = c.style;
    e.style.draw = Paint();
    e.style.fill = Paint();
    applyStyleNamed(e.style, "edge from parent");
    std::size_t k = 0;
    skipSpace(edgeText, k);
    if (k < edgeText.size() && edgeText[k] == '[') {
      std::size_t at = k;
      applyOptions(e.style, groupAt(edgeText, at), false, 0);
      k = at;
    }
    std::string path = e.style.edgePath.empty() ? "(\\tikzparentnode\\tikzparentanchor) -- (\\tikzchildnode\\tikzchildanchor)"
                                                 : e.style.edgePath;
    path = replaceControl(path, "tikzparentanchor", e.style.parentAnchor.empty() ? "" : "." + e.style.parentAnchor);
    path = replaceControl(path, "tikzchildanchor", e.style.childAnchor.empty() ? "" : "." + e.style.childAnchor);
    path = replaceControl(path, "tikzparentnode", parentName);
    path = replaceControl(path, "tikzchildnode", cn.name);
    pathOps(e, path + " " + edgeText.substr(k), 0);
    // A node of an edge from the parent is half way along it unless it says otherwise (pdfTeX), where a node after the last point
    // of any other path is at that point.
    for (Node& label : e.nodes) {
      if (!label.posGiven) {
        label.posGiven = true;
        label.pos = 0.5f;
      }
    }
    _edges.push_back(std::move(e));
    c.node = std::move(cn);
  }

  /** The edges the statement just read made, now that its own path is in the picture. */
  void flushEdges() {
    for (PathItem& e : _edges) _pic.paths.push_back(std::move(e));
    _edges.clear();
  }

  // --- statements -------------------------------------------------------------------------------

  static void skipSpace(const std::string& t, std::size_t& i) {
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i])) != 0) i++;
  }

  static std::string word(const std::string& t, std::size_t& i) {
    std::string w;
    while (i < t.size() && std::isalpha(static_cast<unsigned char>(t[i])) != 0) w += t[i++];
    return w;
  }

  /** The index of the `;` that ends the statement at `i`, outside every bracket, or the end. */
  static std::size_t statementEnd(const std::string& t, std::size_t i) {
    int brace = 0, bracket = 0, paren = 0;
    for (; i < t.size(); i++) {
      const char c = t[i];
      if (c == '\\' && i + 1 < t.size()) {
        i++;
        continue;
      }
      if (c == '{') brace++;
      if (c == '}') brace--;
      if (c == '[') bracket++;
      if (c == ']') bracket--;
      if (c == '(') paren++;
      if (c == ')') paren--;
      if (c == ';' && brace <= 0 && bracket <= 0 && paren <= 0) return i;
    }
    return t.size();
  }

  void run(const std::string& text, const Style& outside, int depth) {
    if (depth > 24) {
      warn("tikz: scopes nest too deeply: left out");
      return;
    }
    // What the statements here start from: the scope's style, and the clip paths they add, which last to its end.
    Style scope = outside;
    std::size_t i = 0;
    while (true) {
      skipSpace(text, i);
      if (i >= text.size()) return;
      if (text[i] == ';') {
        i++;
        continue;
      }
      if (text[i] == '{') {
        // A group around statements: a scope without options.
        std::size_t at = i;
        const std::string inner = groupAt(text, at);
        run(inner, scope, depth + 1);
        i = at;
        continue;
      }
      if (text[i] != '\\') {
        warn("tikz: `" + text.substr(i, 12) + "' is not a statement: left out");
        i = statementEnd(text, i) + 1;
        continue;
      }
      i++;
      const std::string cmd = word(text, i);
      if (cmd == "draw" || cmd == "path" || cmd == "fill" || cmd == "filldraw" || cmd == "node" || cmd == "coordinate" || cmd == "matrix" ||
          cmd == "clip" || cmd == "shade" || cmd == "shadedraw" || cmd == "useasboundingbox") {
        const std::size_t end = statementEnd(text, i);
        const int clip = statement(cmd, text.substr(i, end - i), scope);
        if (clip >= 0) scope.clips.push_back(clip);
        i = end + 1;
      } else if (cmd == "begin") {
        skipSpace(text, i);
        std::size_t at = i;
        const std::string env = trim(groupAt(text, at));
        i = at;
        // The matching \end{env}, nested ones counted.
        int level = 1;
        std::size_t j = i;
        std::size_t close = std::string::npos;
        const std::string beginTag = "\\begin{" + env + "}", endTag = "\\end{" + env + "}";
        while (j < text.size()) {
          const auto b = text.find(beginTag, j), e = text.find(endTag, j);
          if (e == std::string::npos) break;
          if (b != std::string::npos && b < e) {
            level++;
            j = b + beginTag.size();
          } else if (--level == 0) {
            close = e;
            break;
          } else {
            j = e + endTag.size();
          }
        }
        if (close == std::string::npos) {
          warn("tikz: \\begin{" + env + "} is not closed: the rest is left out");
          return;
        }
        std::string inner = text.substr(i, close - i);
        i = close + endTag.size();
        if (env == "scope") {
          Style child = scope;
          const std::string options = listPeelOptional(inner);
          // Styles a scope defines (in its options, or with \tikzset in it) end with it, as a TeX group's work does:
          // `every node/.style` in a scope's options was still in force after it.
          const auto savedStyles = _styles;
          const auto savedAppended = _appended;
          applyOptions(child, options, false, 0);
          run(inner, child, depth + 1);
          _styles = savedStyles;
          _appended = savedAppended;
        } else if (env == "pgfonlayer") {
          // `\begin{pgfonlayer}{background}`: the paths in it are drawn on that layer.
          std::size_t at = 0;
          skipSpace(inner, at);
          const std::string name = trim(groupAt(inner, at));
          Style child = scope;
          if (name == "background") {
            child.layer = -1;
          } else if (name == "main") {
            child.layer = 0;
          } else if (name == "foreground") {
            child.layer = 1;
          } else {
            warn("tikz: the layer " + name + " is not declared here: the main layer is used");
          }
          run(inner.substr(at), child, depth + 1);
        } else {
          warn("tikz: \\begin{" + env + "} is not supported in a picture: left out");
        }
      } else if (cmd == "foreach") {
        i = foreach (text, i, scope, depth);
      } else if (cmd == "tikzset") {
        skipSpace(text, i);
        std::size_t at = i;
        const std::string body = groupAt(text, at);
        i = at;
        Style ignored;
        applyOptions(ignored, body, false, 0);
      } else if (cmd == "usetikzlibrary" || cmd == "pgfmathsetseed" || cmd == "pgfkeys") {
        skipSpace(text, i);
        if (i < text.size() && text[i] == '{') {
          std::size_t at = i;
          groupAt(text, at);
          i = at;
        }
      } else {
        warn("tikz: \\" + cmd + " is not supported in a picture: left out");
        i = statementEnd(text, i) + 1;
      }
    }
  }

  /** \foreach \x in {list} body: the body once for each value, `\x` replaced. */
  std::size_t foreach (const std::string& text, std::size_t i, const Style& scope, int depth) {
    skipSpace(text, i);
    // The variables: \x or \x/\y.
    std::vector<std::string> vars;
    while (i < text.size() && text[i] == '\\') {
      i++;
      vars.push_back(word(text, i));
      skipSpace(text, i);
      if (i < text.size() && text[i] == '/') {
        i++;
        skipSpace(text, i);
      }
    }
    skipSpace(text, i);
    if (text.compare(i, 2, "in") != 0 || vars.empty()) {
      warn("tikz: \\foreach is not in the form `\\x in {list}': left out");
      return statementEnd(text, i) + 1;
    }
    i += 2;
    skipSpace(text, i);
    std::string list;
    if (i < text.size() && text[i] == '{') {
      std::size_t at = i;
      list = groupAt(text, at);
      i = at;
    }
    skipSpace(text, i);
    // The body: a group, or the one statement.
    std::string body;
    if (i < text.size() && text[i] == '{') {
      std::size_t at = i;
      body = groupAt(text, at);
      i = at;
    } else {
      const std::size_t end = statementEnd(text, i);
      body = text.substr(i, end + 1 - i);
      i = end + 1;
    }
    for (const std::vector<std::string>& values : foreachValues(list, vars.size())) {
      std::string one = body;
      for (std::size_t v = 0; v < vars.size(); v++) one = replaceControl(one, vars[v], v < values.size() ? values[v] : "");
      run(one, scope, depth + 1);
    }
    return i;
  }

  /** The values of a \foreach list: `1,2,3`, `0,60,...,300`, `a/1,b/2`. */
  std::vector<std::vector<std::string>> foreachValues(const std::string& list, std::size_t vars) {
    std::vector<std::string> items;
    for (const std::string& it : splitTop(list, ',')) items.push_back(trim(it));
    // An ellipsis: the step is the difference of the two values before it.
    std::vector<std::string> expanded;
    for (std::size_t k = 0; k < items.size(); k++) {
      if (items[k] == "..." && k >= 1 && k + 1 < items.size()) {
        double last = 0, prev = 0, stop = 0;
        const bool two = k >= 2 && number(items[k - 2], prev) && number(items[k - 1], last);
        const bool one = !two && number(items[k - 1], last);
        if ((two || one) && number(items[k + 1], stop)) {
          const double step = two ? last - prev : (stop >= last ? 1.0 : -1.0);
          if (std::fabs(step) > 1e-9) {
            for (double v = last + step; step > 0 ? v < stop - 1e-9 : v > stop + 1e-9; v += step) {
              expanded.push_back(formatNumber(v));
              if (expanded.size() > 4096) break;
            }
          }
        }
        continue;
      }
      if (items[k].empty()) continue;
      expanded.push_back(items[k]);
    }
    std::vector<std::vector<std::string>> out;
    for (const std::string& e : expanded) {
      std::vector<std::string> parts = splitTop(e, '/');
      for (std::string& p : parts) p = trim(p);
      if (vars == 1) parts = {e};
      out.push_back(parts);
    }
    return out;
  }

  static std::string formatNumber(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
  }

  /** `text` with the control word `\name` replaced by `value`. */
  static std::string replaceControl(const std::string& text, const std::string& name, const std::string& value) {
    std::string out;
    const std::string tag = "\\" + name;
    for (std::size_t i = 0; i < text.size();) {
      if (text.compare(i, tag.size(), tag) == 0 &&
          (i + tag.size() >= text.size() || std::isalpha(static_cast<unsigned char>(text[i + tag.size()])) == 0)) {
        out += value;
        i += tag.size();
      } else {
        out += text[i++];
      }
    }
    return out;
  }

  // --- a path -----------------------------------------------------------------------------------

  /** `\matrix (name) [options] at (point) { cell & cell \\ ... };` as a matrix of nodes (`matrix of nodes`, `matrix of
   *  math nodes`): each cell is a node named `name-row-column`, the styles of `every cell`, `column n`, `row n`, `row r
   *  column c` and the cell's own `|[options]|` applied in pgf's order. */
  void matrixStatement(const std::string& text, const Style& scope) {
    PathItem item;
    item.style = scope;
    auto grid = std::make_shared<NodeMatrix>();
    Node& mn = grid->node;
    mn.style = scope;
    mn.style.draw = Paint();
    mn.style.fill = Paint();
    mn.style.shape = "rectangle";
    applyStyleNamed(mn.style, "every matrix");
    std::size_t i = 0;
    std::string body;
    for (;;) {
      skipSpace(text, i);
      if (i >= text.size()) break;
      if (text[i] == '[') {
        std::size_t at = i;
        applyOptions(mn.style, groupAt(text, at), true, 0);
        i = at;
      } else if (text[i] == '(') {
        std::size_t at = i;
        mn.name = trim(parenAt(text, at));
        i = at;
      } else if (text.compare(i, 2, "at") == 0 && i + 2 < text.size() && (text[i + 2] == ' ' || text[i + 2] == '(')) {
        i += 2;
        skipSpace(text, i);
        std::size_t at = i;
        if (i < text.size() && text[i] == '(') {
          mn.at = coord(parenAt(text, at));
          mn.hasAt = true;
        }
        i = at;
      } else if (text[i] == '{') {
        std::size_t at = i;
        body = groupAt(text, at);
        i = at;
        break;
      } else {
        break;
      }
    }
    if (mn.style.matrixKind == 0) {
      warn("tikz: only `matrix of nodes` and `matrix of math nodes` are drawn: a matrix of paths is left out");
      return;
    }
    if (mn.name.empty()) mn.name = "tikz@matrix" + std::to_string(++_treeNames);
    _defined.insert(mn.name);
    // Rows end at `\\`, cells at `&`, outside every group.
    std::vector<std::string> rowTexts;
    std::vector<Dimen> skips{Dimen()};
    {
      std::string cur;
      int depth = 0;
      for (std::size_t k = 0; k < body.size(); k++) {
        const char c = body[k];
        if (c == '{' || c == '[') depth++;
        if (c == '}' || c == ']') depth--;
        if (depth == 0 && c == '\\' && k + 1 < body.size() && body[k + 1] == '\\') {
          k++;
          Dimen skip;
          std::size_t j = k + 1;
          while (j < body.size() && std::isspace(static_cast<unsigned char>(body[j])) != 0) j++;
          if (j < body.size() && body[j] == '[') {
            std::size_t at = j;
            skip = dimen(groupAt(body, at));
            k = at - 1;
          }
          rowTexts.push_back(cur);
          skips.push_back(skip);
          cur.clear();
          continue;
        }
        if (c == '\\' && k + 1 < body.size()) {
          cur += c;
          cur += body[++k];
          continue;
        }
        cur += c;
      }
      if (!trim(cur).empty()) rowTexts.push_back(cur);
    }
    int cols = 0;
    for (std::size_t r = 0; r < rowTexts.size(); r++) {
      const std::vector<std::string> cells = splitTop(rowTexts[r], '&');
      cols = std::max(cols, static_cast<int>(cells.size()));
      for (std::size_t c = 0; c < cells.size(); c++) {
        MatrixCell cell;
        cell.row = static_cast<int>(r) + 1;
        cell.col = static_cast<int>(c) + 1;
        cell.node = matrixCell(mn, cell.row, cell.col, cells[c], scope, cell.empty);
        grid->cells.push_back(std::move(cell));
      }
    }
    grid->rows = static_cast<int>(rowTexts.size());
    grid->cols = cols;
    grid->rowSkip = skips;
    item.grid = std::move(grid);
    _pic.paths.push_back(std::move(item));
  }

  /** One cell: the styles in the order of `matrix/inner style order`, then its own options. */
  Node matrixCell(const Node& matrix, int row, int col, const std::string& source, const Style& scope, bool& empty) {
    Node n;
    n.style = scope;
    n.style.draw = Paint();
    n.style.fill = Paint();
    n.style.tipStart.reset();
    n.style.tipEnd.reset();
    n.style.rounded.reset();
    n.style.shape = "rectangle";
    // The colour and the font of the matrix are the cells'.
    n.style.current = matrix.style.current;
    n.style.textColor = matrix.style.textColor;
    n.style.font = matrix.style.font;
    n.style.matrixKind = 0;
    n.style.nodesStyle.clear();
    applyStyleNamed(n.style, "every node");
    applyOptions(n.style, matrix.style.nodesStyle, true, 0);
    const std::string r = std::to_string(row), c = std::to_string(col);
    // `nodes={...}` in a cell's scope is for the node it makes: applied where it is read.
    const auto step = [&](const std::string& name) {
      applyStyleNamed(n.style, name);
      if (!n.style.nodesStyle.empty()) {
        const std::string inner = n.style.nodesStyle;
        n.style.nodesStyle.clear();
        applyOptions(n.style, inner, true, 0);
      }
    };
    step("every cell");
    applyOptions(n.style, matrix.style.cellsStyle, true, 0);
    step("column " + c);
    step(col % 2 == 1 ? "every odd column" : "every even column");
    step("row " + r);
    step(row % 2 == 1 ? "every odd row" : "every even row");
    step("row " + r + " column " + c);
    std::string text = trim(source);
    if (text.size() >= 2 && text[0] == '|' && text[1] == '[') {
      const std::size_t close = text.find("]|");
      if (close != std::string::npos) {
        applyOptions(n.style, text.substr(2, close - 2), true, 0);
        text = trim(text.substr(close + 2));
      }
    }
    n.style.labelSpecs.clear();
    n.ownFrom = scope.transform.size();
    n.name = matrix.name + "-" + r + "-" + c;
    empty = text.empty() && !matrix.style.emptyNodes;
    if (matrix.style.matrixKind == 2 && !text.empty()) text = "$" + text + "$";
    n.content = text.empty() ? nullptr : contentOf(text, n.style);
    return n;
  }

  /** Whether a path of straight sides is, as far as its literal coordinates say, a convex polygon -- the only region the layout
   *  clips to. A coordinate that is not a number of lengths (a node, a calculation, a relative point) cannot be judged here,
   *  and a path with one counts as convex; the layout drops a concave region it is handed. */
  /** A length written in absolute units, in centimetres; false for one that follows the text (`em`) or is not known. */
  static bool lengthInCm(const Dimen& d, double& out) {
    switch (d.unit) {
      case UnitType::cm: out = d.val; return true;
      case UnitType::mm: out = d.val * 0.1; return true;
      case UnitType::in: out = d.val * 2.54; return true;
      case UnitType::pt: out = d.val * 2.54 / 72.0; return true;
      case UnitType::point: out = d.val * 2.54 / 72.27; return true;
      default: return false;
    }
  }

  /** Whether a literal coordinate is within a hundred metres of the origin: a picture further out has no use, and its size
   *  overflows what a box and the host can hold. One that cannot be told (a node, a calculation) is taken to be. */
  static bool coordinateIsReasonable(const Coord& c) {
    double a = 0, b = 0;
    constexpr double limit = 1e4;
    if (c.kind == Coord::Kind::xy) {
      if (lengthInCm(c.x, a) && !(std::fabs(a) <= limit)) return false;
      if (lengthInCm(c.y, b) && !(std::fabs(b) <= limit)) return false;
    } else if (c.kind == Coord::Kind::polar) {
      if (lengthInCm(c.r, a) && !(std::fabs(a) <= limit)) return false;
    }
    return true;
  }

  static bool convexSides(const std::vector<PathOp>& ops) {
    const auto toCm = [](const Dimen& d, double& out) { return lengthInCm(d, out); };
    const auto at = [&](const Coord& c, double& x, double& y) {
      if (c.relative != 0) return false;
      if (c.kind == Coord::Kind::xy) return toCm(c.x, x) && toCm(c.y, y);
      if (c.kind == Coord::Kind::polar && !c.r2.isValid()) {
        double r = 0;
        if (!toCm(c.r, r)) return false;
        x = r * std::cos(c.angle * 3.14159265358979 / 180.0);
        y = r * std::sin(c.angle * 3.14159265358979 / 180.0);
        return true;
      }
      return false;
    };
    std::vector<std::vector<std::pair<double, double>>> polygons(1);
    for (const PathOp& op : ops) {
      auto& pts = polygons.back();
      double x = 0, y = 0;
      switch (op.kind) {
        case PathOp::Kind::move:
          if (!pts.empty()) polygons.emplace_back();
          if (!at(op.p, x, y)) return true;
          polygons.back().emplace_back(x, y);
          break;
        case PathOp::Kind::line:
          if (!at(op.p, x, y)) return true;
          pts.emplace_back(x, y);
          break;
        case PathOp::Kind::hline:
        case PathOp::Kind::vline:
          if (pts.empty() || !at(op.p, x, y)) return true;
          pts.emplace_back(op.kind == PathOp::Kind::hline ? x : pts.back().first, op.kind == PathOp::Kind::hline ? pts.back().second : y);
          pts.emplace_back(x, y);
          break;
        default: break;   // close, rectangle, circle, ellipse, nodes: convex or no corner of their own
      }
    }
    for (const auto& pts : polygons) {
      if (pts.size() < 3) continue;
      int sign = 0;
      for (std::size_t i = 0; i < pts.size(); i++) {
        const auto &a = pts[i], &b = pts[(i + 1) % pts.size()], &c = pts[(i + 2) % pts.size()];
        const double cross = (b.first - a.first) * (c.second - b.second) - (b.second - a.second) * (c.first - b.first);
        const double scale = std::hypot(b.first - a.first, b.second - a.second) * std::hypot(c.first - b.first, c.second - b.second);
        if (std::fabs(cross) <= 1e-7 * scale) continue;   // a straight angle (or a repeated point)
        const int s = cross > 0 ? 1 : -1;
        if (sign != 0 && s != sign) return false;
        sign = s;
      }
    }
    return true;
  }

  /** One statement; the id of the clip path it made, or -1. */
  int statement(const std::string& cmd, const std::string& text, const Style& scope) {
    if (cmd == "matrix") {
      matrixStatement(text, scope);
      return -1;
    }
    PathItem path;
    path.style = scope;
    std::size_t i = 0;
    // Nodes and paths take the picture's style for them first.
    const bool nodeCommand = cmd == "node" || cmd == "coordinate";
    if (!nodeCommand) applyStyleNamed(path.style, "every path");
    if (cmd == "draw") {
      path.style.draw.mode = Paint::Mode::current;
    } else if (cmd == "fill") {
      path.style.fill.mode = Paint::Mode::current;
    } else if (cmd == "filldraw") {
      path.style.draw.mode = path.style.fill.mode = Paint::Mode::current;
    } else if (cmd == "clip" || cmd == "useasboundingbox") {
      path.clip = true;
    }
    skipSpace(text, i);
    if (i < text.size() && text[i] == '[') {
      const std::string options = groupAt(text, i);
      applyOptions(path.style, options, false, 0);
    }
    if (nodeCommand) {
      // \node[options] (name) at (coord) {text};
      i = 0;
      Node n = nodeOf(cmd == "coordinate", text, i, path.style, true, scope);
      // (The picture item's own style is what puts the node: its own scale and rotation are taken out of it too.)
      const std::size_t before = path.style.transform.size();
      path.style.nodeScaleX = path.style.nodeScaleY = 1.f;
      Style scratch = path.style;
      takeNodeTransform(scratch, scope.transform.size());
      if (scratch.transform.size() != before) path.style.transform = scratch.transform;
      readTree(n, scope, 1, text, i);
      path.isNode = true;
      path.nodes.push_back(std::move(n));
      _pic.paths.push_back(std::move(path));
      flushEdges();
      return -1;
    }
    pathOps(path, text, i);
    // `\clip` and the `clip` option: what is drawn after, in this scope, stays inside the path. Only a path of straight
    // sides, rectangles, circles and ellipses is a region the layout can clip to; any other is drawn as it stands.
    int clip = -1;
    if (cmd == "clip" || path.style.clipPath) {
      bool curved = false;
      int parts = 0;
      for (const PathOp& op : path.ops) {
        if (op.kind == PathOp::Kind::curve || op.kind == PathOp::Kind::arc || op.kind == PathOp::Kind::to ||
            op.kind == PathOp::Kind::grid) {
          curved = true;
        }
        if (op.kind == PathOp::Kind::move) parts++;
      }
      bool simple = !path.ops.empty() && !curved;
      if (simple && !convexSides(path.ops)) {
        simple = false;
        warn("tikz: a clip path that is not convex is not supported: it is not clipped to");
      } else if (curved) {
        warn("tikz: a clip path of curves is not supported: only rectangles, circles, ellipses and straight sides are clipped to");
      }
      if (simple && parts > 1) {
        warn("tikz: a clip path of several parts is clipped to its first part only");
      }
      if (simple) {
        clip = _clips++;
        PathItem region = path;
        region.clipId = clip;
        region.nodes.clear();
        region.style.draw = region.style.fill = Paint();
        if (cmd != "clip") _pic.paths.push_back(path);   // `\draw[clip]` also draws
        path = std::move(region);
      }
    }
    _pic.paths.push_back(std::move(path));
    flushEdges();
    return clip;
  }

  void applyStyleNamed(Style& st, const std::string& name) {
    const auto it = _styles.find(name);
    if (it != _styles.end()) applyOptions(st, it->second, name == "every node", 0);
  }

  /** `node [options] (name) at (coord) {text}` from `i`, which is on the `[`, the `(` or the `{` after the word. */
  Node nodeOf(bool coordinateOnly, const std::string& t, std::size_t& i, const Style& pathStyle, bool standalone,
              const Style& enclosing) {
    Node n;
    n.style = pathStyle;
    // A `\node[options]` reads its options twice: as the options of its path, and here. The steps of the first reading are
    // dropped, or `scale=2` was a scale of 4.
    if (standalone && n.style.transform.size() > enclosing.transform.size()) {
      n.style.transform.erase(n.style.transform.begin() + static_cast<std::ptrdiff_t>(enclosing.transform.size()),
                              n.style.transform.end());
    }
    n.style.labelSpecs.clear();  // the node's label= was read once already, as an option of its path
    n.ownFrom = standalone ? static_cast<std::size_t>(-1) : pathStyle.transform.size();
    // A node is not a path: what is drawn, filled or pointed is its own.
    if (!standalone || true) {
      n.style.draw = Paint();
      n.style.fill = Paint();
      n.style.tipStart.reset();
      n.style.tipEnd.reset();
      n.style.rounded.reset();
    }
    applyStyleNamed(n.style, "every node");
    if (!_nodeExtra.empty()) applyStyleNamed(n.style, _nodeExtra);
    std::string optionsText;
    bool optionsRead = false;
    std::string text;
    bool haveText = false;
    for (;;) {
      skipSpace(t, i);
      if (i >= t.size()) break;
      if (t[i] == '[' && !optionsRead) {
        optionsText = groupAt(t, i);
        optionsRead = true;
        // `circle`, `draw` and kin may come later in the options: apply them as they stand.
        applyOptions(n.style, optionsText, true, 0);
        continue;
      }
      if (t[i] == '(') {
        std::size_t at = i;
        const std::string inside = parenAt(t, at);
        n.name = trim(inside);
        _defined.insert(n.name);
        i = at;
        continue;
      }
      if (t.compare(i, 2, "at") == 0 && (i + 2 >= t.size() || std::isspace(static_cast<unsigned char>(t[i + 2])) != 0 || t[i + 2] == '(')) {
        i += 2;
        skipSpace(t, i);
        if (i < t.size() && t[i] == '(') {
          std::size_t at = i;
          n.at = coord(parenAt(t, at));
          n.hasAt = true;
          i = at;
        }
        continue;
      }
      if (t[i] == '{') {
        std::size_t at = i;
        text = groupAt(t, at);
        haveText = true;
        i = at;
        break;
      }
      break;
    }
    if (n.style.shape.empty()) n.style.shape = coordinateOnly ? "coordinate" : "rectangle";
    if (coordinateOnly) n.style.shape = "coordinate";
    n.pos = n.style.pos.value_or(0.5f);
    n.posGiven = n.style.pos.has_value();
    n.content = haveText && !coordinateOnly ? contentOf(text, n.style) : nullptr;
    // TikZ reads `label distance`, `pin distance` and `pin edge` where the label is set up, after the node's own
    // options are closed: the scope round the node decides, whatever the node says (checked with pdfTeX).
    n.style.labelDistance = enclosing.labelDistance;
    n.style.pinDistance = enclosing.pinDistance;
    n.style.pinEdge = enclosing.pinEdge;
    for (const auto& spec : n.style.labelSpecs) n.labels.push_back(labelOf(spec.first, spec.second, n.style));
    n.style.labelSpecs.clear();
    takeNodeTransform(n.style, standalone ? enclosing.transform.size() : pathStyle.transform.size());
    return n;
  }

  /** The `scale`, `xscale`, `yscale` and `rotate` a node was given are the node's own: pgf scales the node and leaves
   *  where it is put (`at (2,1)`) to the picture and its scopes. They were steps of the coordinates, which moved the node
   *  to (4,2) and left it its size. Steps from `from` on are the node's; `rotate` is said and not drawn. */
  void takeNodeTransform(Style& st, std::size_t from) {
    for (std::size_t k = std::min(from, st.transform.size()); k < st.transform.size();) {
      const XStep s = st.transform[k];
      if (s.kind == XStep::Kind::scale || s.kind == XStep::Kind::xscale || s.kind == XStep::Kind::yscale) {
        if (s.kind != XStep::Kind::yscale) st.nodeScaleX *= s.v;
        if (s.kind != XStep::Kind::xscale) st.nodeScaleY *= s.v;
      } else if (s.kind == XStep::Kind::rotate) {
        warn("tikz: rotating a node is not supported: it is drawn unrotated, where it is put");
      } else {
        k++;
        continue;
      }
      st.transform.erase(st.transform.begin() + static_cast<std::ptrdiff_t>(k));
    }
  }

  /** `label=[options]angle:text` and `pin=...`: the angle is a direction (`above`, `30`) and defaults to
   *  `above`; the options and the text are those of a node of its own, drawn beside the one it belongs to. */
  Label labelOf(bool pin, const std::string& spec, const Style& owner) {
    Label l;
    l.pin = pin;
    std::string rest = trim(spec), options;
    if (!rest.empty() && rest[0] == '[') {
      std::size_t at = 0;
      options = groupAt(rest, at);
      rest = trim(rest.substr(at));
    }
    std::size_t colon = std::string::npos;
    int depth = 0;
    for (std::size_t k = 0; k < rest.size() && colon == std::string::npos; k++) {
      if (rest[k] == '{' || rest[k] == '[') depth++;
      if (rest[k] == '}' || rest[k] == ']') depth--;
      if (depth == 0 && rest[k] == ':') colon = k;
    }
    std::string text = rest;
    if (colon != std::string::npos) {
      l.angle = trim(rest.substr(0, colon));
      text = rest.substr(colon + 1);
    }
    if (l.angle.empty()) l.angle = "above";
    Style s;
    s.current = owner.current;
    s.textColor = owner.textColor;
    s.font = owner.font;
    s.shape = "rectangle";
    applyStyleNamed(s, pin ? "every pin" : "every label");
    applyOptions(s, options, true, 0);
    s.labelSpecs.clear();
    l.style = s;
    l.content = contentOf(trim(text), s);
    if (pin) {
      // `every pin edge` is `help lines`: gray, 0.2pt.
      Style e;
      color gray = 0;
      if (colour("gray", gray)) e.current = gray;
      e.draw.mode = Paint::Mode::current;
      e.lineWidth = Dimen{0.2f, UnitType::pt};
      applyStyleNamed(e, "every pin edge");
      // The pin's own `pin edge=` wins over the scope's.
      applyOptions(e, s.pinEdge.empty() ? owner.pinEdge : s.pinEdge, false, 0);
      l.edge = e;
    }
    return l;
  }

  /** The text of a node as an atom: set to its text width and alignment, in its font. */
  sptr<Atom> contentOf(const std::string& text, const Style& st) {
    std::string tex = text;
    if (st.textWidth) {
      const char a = st.align.value_or('l');
      const std::string decl = a == 'c' ? "\\centering " : a == 'r' ? "\\raggedleft " : "";
      tex = "\\begin{minipage}[t]{" + dimenText(*st.textWidth) + "}" + decl + text + "\\end{minipage}";
    } else if (st.align && *st.align != 'l') {
      // Aligned text with no width is lines set against each other.
      tex = text;
    }
    if (!st.font.empty()) tex = "{" + st.font + " " + tex + "}";
    if (tex.empty()) return nullptr;
    return _args.formulaChecked(tex, false);
  }

  static std::string dimenText(const Dimen& d) {
    static const char* names[] = {"em", "ex", "px", "bp", "pc", "mu", "cm", "mm", "in", "sp", "pt", "dd", "cc", "tt", "tw"};
    char buf[48];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(d.val));
    const int u = static_cast<int>(d.unit);
    return std::string(buf) + (u >= 0 && u < 15 ? names[u] : "pt");
  }

  /** The text of the `(`...`)` at `i`, and `i` past it. */
  static std::string parenAt(const std::string& t, std::size_t& i) {
    int depth = 0;
    for (std::size_t j = i; j < t.size(); j++) {
      if (t[j] == '(') depth++;
      if (t[j] == ')' && --depth == 0) {
        const std::string inner = t.substr(i + 1, j - i - 1);
        i = j + 1;
        return inner;
      }
    }
    const std::string rest = t.substr(i + 1);
    i = t.size();
    return rest;
  }

  /** A point of a plot as `x, y` -- each an expression, in cm when it has no unit; false when it is not a number. */
  bool plotPoint(const std::string& text, double& x, double& y) {
    std::vector<std::string> parts = splitTop(text, ',');
    if (parts.size() < 2) {
      // Polar, `angle:radius`, as a coordinate writes it.
      const std::vector<std::string> polar = splitTop(text, ':');
      if (polar.size() != 2) return false;
      Expr ea(unbrace(polar[0])), er(unbrace(polar[1]));
      double angle = 0, r = 0;
      if (!ea.value(angle) || !er.value(r) || !(std::fabs(r) <= 1e4)) return false;
      x = r * std::cos(angle * 3.14159265358979 / 180.0);
      y = r * std::sin(angle * 3.14159265358979 / 180.0);
      return true;
    }
    Expr ex(unbrace(parts[0])), ey(unbrace(parts[1]));
    // (Within a hundred metres: `exp(\x)` runs off the page long before it overflows, and the comparison is false for NaN.)
    return ex.value(x) && ey.value(y) && std::fabs(x) <= 1e4 && std::fabs(y) <= 1e4;
  }

  /** `text` with the plot's variable replaced by the number `v`. */
  static std::string bindVariable(const std::string& text, const std::string& var, double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "(%.12g)", v);
    std::string out;
    const bool word = !var.empty() && std::isalpha(static_cast<unsigned char>(var.back())) != 0;
    for (std::size_t k = 0; k < text.size();) {
      const bool here = text.compare(k, var.size(), var) == 0;
      const bool more = here && word && k + var.size() < text.size() && std::isalpha(static_cast<unsigned char>(text[k + var.size()])) != 0;
      if (here && !more) {
        out += buf;
        k += var.size();
      } else {
        out += text[k++];
      }
    }
    return out;
  }

  /** The values of the plot's variable: `samples at`, or `samples` of them evenly over the `domain`. */
  std::vector<double> plotSamples(const Style& s) {
    std::vector<double> xs;
    if (s.samplesAt) {
      // `0,0.5,...,2`: the dots continue the step of the two values before them.
      std::vector<std::string> items;
      for (const std::string& raw : splitTop(unbrace(*s.samplesAt), ',')) items.push_back(trim(raw));
      for (std::size_t k = 0; k < items.size(); k++) {
        double v = 0;
        if (items[k] == "..." || items[k] == "\\ldots") {
          double last = 0;
          if (xs.size() >= 2 && k + 1 < items.size() && number(items[k + 1], last)) {
            const double step = xs[xs.size() - 1] - xs[xs.size() - 2];
            if (std::fabs(step) > 1e-12) {
              for (double x = xs.back() + step; step > 0 ? x < last - 1e-9 : x > last + 1e-9; x += step) {
                if (xs.size() >= static_cast<std::size_t>(kMaxPoints)) {
                  warn("tikz: samples at has more than " + std::to_string(static_cast<int>(kMaxPoints)) + " values: the rest are left out");
                  return xs;
                }
                xs.push_back(x);
              }
            }
          }
        } else if (number(items[k], v)) {
          xs.push_back(v);
        }
      }
      return xs;
    }
    const std::string domain = s.domain.value_or("-5:5");
    const auto colon = domain.find(':');
    double a = 0, b = 0;
    if (colon == std::string::npos || !number(domain.substr(0, colon), a) || !number(domain.substr(colon + 1), b)) {
      warn("tikz: domain=" + domain + " is not a range a:b: left out");
      return xs;
    }
    const int n = s.samples.value_or(25);
    if (std::fabs(b - a) / (n - 1) < 1e-4) return {a, b};
    for (int k = 0; k < n; k++) xs.push_back(a + (b - a) * k / (n - 1));
    return xs;
  }

  /** `plot[options] (\x, {f(\x)})` and `plot[options] coordinates {(x,y) ...}`, as the handler the options name (pgfplothandlers)
   *  turns the points into path operations. The first point is a line when a `--` came before `plot`, else a move. */
  void plotOps(PathItem& path, const std::string& t, std::size_t& i, bool lineFirst, std::vector<PathOp>& out) {
    skipSpace(t, i);
    Style s = path.style;
    if (i < t.size() && t[i] == '[') applyOptions(s, groupAt(t, i), false, 0);
    skipSpace(t, i);
    std::vector<std::pair<double, double>> pts;
    bool skipped = false;
    if (t.compare(i, 11, "coordinates") == 0) {
      i += 11;
      skipSpace(t, i);
      if (i < t.size() && t[i] == '{') {
        const std::string body = groupAt(t, i);
        for (std::size_t k = 0; k < body.size();) {
          if (body[k] != '(') {
            k++;
            continue;
          }
          double x = 0, y = 0;
          if (plotPoint(parenAt(body, k), x, y)) {
            pts.emplace_back(x, y);
          } else {
            skipped = true;
          }
        }
      }
    } else if (t.compare(i, 8, "function") == 0) {
      i += 8;
      skipSpace(t, i);
      if (i < t.size() && t[i] == '{') groupAt(t, i);
      warn("tikz: plot function (gnuplot) is not supported: left out");
      return;
    } else if (i < t.size() && t[i] == '(') {
      const std::string expression = parenAt(t, i);
      const std::string var = s.plotVariable.empty() ? std::string("\\x") : s.plotVariable;
      for (const double v : plotSamples(s)) {
        double x = 0, y = 0;
        if (plotPoint(bindVariable(expression, var, v), x, y)) {
          pts.emplace_back(x, y);
        } else {
          skipped = true;
        }
      }
    } else {
      warn("tikz: plot is not followed by a point, coordinates or function: left out");
      return;
    }
    if (skipped) warn("tikz: a point of a plot is not a number: left out");
    if (pts.empty()) return;
    const auto at = [](double x, double y) {
      Coord c;
      c.x = Dimen{static_cast<float>(x), UnitType::cm};
      c.y = Dimen{static_cast<float>(y), UnitType::cm};
      return c;
    };
    const auto add = [&](PathOp::Kind kind, double x, double y) {
      PathOp op;
      op.kind = kind;
      op.p = at(x, y);
      out.push_back(std::move(op));
    };
    const PathOp::Kind first = lineFirst ? PathOp::Kind::line : PathOp::Kind::move;
    const std::string& handler = s.plotHandler;
    if (handler == "ycomb" || handler == "xcomb") {
      // A line from the axis to every point, each its own subpath.
      for (const auto& p : pts) {
        add(PathOp::Kind::move, handler == "ycomb" ? p.first : 0.0, handler == "ycomb" ? 0.0 : p.second);
        add(PathOp::Kind::line, p.first, p.second);
      }
    } else if (handler == "const plot") {
      add(first, pts[0].first, pts[0].second);
      for (std::size_t k = 1; k < pts.size(); k++) {
        add(PathOp::Kind::line, pts[k].first, pts[k - 1].second);
        add(PathOp::Kind::line, pts[k].first, pts[k].second);
      }
    } else if (handler == "smooth" || handler == "smooth cycle") {
      if (handler == "smooth cycle") warn("tikz: smooth cycle is drawn as smooth, not closed");
      // pgf's curveto handler: the controls lie along the line through the neighbours, 0.2775 * tension of the way.
      const double k = 0.2775 * s.tension.value_or(0.5);
      add(first, pts[0].first, pts[0].second);
      std::pair<double, double> support = pts[0];
      for (std::size_t n = 1; n < pts.size(); n++) {
        PathOp op;
        op.kind = PathOp::Kind::curve;
        op.p = at(pts[n].first, pts[n].second);
        op.c1 = at(support.first, support.second);
        if (n + 1 < pts.size()) {
          const double dx = k * (pts[n + 1].first - pts[n - 1].first), dy = k * (pts[n + 1].second - pts[n - 1].second);
          op.c2 = at(pts[n].first - dx, pts[n].second - dy);
          support = {pts[n].first + dx, pts[n].second + dy};
        } else {
          op.c2 = op.p;
        }
        out.push_back(std::move(op));
      }
    } else {
      add(first, pts[0].first, pts[0].second);
      for (std::size_t n = 1; n < pts.size(); n++) add(PathOp::Kind::line, pts[n].first, pts[n].second);
      if (handler == "sharp cycle") {
        PathOp op;
        op.kind = PathOp::Kind::close;
        out.push_back(std::move(op));
      }
    }
  }

  void pathOps(PathItem& path, const std::string& t, std::size_t i) {
    enum class Pending { none, line, hline, vline, rect, to, edge, grid } pending = Pending::none;
    bool nodeBetween = false;                  // a node was written after the connector, before its coordinate
    std::vector<int> betweenNodes;
    PathOp lastOp;
    std::string toOptions;
    const auto push = [&](PathOp op) {
      const int index = static_cast<int>(path.ops.size());
      path.ops.push_back(std::move(op));
      for (const int n : betweenNodes) path.nodes[static_cast<std::size_t>(n)].alongOp = index;
      betweenNodes.clear();
      nodeBetween = false;
      pending = Pending::none;
      toOptions.clear();
      return index;
    };
    for (;;) {
      skipSpace(t, i);
      if (i >= t.size()) break;
      const char c = t[i];
      int relative = 0;
      if (c == '+') {
        relative = 1;
        i++;
        if (i < t.size() && t[i] == '+') {
          relative = 2;
          i++;
        }
        skipSpace(t, i);
        if (i >= t.size() || t[i] != '(') {
          warn("tikz: `+' is not followed by a coordinate: left out");
          continue;
        }
      }
      if (t[i] == '(') {
        std::size_t at = i;
        Coord co = coord(parenAt(t, at));
        co.relative = relative;
        i = at;
        if (!coordinateIsReasonable(co)) {
          warn("tikz: a coordinate more than 100 metres from the origin is left out");
          continue;
        }
        PathOp op;
        op.p = co;
        if (pending == Pending::to || pending == Pending::edge) {
          // A `to` with options of its own, and an edge, which always has: its path style is the path's, drawn.
          if (pending == Pending::edge || !toOptions.empty()) {
            Style s = path.style;
            if (pending == Pending::edge && s.draw.mode != Paint::Mode::color) s.draw.mode = Paint::Mode::current;
            applyOptions(s, toOptions, false, 0);
            op.own = s;
          }
        }
        switch (pending) {
          case Pending::none: op.kind = PathOp::Kind::move; break;
          case Pending::line: op.kind = PathOp::Kind::line; break;
          case Pending::hline: op.kind = PathOp::Kind::hline; break;
          case Pending::vline: op.kind = PathOp::Kind::vline; break;
          case Pending::rect: op.kind = PathOp::Kind::rect; break;
          case Pending::to: op.kind = PathOp::Kind::to; break;
          case Pending::edge: op.kind = PathOp::Kind::to; op.edge = true; break;
          case Pending::grid: op.kind = PathOp::Kind::grid; break;
        }
        push(std::move(op));
        continue;
      }
      if (t.compare(i, 2, "--") == 0) {
        pending = Pending::line;
        i += 2;
        continue;
      }
      if (t.compare(i, 2, "-|") == 0) {
        pending = Pending::hline;
        i += 2;
        continue;
      }
      if (t.compare(i, 2, "|-") == 0) {
        pending = Pending::vline;
        i += 2;
        continue;
      }
      if (t.compare(i, 2, "..") == 0) {
        // `.. controls (c1) and (c2) .. (p)`.
        i += 2;
        skipSpace(t, i);
        const std::string w = word(t, i);
        if (w != "controls") {
          warn("tikz: `.. " + w + "' is not supported: left out");
          continue;
        }
        PathOp op;
        op.kind = PathOp::Kind::curve;
        skipSpace(t, i);
        if (i < t.size() && t[i] == '(') {
          std::size_t at = i;
          op.c1 = coord(parenAt(t, at));
          i = at;
        }
        skipSpace(t, i);
        if (t.compare(i, 3, "and") == 0) {
          i += 3;
          skipSpace(t, i);
          if (i < t.size() && t[i] == '(') {
            std::size_t at = i;
            op.c2 = coord(parenAt(t, at));
            i = at;
          }
        } else {
          op.c2 = op.c1;
        }
        skipSpace(t, i);
        if (t.compare(i, 2, "..") == 0) i += 2;
        skipSpace(t, i);
        // Nodes written between the `..` and the point the curve ends at sit half way along it.
        while (t.compare(i, 4, "node") == 0 && (i + 4 >= t.size() || std::isalnum(static_cast<unsigned char>(t[i + 4])) == 0)) {
          i += 4;
          Node between = nodeOf(false, t, i, path.style, false, path.style);
          between.between = true;
          path.nodes.push_back(std::move(between));
          PathOp nodeOp;
          nodeOp.kind = PathOp::Kind::node;
          nodeOp.node = static_cast<int>(path.nodes.size()) - 1;
          betweenNodes.push_back(nodeOp.node);
          path.ops.push_back(std::move(nodeOp));
          skipSpace(t, i);
        }
        if (i < t.size() && t[i] == '+') {
          op.p.relative = 1;
          i++;
          if (i < t.size() && t[i] == '+') {
            op.p.relative = 2;
            i++;
          }
        }
        if (i < t.size() && t[i] == '(') {
          std::size_t at = i;
          const int rel = op.p.relative;
          op.p = coord(parenAt(t, at));
          op.p.relative = rel;
          i = at;
        }
        push(std::move(op));
        continue;
      }
      if (c == '[') {
        // Options of the operation that is pending or has just been read.
        const std::string options = groupAt(t, i);
        if (pending == Pending::to || pending == Pending::edge) {
          toOptions = options;
        } else if (pending == Pending::grid) {
          applyOptions(path.style, options, false, 0);
        } else {
          warn("tikz: options in the middle of a path are not supported: left out");
        }
        continue;
      }
      if (c == '{') {
        // A group inside a path: `{[rounded corners] ...}` is not read.
        warn("tikz: a group inside a path is not supported: left out");
        groupAt(t, i);
        continue;
      }
      if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
        const std::size_t before = i;
        const std::string w = word(t, i);
        if (w == "cycle") {
          PathOp op;
          op.kind = PathOp::Kind::close;
          push(std::move(op));
        } else if (w == "rectangle") {
          pending = Pending::rect;
        } else if (w == "to") {
          pending = Pending::to;
        } else if (w == "edge") {
          pending = Pending::edge;
        } else if (w == "grid") {
          pending = Pending::grid;
        } else if (w == "circle" || w == "ellipse") {
          PathOp op;
          op.kind = w == "circle" ? PathOp::Kind::circle : PathOp::Kind::ellipse;
          readRadii(t, i, op);
          push(std::move(op));
        } else if (w == "arc") {
          PathOp op;
          op.kind = PathOp::Kind::arc;
          readArc(t, i, op);
          push(std::move(op));
        } else if (w == "node" || w == "coordinate") {
          Node n = nodeOf(w == "coordinate", t, i, path.style, false, path.style);
          readTree(n, path.style, 1, t, i);
          const bool before = pending != Pending::none;
          n.between = before;
          path.nodes.push_back(std::move(n));
          PathOp op;
          op.kind = PathOp::Kind::node;
          op.node = static_cast<int>(path.nodes.size()) - 1;
          if (before) {
            // Written between an operation and its coordinate: it sits on the segment that coordinate ends.
            betweenNodes.push_back(op.node);
            nodeBetween = true;
            const auto keep = pending;
            const int idx = static_cast<int>(path.ops.size());
            path.ops.push_back(std::move(op));
            (void)idx;
            pending = keep;
          } else {
            path.ops.push_back(std::move(op));
          }
        } else if (w == "at") {
          // `\path at (x,y) node ...`: the node takes the position; nothing to do here.
        } else if (w == "plot") {
          std::vector<PathOp> ops;
          plotOps(path, t, i, pending == Pending::line, ops);
          for (PathOp& op : ops) push(std::move(op));
        } else if (w == "and") {
        } else {
          i = before + 1;
          warn("tikz: `" + w + "' in a path is not supported: left out");
          i = before + w.size();
        }
        continue;
      }
      warn(std::string("tikz: `") + c + "' in a path is not read");
      i++;
    }
    (void)nodeBetween;
    (void)lastOp;
  }

  /** `circle (1)`, `circle (1 and 2)`, `circle[radius=1]`, `ellipse[x radius=2, y radius=1]`. */
  void readRadii(const std::string& t, std::size_t& i, PathOp& op) {
    skipSpace(t, i);
    if (i < t.size() && t[i] == '(') {
      const std::string inside = parenAt(t, i);
      const auto and_ = inside.find(" and ");
      op.r = dimen(and_ == std::string::npos ? inside : inside.substr(0, and_));
      if (and_ != std::string::npos) {
        op.r2 = dimen(inside.substr(and_ + 5));
        op.kind = PathOp::Kind::ellipse;
      }
      return;
    }
    if (i < t.size() && t[i] == '[') {
      const std::string options = groupAt(t, i);
      for (const std::string& raw : splitTop(options, ',')) {
        const auto eq = raw.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(raw.substr(0, eq)), v = unbrace(raw.substr(eq + 1));
        if (k == "radius" || k == "x radius") op.r = dimen(v);
        if (k == "y radius") {
          op.r2 = dimen(v);
          op.kind = PathOp::Kind::ellipse;
        }
      }
      if (op.kind == PathOp::Kind::ellipse && !op.r2.isValid()) op.r2 = op.r;
    }
  }

  void readArc(const std::string& t, std::size_t& i, PathOp& op) {
    skipSpace(t, i);
    if (i < t.size() && t[i] == '(') {
      const std::string inside = parenAt(t, i);
      const auto parts = splitTop(inside, ':');
      double a0 = 0, a1 = 0;
      if (parts.size() >= 3 && number(parts[0], a0) && number(parts[1], a1)) {
        op.a0 = static_cast<float>(a0);
        op.a1 = static_cast<float>(a1);
        const std::string rest = trim(parts[2]);
        const auto and_ = rest.find(" and ");
        op.r = dimen(and_ == std::string::npos ? rest : rest.substr(0, and_));
        if (and_ != std::string::npos) op.r2 = dimen(rest.substr(and_ + 5));
      }
      return;
    }
    if (i < t.size() && t[i] == '[') {
      const std::string options = groupAt(t, i);
      double delta = 0;
      bool haveDelta = false;
      for (const std::string& raw : splitTop(options, ',')) {
        const auto eq = raw.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(raw.substr(0, eq)), v = unbrace(raw.substr(eq + 1));
        double x = 0;
        if (k == "start angle" && number(v, x)) op.a0 = static_cast<float>(x);
        if (k == "end angle" && number(v, x)) op.a1 = static_cast<float>(x);
        if (k == "delta angle" && number(v, x)) {
          delta = x;
          haveDelta = true;
        }
        if (k == "radius" || k == "x radius") op.r = dimen(v);
        if (k == "y radius") op.r2 = dimen(v);
      }
      if (haveDelta) op.a1 = op.a0 + static_cast<float>(delta);
    }
  }
};

}  // namespace tikz

inline cmdmacro(tikzpictureATATenv) {
  auto atom = sptrOf<TikzAtom>();
  tikz::Reader reader(args, *atom);
  reader.picture(args.text(1));
  return atom;
}

}  // namespace microtex

#endif  // MICROTEX_MACRO_TIKZ_H
