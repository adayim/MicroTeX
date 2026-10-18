#include "atom/tikz_atom.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "atom/atom_basic.h"
#include "box/box_single.h"
#include "env/env.h"
#include "env/units.h"
#include "graphic/graphic.h"

namespace microtex {

namespace tikz {

/** The tip a name stands for (`to`, `stealth`, `Latex[length=3mm]` ...), or false. */
bool tipByName(std::string name, Tip& tip) {
  const auto trim = [](std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.pop_back();
    return s;
  };
  name = trim(name);
  if (!name.empty() && name.front() == '{' && name.back() == '}') name = trim(name.substr(1, name.size() - 2));
  std::string params;
  const auto bracket = name.find('[');
  if (bracket != std::string::npos) {
    const auto close = name.rfind(']');
    if (close != std::string::npos && close > bracket) params = name.substr(bracket + 1, close - bracket - 1);
    name = trim(name.substr(0, bracket));
  }
  static const std::map<std::string, Tip::Kind> kinds = {
    {"to", Tip::Kind::to},       {"To", Tip::Kind::to},          {"latex", Tip::Kind::latex},
    {"latex'", Tip::Kind::latex}, {"Latex", Tip::Kind::newLatex}, {"LaTeX", Tip::Kind::newLatex},
    {"stealth", Tip::Kind::stealth}, {"stealth'", Tip::Kind::stealth}, {"Stealth", Tip::Kind::newStealth},
    {"triangle 45", Tip::Kind::triangle45}, {"Triangle", Tip::Kind::triangleMeta}, {"triangle", Tip::Kind::triangle45},
    {"|", Tip::Kind::bar},       {"Bar", Tip::Kind::bar},        {"Circle", Tip::Kind::circle},
    {"*", Tip::Kind::circle},    {"o", Tip::Kind::circle}};
  const auto it = kinds.find(name);
  if (it == kinds.end()) return false;
  tip = Tip();
  tip.kind = it->second;
  // Options: length, width, inset, line width, open, round, scale.
  std::string rest = params;
  while (!rest.empty()) {
    const auto comma = rest.find(',');
    const std::string item = trim(rest.substr(0, comma));
    rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
    const auto eq = item.find('=');
    const std::string key = trim(item.substr(0, eq)), value = eq == std::string::npos ? "" : trim(item.substr(eq + 1));
    if (key == "length") tip.length = Units::getDimen(value);
    if (key == "width") tip.width = Units::getDimen(value);
    if (key == "inset") tip.inset = Units::getDimen(value);
    if (key == "line width") tip.lineWidth = Units::getDimen(value);
    if (key == "open") tip.open = true;
    if (key == "round") tip.round = true;
    if (key == "scale") tip.scale = static_cast<float>(std::atof(value.c_str()));
  }
  return true;
}

}  // namespace tikz

namespace {

using namespace tikz;

constexpr double kPi = 3.14159265358979323846;
// pgf's circle: four cubic arcs, each with control points this share of the radius from the ends.
constexpr double kKappa = 0.5522847498;

// Geometry here is TikZ's: x to the right, y up. The box flips it when it draws.
struct P {
  double x = 0, y = 0;
};
P operator+(P a, P b) { return {a.x + b.x, a.y + b.y}; }
P operator-(P a, P b) { return {a.x - b.x, a.y - b.y}; }
P operator*(P a, double k) { return {a.x * k, a.y * k}; }
double length(P a) { return std::sqrt(a.x * a.x + a.y * a.y); }
double radians(double deg) { return deg * kPi / 180.0; }
double degrees(double rad) { return rad * 180.0 / kPi; }
P polar(double deg, double r) { return {r * std::cos(radians(deg)), r * std::sin(radians(deg))}; }
P unit(P a) {
  const double n = length(a);
  return n > 1e-9 ? a * (1.0 / n) : P{1, 0};
}
P left(P u) { return {-u.y, u.x}; }

/** A cubic segment, a line when `cubic` is false. */
struct Seg {
  bool cubic = false;
  P c1, c2, to;
};

/** A run of segments from `start`: a subpath. */
struct Sub {
  P start;
  std::vector<Seg> segs;
  bool closed = false;
};

/** What the box draws, in order. */
struct Prim {
  std::vector<Sub> subs;
  bool stroke = false, fill = false;
  color strokeCol = 0, fillCol = 0;
  double lw = 0.4;
  std::vector<double> dash;
  double phase = 0;
  int cap = CAP_BUTT, join = JOIN_MITER;
  /** A node's text: drawn at its baseline point, in `textCol`. */
  sptr<Box> content;
  P at;
  color textCol = 0;
  int layer = 0;             // `on background layer` (-1) is drawn before the main layer (0)
  std::vector<int> clips;    // the clip paths this is drawn inside of (a PathItem's clipId)
};

struct Matrix {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  P apply(P p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  P linear(P p) const { return {a * p.x + c * p.y, b * p.x + d * p.y}; }
  // This matrix after `m` has been applied to the coordinates first.
  Matrix operator*(const Matrix& m) const {
    return {a * m.a + c * m.b, b * m.a + d * m.b, a * m.c + c * m.d, b * m.c + d * m.d,
            a * m.e + c * m.f + e, b * m.e + d * m.f + f};
  }
  double scale() const { return std::sqrt(std::fabs(a * d - b * c)); }
};

/** A node once it has its size: where it is, how far it reaches, and what draws its text. */
/** The box of the points a node is fitted to. */
struct FitBox {
  P centre;
  double width = 0, height = 0;
};

struct Geom {
  P c;                       // the centre of the shape
  double hw = 0, hh = 0;     // half the width and height of the shape, outer separation not included
  double outer = 0;          // the outer separation
  std::string shape;
  double baseline = 0;       // the text's baseline, relative to the centre (negative: below)
  bool point = false;        // a coordinate, no shape
  double arcR = 0;           // a rounded rectangle: the radius of its ends,
  double inset = 0;          // and how far the ends' chords are in from the tips
  double halfArc = 0;        // and half the angle an end covers, degrees
  // A polygon (regular polygon, star, isosceles triangle): the corners as drawn and the polygon of the anchors (the corners
  // moved out by the outer separation, mitred), both relative to the centre, and the anchors a shape names (`corner 2`).
  std::vector<P> outline, poly;
  std::map<std::string, P> named;
};

/** The polygon `v` (either way round) with every side moved out by `o`, the corners mitred: the border pgf's polygons
 *  keep their anchors on, at the radii `radius + o * csc(...)` its code works out one by one. */
std::vector<P> offsetPolygon(const std::vector<P>& v, double o) {
  const std::size_t n = v.size();
  double area = 0;
  for (std::size_t i = 0; i < n; i++) area += v[i].x * v[(i + 1) % n].y - v[(i + 1) % n].x * v[i].y;
  const double sign = area >= 0 ? 1.0 : -1.0;
  std::vector<P> out;
  for (std::size_t i = 0; i < n; i++) {
    const P a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n];
    const P d1 = unit(b - a), d2 = unit(c - b);
    const P n1{d1.y * sign, -d1.x * sign}, n2{d2.y * sign, -d2.x * sign};
    const double k = 1.0 + n1.x * n2.x + n1.y * n2.y;
    out.push_back(k > 1e-9 ? b + (n1 + n2) * (o / k) : b + n1 * o);
  }
  return out;
}

/** How far along `u` from the origin the polygon `poly` is met (the origin is inside it). */
double rayPolygon(const std::vector<P>& poly, P u) {
  double best = 1e30;
  const std::size_t n = poly.size();
  for (std::size_t i = 0; i < n; i++) {
    const P a = poly[i], e = poly[(i + 1) % n] - poly[i];
    const double den = u.x * e.y - u.y * e.x;
    if (std::fabs(den) < 1e-12) continue;
    const double t = (a.x * e.y - a.y * e.x) / den, s = (a.x * u.y - a.y * u.x) / den;
    if (t >= 0 && s >= -1e-9 && s <= 1 + 1e-9) best = std::min(best, t);
  }
  return best < 1e29 ? best : 0.0;
}

P bezier(P a, P b, P c, P d, double t) {
  const double u = 1 - t;
  return a * (u * u * u) + b * (3 * u * u * t) + c * (3 * u * t * t) + d * (t * t * t);
}

/** A segment as it was drawn, to put a node on it and to point an arrow along it. */
struct SegInfo {
  bool valid = false;
  bool cubic = false;
  P from, c1, c2, to;
  P at(double t) const { return cubic ? bezier(from, c1, c2, to, t) : from + (to - from) * t; }
  P direction(double t) const {
    if (!cubic) return unit(to - from);
    const double h = 1e-3;
    return unit(at(std::min(1.0, t + h)) - at(std::max(0.0, t - h)));
  }
};

struct TipGeom {
  std::vector<Sub> subs;     // in the tip's own frame: x along the path, y up
  bool fill = false, stroke = false;
  double lw = 0;
  int cap = CAP_BUTT, join = JOIN_MITER;
  double tipEnd = 0;         // the origin is this far behind the end of the path
  double lineEnd = 0;        // the shaft ends this far ahead of the origin
};

class Layout {
public:
  Layout(TikzAtom& pic, Env& env) : _pic(pic), _env(env) {
    _cm = Units::fsize(Units::getDimen("1cm"), env);
    _em = Units::fsize(UnitType::em, 1.f, env);
    _pt = Units::fsize(Units::getDimen("1pt"), env);
  }

  sptr<Box> run();

private:
  TikzAtom& _pic;
  Env& _env;
  double _cm = 0, _em = 0, _pt = 0;
  int _labelCount = 0;
  std::vector<Prim> _prims;
  std::map<std::string, Geom> _nodes;
  // The picture's extent, with a path's control points as pgf takes them.
  double _minx = 1e30, _maxx = -1e30, _miny = 1e30, _maxy = -1e30;
  // Clip paths: each one's region (a convex polygon, counter-clockwise; empty when it is not one), and the box of the
  // regions that are in force for the item being laid out -- what is drawn inside them counts towards the picture's
  // size only as far as they reach, as in pgf.
  std::map<int, std::vector<P>> _regions;
  bool _clipped = false;
  double _cx0 = 0, _cx1 = 0, _cy0 = 0, _cy1 = 0;

  double len(const Dimen& d) const { return d.isValid() ? Units::fsize(d, _env) : 0.0; }
  double len(const std::optional<Dimen>& d, double fallback) const { return d ? len(*d) : fallback; }

  void see(P p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;   // a shape of no size (an arc of 0 degrees) is nowhere
    if (_clipped) {
      p.x = std::min(std::max(p.x, _cx0), _cx1);
      p.y = std::min(std::max(p.y, _cy0), _cy1);
    }
    _minx = std::min(_minx, p.x);
    _maxx = std::max(_maxx, p.x);
    _miny = std::min(_miny, p.y);
    _maxy = std::max(_maxy, p.y);
  }

  Matrix matrixOf(const Style& st) const;
  color alpha(color c, double opacity) const;
  Prim primOf(const Style& st, bool stroke, bool fill) const;

  struct Where {
    P p;
    const Geom* node = nullptr;   // a node named without an anchor: the border towards the other end
  };
  bool resolve(const Coord& c, const Style& st, P base, Where& out);
  P anchorOf(const Geom& g, const std::string& anchor) const;
  P borderToward(const Geom& g, P target) const;

  void layoutItem(const PathItem& item);
  void layoutPath(const PathItem& item);
  void clipPrims();
  void clipOne(const Prim& p, const std::vector<P>& region, std::vector<Prim>& out) const;
  void layoutNodeMatrix(const NodeMatrix& gm, const Style& st);
  void layoutNode(const Node& n, const Style& st, P at, bool atGiven, const SegInfo* seg, double t);
  void placeLabel(const Style& ownerStyle, const Geom& owner, const Label& lb);
  void placeChildren(const Node& parent, const Geom& g);
  Geom makeGeom(const Node& n, const Style& st, sptr<Box>& box, P& textAt, const FitBox* fit) const;
  void nodeOutline(const Geom& g, const Style& st, Prim& prim) const;
  void polygonGeom(Geom& g, const Style& st, double hx, double hy, double minW, double minH) const;
  void connect(const PathItem& item, const PathOp& op, const Where& from, const Where& to, std::vector<Sub>& out,
               SegInfo& info, const Style& st);

  TipGeom tipGeom(const Tip& tip, double lw) const;
  void applyTips(const Style& st, std::vector<Sub>& subs, double lw, std::vector<Prim>& out);

  static void rounded(const std::vector<Sub>& in, double r, std::vector<Sub>& out);
  void braces(std::vector<Sub>& subs, const Style& st);
  static std::vector<P> flatten(const Sub& s, int steps = 16);
  static void shortenEnd(Sub& s, double d);
  static void shortenStart(Sub& s, double d);
  static P endDirection(const Sub& s);
  static P startDirection(const Sub& s);
  static void arcSegments(Sub& sub, P centre, double rx, double ry, double a0, double a1);
};

Matrix Layout::matrixOf(const Style& st) const {
  Matrix m;
  for (const XStep& s : st.transform) {
    Matrix t;
    switch (s.kind) {
      case XStep::Kind::shift:
        t.e = len(s.dx);
        t.f = len(s.dy);
        break;
      case XStep::Kind::xshift: t.e = len(s.dx); break;
      case XStep::Kind::yshift: t.f = len(s.dx); break;
      case XStep::Kind::scale: t.a = t.d = s.v; break;
      case XStep::Kind::xscale: t.a = s.v; break;
      case XStep::Kind::yscale: t.d = s.v; break;
      case XStep::Kind::rotate:
        t.a = t.d = std::cos(radians(s.v));
        t.b = std::sin(radians(s.v));
        t.c = -t.b;
        break;
    }
    m = m * t;
  }
  return m;
}

color Layout::alpha(color c, double opacity) const {
  if (opacity >= 1.0) return c;
  const int a = static_cast<int>(std::lround(color_a(c) * std::max(0.0, opacity)));
  return argb(a, static_cast<int>(color_r(c)), static_cast<int>(color_g(c)), static_cast<int>(color_b(c)));
}

Prim Layout::primOf(const Style& st, bool stroke, bool fill) const {
  Prim p;
  const color current = st.current.value_or(black);
  p.stroke = stroke;
  p.fill = fill;
  p.strokeCol = alpha(st.draw.mode == Paint::Mode::color ? st.draw.c : current, st.drawOpacity.value_or(1.f));
  p.fillCol = alpha(st.fill.mode == Paint::Mode::color ? st.fill.c : current, st.fillOpacity.value_or(1.f));
  p.lw = len(st.lineWidth, 0.4 * _pt);
  if (st.dash) {
    for (const Dimen& d : *st.dash) p.dash.push_back(len(d));
    p.phase = len(st.dashPhase, 0.0);
  }
  p.cap = st.cap.value_or(CAP_BUTT);
  p.join = st.join.value_or(JOIN_MITER);
  p.textCol = alpha(st.textColor.value_or(current), st.textOpacity.value_or(1.f));
  p.layer = st.layer.value_or(0);
  p.clips = st.clips;
  return p;
}

// --- coordinates --------------------------------------------------------------------------------

P Layout::borderToward(const Geom& g, P target) const {
  const P d = target - g.c;
  const double n = length(d);
  if (g.point || n < 1e-9) return g.c;
  const P u = d * (1.0 / n);
  if (!g.poly.empty()) return g.c + u * rayPolygon(g.poly, u);
  if (g.shape == "circle") return g.c + u * (g.hw + g.outer);
  // An ellipse and a diamond are given by the radii of their outer border: the point on the ray.
  if (g.shape == "ellipse") {
    const double rx = g.hw + g.outer, ry = g.hh + g.outer;
    return g.c + u * (1.0 / std::hypot(u.x / rx, u.y / ry));
  }
  if (g.shape == "diamond") {
    const double rx = g.hw + g.outer, ry = g.hh + g.outer;
    return g.c + u * (1.0 / (std::fabs(u.x) / rx + std::fabs(u.y) / ry));
  }
  if (g.shape == "rounded rectangle") {
    // The straight top or bottom between the corners, else the arc at the end, its radius grown by the outer sep.
    const double top = g.hh + g.outer, limit = g.hw - g.inset;
    if (std::fabs(u.y) > 1e-12) {
      const double t = top / std::fabs(u.y);
      if (std::fabs(t * u.x) <= limit) return g.c + u * t;
    }
    const P centre{(u.x < 0 ? -1.0 : 1.0) * (g.hw - g.arcR), 0};
    const double R = g.arcR + g.outer, b = u.x * centre.x + u.y * centre.y;
    return g.c + u * (b + std::sqrt(std::max(0.0, b * b - (centre.x * centre.x + centre.y * centre.y) + R * R)));
  }
  // A rectangle: the distance along `u` to the side it meets first.
  const double hw = g.hw + g.outer, hh = g.hh + g.outer;
  const double tx = std::fabs(u.x) > 1e-12 ? hw / std::fabs(u.x) : 1e30;
  const double ty = std::fabs(u.y) > 1e-12 ? hh / std::fabs(u.y) : 1e30;
  return g.c + u * std::min(tx, ty);
}

P Layout::anchorOf(const Geom& g, const std::string& anchor) const {
  if (anchor.empty() || anchor == "center") return g.c;
  if (g.point) return g.c;
  if (!g.poly.empty()) {
    // A polygon's own anchors (`corner 2`, `apex`), then the compass points as the border in that direction.
    const auto named = g.named.find(anchor);
    if (named != g.named.end()) return g.c + named->second;
    static const struct {
      const char* name;
      double dx, dy;
    } dirs[] = {{"north", 0, 1},  {"south", 0, -1},  {"east", 1, 0},       {"west", -1, 0},
                {"north east", 1, 1}, {"north west", -1, 1}, {"south east", 1, -1}, {"south west", -1, -1}};
    for (const auto& d : dirs) {
      if (anchor == d.name) return borderToward(g, g.c + P{d.dx, d.dy});
    }
  }
  const double hw = g.hw + g.outer, hh = g.hh + g.outer;
  const bool circle = g.shape == "circle";
  const double r = g.hw + g.outer;
  // A circle's corners are on the circle, at 45 degrees, an ellipse's at 0.707107 of its radii (pgf's
  // constant), a diamond's half way to its tips.
  const double k = circle || g.shape == "ellipse" ? 0.707107 : g.shape == "diamond" ? 0.5 : 1.0;
  const auto corner = [&](double sx, double sy) {
    if (circle) return g.c + P{sx * r * std::cos(kPi / 4), sy * r * std::sin(kPi / 4)};
    // A rounded rectangle's corners are where the straight side meets the arc.
    if (g.shape == "rounded rectangle") return g.c + P{sx * (g.hw - g.inset), sy * hh};
    return g.c + P{sx * hw * k, sy * hh * k};
  };
  const double ew = circle ? r : hw, ns = circle ? r : hh;
  if (anchor == "north") return g.c + P{0, ns};
  if (anchor == "south") return g.c + P{0, -ns};
  if (anchor == "east") return g.c + P{ew, 0};
  if (anchor == "west") return g.c + P{-ew, 0};
  if (anchor == "north east") return corner(1, 1);
  if (anchor == "north west") return corner(-1, 1);
  if (anchor == "south east") return corner(1, -1);
  if (anchor == "south west") return corner(-1, -1);
  if (anchor == "mid") return g.c;
  if (anchor == "base" || anchor == "text") return g.c + P{0, g.baseline};
  if (anchor == "mid east") return g.c + P{ew, 0};
  if (anchor == "mid west") return g.c + P{-ew, 0};
  if (anchor == "base east") return g.c + P{ew, g.baseline};
  if (anchor == "base west") return g.c + P{-ew, g.baseline};
  // An angle: the border on the ray from the centre.
  char* end = nullptr;
  const double a = std::strtod(anchor.c_str(), &end);
  if (end != anchor.c_str() && *end == '\0') return borderToward(g, g.c + polar(a, 1.0));
  return g.c;
}

bool Layout::resolve(const Coord& c, const Style& st, P base, Where& out) {
  const Matrix m = matrixOf(st);
  out.node = nullptr;
  switch (c.kind) {
    case Coord::Kind::xy: {
      const P v{len(c.x), len(c.y)};
      out.p = c.relative ? base + m.linear(v) : m.apply(v);
      return true;
    }
    case Coord::Kind::polar: {
      const double r = len(c.r);
      const P v = c.r2.isValid() ? P{r * std::cos(radians(c.angle)), len(c.r2) * std::sin(radians(c.angle))}
                                 : polar(c.angle, r);
      out.p = c.relative ? base + m.linear(v) : m.apply(v);
      return true;
    }
    case Coord::Kind::cross: {
      Where a, b;
      if (!resolve(*c.first, st, base, a) || !resolve(*c.second, st, base, b)) return false;
      out.p = c.xFromFirst ? P{a.p.x, b.p.y} : P{b.p.x, a.p.y};
      return true;
    }
    case Coord::Kind::node: {
      const auto it = _nodes.find(c.name);
      if (it == _nodes.end()) return false;
      const Geom& g = it->second;
      if (c.anchor.empty()) {
        out.p = g.c;
        out.node = &g;
      } else {
        out.p = anchorOf(g, c.anchor);
      }
      return true;
    }
    case Coord::Kind::calc: {
      // tikzlibrarycalc: a sum of signed, scaled points, each taken through its `!` steps in turn. Every point
      // in it is a point of the canvas already, so the sum is not transformed again.
      if (c.calc == nullptr) return false;
      P total{0, 0};
      for (const CalcTerm& term : c.calc->terms) {
        Where w;
        if (!resolve(term.first, st, base, w)) return false;
        P p = w.p;
        for (const CalcStep& step : term.steps) {
          Where target;
          if (!resolve(step.to, st, base, target)) return false;
          P b = target.p;
          if (step.rotated) {
            // The target is turned about the point so far before it is used.
            const P d = b - p;
            const double a = radians(step.angle);
            b = p + P{d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)};
          }
          if (step.kind == CalcStep::Kind::fraction) {
            p = p * (1.0 - step.fraction) + b * step.fraction;
          } else if (step.kind == CalcStep::Kind::distance) {
            p = p + unit(b - p) * len(step.distance);
          } else {
            Where via;
            if (!resolve(step.via, st, base, via)) return false;
            const P u = unit(b - p);
            const P q = via.p - p;
            p = p + u * (q.x * u.x + q.y * u.y);
          }
        }
        total = total + p * term.factor;
      }
      out.p = total;
      return true;
    }
  }
  return false;
}

// --- path pieces --------------------------------------------------------------------------------

std::vector<P> Layout::flatten(const Sub& s, int steps) {
  std::vector<P> pts{s.start};
  P cur = s.start;
  for (const Seg& g : s.segs) {
    if (!g.cubic) {
      pts.push_back(g.to);
    } else {
      for (int k = 1; k <= steps; k++) pts.push_back(bezier(cur, g.c1, g.c2, g.to, static_cast<double>(k) / steps));
    }
    cur = g.to;
  }
  if (s.closed && !pts.empty()) pts.push_back(s.start);
  return pts;
}

void Layout::arcSegments(Sub& sub, P centre, double rx, double ry, double a0, double a1) {
  // Cubic arcs of at most 90 degrees, from angle a0 to a1 (degrees) on the ellipse.
  // (At most ten turns: an arc of 36000000 degrees was 400000 curves and a minute of layout.)
  const double span = std::min(3600.0, std::max(-3600.0, a1 - a0));
  const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(span) / 90.0 - 1e-9)));
  const double step = radians(span / n);
  const double k = 4.0 / 3.0 * std::tan(step / 4.0);
  for (int i = 0; i < n; i++) {
    const double t0 = radians(a0) + step * i, t1 = t0 + step;
    const P p0{std::cos(t0), std::sin(t0)}, p3{std::cos(t1), std::sin(t1)};
    const P p1{p0.x - k * p0.y, p0.y + k * p0.x}, p2{p3.x + k * p3.y, p3.y - k * p3.x};
    const auto map = [&](P q) { return centre + P{q.x * rx, q.y * ry}; };
    Seg g;
    g.cubic = true;
    g.c1 = map(p1);
    g.c2 = map(p2);
    g.to = map(p3);
    sub.segs.push_back(g);
  }
}

/** pgflibrarydecorations.pathreplacing's `brace`, for a path that is one straight line: from one end to the other,
 *  a bump of `amplitude` (2.5pt) above the line, a point at `aspect` (0.5) of the way, the bumps half the amplitude
 *  high where they meet the line; `mirror` puts it on the other side and `raise` moves it away. The curves are the
 *  decoration's own, in its frame (x along the path, y to its left). */
void Layout::braces(std::vector<Sub>& subs, const Style& st) {
  if (subs.size() != 1 || subs[0].segs.size() != 1 || subs[0].segs[0].cubic || subs[0].closed) {
    return;  // the reader said so, once
  }
  const Sub in = subs[0];
  const P p0 = in.start, p1 = in.segs[0].to;
  const double distance = length(p1 - p0);
  if (distance < 1e-9) return;
  const double amp = len(st.amplitude, 2.5 * _pt), aspect = st.braceAspect.value_or(0.5f), raise = len(st.raise, 0.0);
  const double sign = st.mirror ? -1.0 : 1.0;
  // pgf's own bump widths: the first up to half the aspect of the path, the second likewise.
  double yc = aspect * distance;
  yc = 2 * amp > yc ? 0.5 * yc : amp;
  double xc = aspect * distance - distance;
  xc = -2 * amp < xc ? -0.5 * xc : amp;
  const P u = unit(p1 - p0), n = left(u);
  const auto at = [&](double x, double y) { return p0 + u * x + n * (sign * (y + raise)); };
  Sub out;
  out.start = at(0, 0);
  const auto curve = [&](double x1, double y1, double x2, double y2, double x3, double y3) {
    Seg g;
    g.cubic = true;
    g.c1 = at(x1, y1);
    g.c2 = at(x2, y2);
    g.to = at(x3, y3);
    out.segs.push_back(g);
    see(g.c1);
    see(g.c2);
    see(g.to);
  };
  const auto line = [&](double x, double y) {
    Seg g;
    g.to = at(x, y);
    out.segs.push_back(g);
    see(g.to);
  };
  const double mid = aspect * distance;
  curve(.15 * yc, .3 * amp, .5 * yc, .5 * amp, yc, .5 * amp);
  line(mid - yc, .5 * amp);
  curve(mid - .5 * yc, .5 * amp, mid - .15 * yc, .7 * amp, mid, amp);
  curve(mid + .15 * xc, .7 * amp, mid + .5 * xc, .5 * amp, mid + xc, .5 * amp);
  line(distance - xc, .5 * amp);
  curve(distance - .5 * xc, .5 * amp, distance - .15 * xc, .3 * amp, distance, 0);
  see(out.start);
  subs = {out};
}

// pgf's rounded corners: at each corner between two lines the path leaves the first `r` before it and
// takes a cubic to the point `r` after it on the second, its controls 0.5523 of the way to the corner.
void Layout::rounded(const std::vector<Sub>& in, double r, std::vector<Sub>& out) {
  for (const Sub& s : in) {
    // Only straight lines have corners to round.
    bool straight = true;
    for (const Seg& g : s.segs) straight = straight && !g.cubic;
    if (!straight || s.segs.size() < 2) {
      out.push_back(s);
      continue;
    }
    std::vector<P> v{s.start};
    for (const Seg& g : s.segs) v.push_back(g.to);
    const bool closed = s.closed;
    if (closed && length(v.front() - v.back()) < 1e-9) v.pop_back();
    const int n = static_cast<int>(v.size());
    const auto along = [&](P from, P toward) {
      const double d = length(toward - from);
      return d < 1e-9 ? from : from + (toward - from) * (std::min(r, d / 2.0) / d);
    };
    Sub o;
    if (closed) {
      for (int i = 0; i < n; i++) {
        const P corner = v[i];
        const P s0 = along(corner, v[(i + n - 1) % n]), e = along(corner, v[(i + 1) % n]);
        Seg curve;
        curve.cubic = true;
        curve.c1 = s0 + (corner - s0) * kKappa;
        curve.c2 = e + (corner - e) * kKappa;
        curve.to = e;
        if (i == 0) {
          o.start = s0;
        } else {
          Seg line;
          line.to = s0;
          o.segs.push_back(line);
        }
        o.segs.push_back(curve);
      }
      o.closed = true;
    } else {
      o.start = v[0];
      for (int i = 1; i < n - 1; i++) {
        const P corner = v[i];
        const P s0 = along(corner, v[i - 1]), e = along(corner, v[i + 1]);
        Seg line;
        line.to = s0;
        o.segs.push_back(line);
        Seg curve;
        curve.cubic = true;
        curve.c1 = s0 + (corner - s0) * kKappa;
        curve.c2 = e + (corner - e) * kKappa;
        curve.to = e;
        o.segs.push_back(curve);
      }
      Seg last;
      last.to = v[n - 1];
      o.segs.push_back(last);
    }
    out.push_back(std::move(o));
  }
}

P Layout::endDirection(const Sub& s) {
  if (s.segs.empty()) return {1, 0};
  const Seg& g = s.segs.back();
  const P from = s.segs.size() > 1 ? s.segs[s.segs.size() - 2].to : s.start;
  if (!g.cubic) return unit(g.to - from);
  if (length(g.to - g.c2) > 1e-9) return unit(g.to - g.c2);
  if (length(g.to - g.c1) > 1e-9) return unit(g.to - g.c1);
  return unit(g.to - from);
}

P Layout::startDirection(const Sub& s) {
  if (s.segs.empty()) return {1, 0};
  const Seg& g = s.segs.front();
  if (!g.cubic) return unit(g.to - s.start);
  if (length(g.c1 - s.start) > 1e-9) return unit(g.c1 - s.start);
  if (length(g.c2 - s.start) > 1e-9) return unit(g.c2 - s.start);
  return unit(g.to - s.start);
}

/** The path cut `d` short at its end: the last segment, a line or a curve, stops that far before it did. */
void Layout::shortenEnd(Sub& s, double d) {
  if (s.segs.empty() || d <= 1e-9) return;
  Seg& g = s.segs.back();
  const P from = s.segs.size() > 1 ? s.segs[s.segs.size() - 2].to : s.start;
  if (!g.cubic) {
    const double n = length(g.to - from);
    if (n > d) g.to = g.to - unit(g.to - from) * d;
    return;
  }
  // Where on the curve is it `d` from the end? Then the curve up to there.
  double t = 1.0;
  for (int k = 63; k >= 0; k--) {
    const double u = static_cast<double>(k) / 64.0;
    if (length(bezier(from, g.c1, g.c2, g.to, u) - g.to) >= d) {
      // Refine between u and the next sample.
      double lo = u, hi = u + 1.0 / 64.0;
      for (int i = 0; i < 24; i++) {
        const double mid = (lo + hi) / 2;
        (length(bezier(from, g.c1, g.c2, g.to, mid) - g.to) >= d ? lo : hi) = mid;
      }
      t = (lo + hi) / 2;
      break;
    }
  }
  // de Casteljau at t: the first part.
  const P a = from + (g.c1 - from) * t, b = g.c1 + (g.c2 - g.c1) * t, c = g.c2 + (g.to - g.c2) * t;
  const P ab = a + (b - a) * t, bc = b + (c - b) * t;
  const P p = ab + (bc - ab) * t;
  g.c1 = a;
  g.c2 = ab;
  g.to = p;
}

void Layout::shortenStart(Sub& s, double d) {
  if (s.segs.empty() || d <= 1e-9) return;
  Seg& g = s.segs.front();
  if (!g.cubic) {
    const double n = length(g.to - s.start);
    if (n > d) s.start = s.start + unit(g.to - s.start) * d;
    return;
  }
  double t = 0.0;
  for (int k = 1; k <= 64; k++) {
    const double u = static_cast<double>(k) / 64.0;
    if (length(bezier(s.start, g.c1, g.c2, g.to, u) - s.start) >= d) {
      double lo = u - 1.0 / 64.0, hi = u;
      for (int i = 0; i < 24; i++) {
        const double mid = (lo + hi) / 2;
        (length(bezier(s.start, g.c1, g.c2, g.to, mid) - s.start) >= d ? hi : lo) = mid;
      }
      t = (lo + hi) / 2;
      break;
    }
  }
  // The part from t on: de Casteljau, taking the second half.
  const P a = s.start + (g.c1 - s.start) * t, b = g.c1 + (g.c2 - g.c1) * t, c = g.c2 + (g.to - g.c2) * t;
  const P ab = a + (b - a) * t, bc = b + (c - b) * t;
  const P p = ab + (bc - ab) * t;
  s.start = p;
  g.c1 = bc;
  g.c2 = c;
}

// --- arrow tips ---------------------------------------------------------------------------------

/** pgf's tips, in their own frame (x along the path, y up), as the arrows libraries draw them. The
 *  formulas are theirs (pgflibraryarrows.meta.code.tex, pgfcorearrows.code.tex), checked against
 *  pdfTeX's output at four line widths. `lw` is the path's line width. */
TipGeom Layout::tipGeom(const Tip& tip, double lw) const {
  TipGeom g;
  const double pt = _pt;
  const double lwpt = lw / pt;
  const auto size = [&](const std::optional<Dimen>& d, double fallback) { return d ? len(*d) : fallback; };
  Sub s;
  const auto line = [&](Sub& sub, P to) {
    Seg q;
    q.to = to;
    sub.segs.push_back(q);
  };
  const auto cubicTo = [&](Sub& sub, P c1, P c2, P to) {
    Seg q;
    q.cubic = true;
    q.c1 = c1;
    q.c2 = c2;
    q.to = to;
    sub.segs.push_back(q);
  };
  const double sc = tip.scale;
  switch (tip.kind) {
    case Tip::Kind::none: break;
    case Tip::Kind::to: {
      // Computer Modern Rightarrow: length 1.6pt + 2.2 lw, width 2.096774 of it, stroked round.
      const double L = size(tip.length, (1.6 + 2.2 * lwpt) * pt) * sc;
      const double W = size(tip.width, 2.096774 * L / sc) * sc;
      const double lwA = size(tip.lineWidth, lw);
      const double a = L - lwA, b = W - lwA;
      s.start = {-a, 0.5 * b};
      cubicTo(s, {-0.81731 * a, 0.2 * b}, {-0.41019 * a, 0.05833333 * b}, {0, 0});
      cubicTo(s, {-0.41019 * a, -0.05833333 * b}, {-0.81731 * a, -0.2 * b}, {-a, -0.5 * b});
      g.stroke = true;
      g.lw = lwA;
      g.cap = CAP_ROUND;
      g.join = JOIN_ROUND;
      g.tipEnd = 0.5 * lwA;
      g.lineEnd = -0.5 * lwA;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::latex: {
      const double d = (0.28 + 0.3 * lwpt) * pt;
      s.start = {9 * d, 0};
      cubicTo(s, {6.3333 * d, 0.5 * d}, {2 * d, 2 * d}, {-d, 3.75 * d});
      line(s, {-d, -3.75 * d});
      cubicTo(s, {2 * d, -2 * d}, {6.3333 * d, -0.5 * d}, {9 * d, 0});
      s.closed = true;
      g.fill = true;
      g.tipEnd = 9 * d;
      g.lineEnd = 0;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::stealth: {
      const double d = (0.28 + 0.3 * lwpt) * pt;
      s.start = {5 * d, 0};
      line(s, {-3 * d, 4 * d});
      line(s, {0, 0});
      line(s, {-3 * d, -4 * d});
      s.closed = true;
      g.fill = true;
      g.tipEnd = 5 * d;
      g.lineEnd = 0;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::newLatex: {
      const double L = size(tip.length, (3 + 4.5 * lwpt) * pt) * sc;
      const double W = size(tip.width, 0.75 * L / sc) * sc;
      double lwA = size(tip.lineWidth, lw);
      if (0.2 * L < lwA) lwA = 0.2 * L;
      const double q = L / W;
      const double xc = lwA * std::sqrt(9 * q * q + 1);
      const double a = L - 0.5 * xc - 0.5 * lwA;
      // The back tip's miter: from the normalised vector (.3 L, .2333 W).
      const P nv = unit({0.3 * L, 0.2333333 * W});
      const double ratio = (nv.y + 1.0) / nv.x;
      const double b = 0.5 * W - 0.5 * ratio * lwA;
      s.start = {a, 0};
      cubicTo(s, {0.877192 * a, 0.077922 * b}, {0.337381 * a, 0.51948 * b}, {0, b});
      line(s, {0, -b});
      cubicTo(s, {0.337381 * a, -0.51948 * b}, {0.877192 * a, -0.077922 * b}, {a, 0});
      s.closed = true;
      g.fill = lwA > 0 && !tip.open ? true : !tip.open;
      g.stroke = lwA > 0;
      g.lw = lwA;
      g.join = tip.round ? JOIN_ROUND : JOIN_MITER;
      g.tipEnd = tip.round ? a + 0.5 * lwA : L - 0.5 * lwA;
      g.lineEnd = 0;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::newStealth:
    case Tip::Kind::triangleMeta: {
      const bool triangle = tip.kind == Tip::Kind::triangleMeta;
      const double L = size(tip.length, triangle ? (2.7 + 3.6 * lwpt) * pt : (3 + 4.5 * lwpt) * pt) * sc;
      const double W = size(tip.width, (triangle ? 2 * std::tan(radians(30)) : 0.75) * L / sc) * sc;
      const double I = triangle ? 0 : size(tip.inset, 0.325 * L / sc) * sc;
      double lwA = size(tip.lineWidth, lw);
      if (0.25 * (L - I) < lwA) lwA = 0.25 * (L - I);
      const double q = L / W;
      const double xc = 0.5 * lwA * std::sqrt(4 * q * q + 1);
      const double A1 = degrees(std::atan2(L, 0.5 * W)), A2 = degrees(std::atan2(I, 0.5 * W));
      const double half = 0.5 * (A1 - A2);
      const double yc0 = 0.5 * lwA / std::tan(radians(half));
      const double ang = A2 + half;
      double ya = std::sin(radians(ang)) * yc0;
      const double yb = std::cos(radians(ang)) * yc0;
      if (I == 0) ya = 0.5 * lwA;
      const double q2 = I / W;
      const double ycInset = 0.5 * lwA * std::sqrt(4 * q2 * q2 + 1);
      const double inner = L - xc - ya;
      const double h = 0.5 * W - yb;
      const double inset = I + ycInset;
      s.start = {inner + ya, 0};
      line(s, {ya, h});
      line(s, {inset, 0});
      line(s, {ya, -h});
      s.closed = true;
      g.fill = !tip.open;
      g.stroke = lwA > 0;
      g.lw = lwA;
      g.join = tip.round ? JOIN_ROUND : JOIN_MITER;
      g.tipEnd = tip.round ? inner + ya + 0.5 * lwA : L;
      g.lineEnd = I + ycInset - 0.25 * lwA;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::triangle45: {
      // pgflibraryarrows' `triangle 45`, fitted: the numbers are linear in the line width.
      const double bx = (4.3525 + 2.1763 * lwpt) * pt, by = (1.9536 + 0.9769 * lwpt) * pt;
      const double ax = (0.25 + 0.125 * lwpt) * pt;
      s.start = {-bx, by};
      line(s, {ax, 0});
      line(s, {-bx, -by});
      s.closed = true;
      g.fill = !tip.open;
      g.stroke = true;
      g.lw = lw;
      g.tipEnd = (0.2489 + 1.405 * lwpt) * pt;
      g.lineEnd = 0;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::bar: {
      // `|` and Bar: a stroke across the path, half 1.5pt + 2 lw high, butt capped.
      const double h = (1.5 + 2.0 * lwpt) * pt * sc;
      s.start = {0, h};
      line(s, {0, -h});
      g.stroke = true;
      g.lw = lw;
      g.cap = CAP_BUTT;
      g.join = JOIN_MITER;
      g.tipEnd = 0.5 * lw;
      g.lineEnd = -0.25 * lw;
      g.subs.push_back(s);
      break;
    }
    case Tip::Kind::circle: {
      const double r = (1.1968 + 1.0958 * lwpt) * pt * sc;
      const P centre{r + 0.5 * lw, 0};
      s.start = centre + P{r, 0};
      arcSegments(s, centre, r, r, 0, 360);
      s.closed = true;
      g.fill = !tip.open;
      g.stroke = true;
      g.lw = lw;
      g.join = JOIN_MITER;
      g.tipEnd = 2 * r + lw;
      g.lineEnd = 0.5 * lw;
      g.subs.push_back(s);
      break;
    }
  }
  return g;
}

/** The tips of a style on the first and last subpath, the shaft cut short for them. */
void Layout::applyTips(const Style& st, std::vector<Sub>& subs, double lw, std::vector<Prim>& out) {
  if (subs.empty()) return;
  const auto resolveTip = [&](const std::optional<Tip>& t) -> std::optional<Tip> {
    if (!t || (t->kind == Tip::Kind::none && !t->defaultTip)) return std::nullopt;
    if (!t->defaultTip) return t;
    // `>` is whatever `>=` says by the time the path is drawn, the default `to` before that.
    Tip chosen;
    if (st.defaultTip.empty() || !tipByName(st.defaultTip, chosen)) {
      chosen = Tip();
      chosen.kind = Tip::Kind::to;
    }
    return chosen;
  };
  struct End {
    Tip tip;
    bool start;
  };
  std::vector<End> ends;
  if (const auto t = resolveTip(st.tipEnd)) ends.push_back({*t, false});
  if (const auto t = resolveTip(st.tipStart)) ends.push_back({*t, true});
  const color current = st.current.value_or(black);
  // `shorten >=` and `shorten <=`: the path stops short of where it was going, before its tips.
  if (!subs.back().closed) shortenEnd(subs.back(), len(st.shortenEnd, 0.0));
  if (!subs.front().closed) shortenStart(subs.front(), len(st.shortenStart, 0.0));
  for (const End& e : ends) {
    Sub& sub = e.start ? subs.front() : subs.back();
    if (sub.closed || sub.segs.empty()) continue;
    const TipGeom tg = tipGeom(e.tip, lw);
    const P end = e.start ? sub.start : sub.segs.back().to;
    const P u = e.start ? startDirection(sub) * -1.0 : endDirection(sub);
    const P origin = end - u * tg.tipEnd;
    const double shorten = tg.tipEnd - tg.lineEnd;
    if (e.start) {
      shortenStart(sub, shorten);
    } else {
      shortenEnd(sub, shorten);
    }
    Prim p;
    p.stroke = tg.stroke;
    p.fill = tg.fill;
    const color c = alpha(st.draw.mode == Paint::Mode::color ? st.draw.c : current, st.drawOpacity.value_or(1.f));
    p.strokeCol = c;
    p.fillCol = c;
    p.lw = tg.lw;
    p.layer = st.layer.value_or(0);
    p.clips = st.clips;
    p.cap = tg.cap;
    p.join = tg.join;
    const P n = left(u);
    const auto map = [&](P q) { return origin + u * q.x + n * q.y; };
    for (const Sub& ts : tg.subs) {
      Sub o;
      o.start = map(ts.start);
      o.closed = ts.closed;
      for (const Seg& sg : ts.segs) {
        Seg r;
        r.cubic = sg.cubic;
        r.c1 = sg.cubic ? map(sg.c1) : P();
        r.c2 = sg.cubic ? map(sg.c2) : P();
        r.to = map(sg.to);
        o.segs.push_back(r);
      }
      p.subs.push_back(std::move(o));
    }
    out.push_back(std::move(p));
  }
}

// --- nodes --------------------------------------------------------------------------------------

Geom Layout::makeGeom(const Node& n, const Style& st, sptr<Box>& box, P& textAt, const FitBox* fit) const {
  Geom g;
  g.shape = st.shape.empty() ? "rectangle" : st.shape;
  // The node's own `scale`: its text, inner separation and minimum size are that much larger; its line is not (pdfTeX: a
  // scale=2 node of one letter is 24.197bp across, two of 11.899 and the line once).
  const double sx = st.nodeScaleX, sy = st.nodeScaleY;
  const bool scaled = sx != 1.0 || sy != 1.0;
  box = n.content != nullptr ? (scaled ? sptrOf<ScaleAtom>(n.content, static_cast<float>(sx), static_cast<float>(sy))->createBox(_env)
                                       : n.content->createBox(_env))
                             : nullptr;
  // A fitted node's text box is the box of the points: that wide, that high.
  const double tw = fit ? fit->width : box ? box->_width : 0.0, th = fit ? fit->height : box ? box->_height : 0.0,
               td = fit ? 0.0 : box ? box->_depth : 0.0;
  if (g.shape == "coordinate") {
    g.point = true;
    g.baseline = 0;
    textAt = {0, 0};
    return g;
  }
  const double ix = sx * len(st.innerX, 0.3333 * _em), iy = sy * len(st.innerY, 0.3333 * _em);
  const double lw = len(st.lineWidth, 0.4 * _pt);
  g.outer = len(st.outerSep, 0.5 * lw);
  const double minW = sx * len(st.minWidth, 0.0), minH = sy * len(st.minHeight, 0.0);
  const double w0 = tw + 2 * ix, h0 = th + td + 2 * iy;
  if (g.shape == "circle") {
    const double r = std::max({0.5 * std::hypot(w0, h0), 0.5 * minW, 0.5 * minH});
    g.hw = g.hh = r;
  } else if (g.shape == "ellipse") {
    // pgf: the ellipse through the corners of the box (1.4142136 of its half sides), at least half the minimum size.
    g.hw = std::max(1.4142136 * 0.5 * w0, 0.5 * minW);
    g.hh = std::max(1.4142136 * 0.5 * h0, 0.5 * minH);
  } else if (g.shape == "rounded rectangle") {
    // pgf's convex ends: a circular arc of `arc length` degrees (180: a semicircle) over the whole height, the
    // text pushed in by the chord it leaves free, wider where a minimum width asks or the two arcs would meet.
    const double halfArc = radians(0.5 * st.arcLength.value_or(180.f));
    const double H = std::max(0.5 * h0, 0.5 * minH), htw = 0.5 * tw, hth = 0.5 * (th + td);
    const double r = H / std::sin(halfArc);
    const double chord = r * (1.0 - std::cos(std::asin(std::min(1.0, hth / r))));
    double xoffset = ix;
    const double natural = 2.0 * (htw + ix) + 2.0 * chord;
    if (natural < minW) xoffset = 0.5 * (minW - natural);
    if (r - chord > htw + xoffset) xoffset = (r - chord) - (htw + xoffset) + xoffset;
    g.arcR = r;
    g.inset = r * (1.0 - std::cos(halfArc));
    g.halfArc = degrees(halfArc);
    g.hw = htw + xoffset + chord;
    g.hh = H;
  } else if (g.shape == "diamond") {
    // pgf: the tips are at the half box widened by the other side times the aspect.
    const double aspect = st.aspect.value_or(1.f) > 0 ? st.aspect.value_or(1.f) : 1.0;
    g.hw = std::max(0.5 * w0 + aspect * 0.5 * h0, 0.5 * minW);
    g.hh = std::max(0.5 * w0 / aspect + 0.5 * h0, 0.5 * minH);
  } else if (g.shape == "regular polygon" || g.shape == "star" || g.shape == "isosceles triangle") {
    polygonGeom(g, st, 0.5 * tw + ix, 0.5 * (th + td) + iy, minW, minH);
  } else {
    g.hw = 0.5 * std::max(w0, minW);
    g.hh = 0.5 * std::max(h0, minH);
  }
  // The text box is centred in the shape, its baseline below its middle by half its height less its depth.
  g.baseline = -0.5 * (th - td);
  textAt = {-0.5 * tw, g.baseline};
  if (fit) {
    // tikzlibraryfit gives the text box the height of the points and `text badly centered`: the text is centred
    // across it, its first baseline half its depth above the middle (pdfTeX, tools/tikz-ref f07).
    const double cw = box ? box->_width : 0.0, cd = box ? box->_depth : 0.0;
    g.baseline = 0.5 * cd;
    textAt = {-0.5 * cw, g.baseline};
  }
  return g;
}

/** The shapes that are polygons (pgflibraryshapes.geometric): `hx` and `hy` are half the text box and its inner separation. The
 *  corners are read from the library's own radii; the anchors' polygon is the corners moved out by the outer separation. */
void Layout::polygonGeom(Geom& g, const Style& st, double hx, double hy, double minW, double minH) const {
  const double rotate = st.borderRotate.value_or(0.f);
  const double m = std::max(hx, hy);
  const double minimum = 0.5 * std::max(minW, minH);
  std::vector<P> corners;
  if (g.shape == "regular polygon") {
    // The incircle is 1.41421 of the half box; the corners lie at its secant, and a corner is up for an odd number of
    // sides, half a step round for an even one.
    const int n = st.polySides.value_or(5);
    const double step = 360.0 / n;
    const double R = std::max(1.41421 * m / std::cos(radians(180.0 / n)), minimum);
    const double start = (n % 2 != 0 ? 90.0 : 90.0 - 0.5 * step) + rotate;
    for (int k = 0; k < n; k++) corners.push_back(polar(start + k * step, R));
  } else if (g.shape == "star") {
    const int n = st.starPoints.value_or(5);
    const double step = 180.0 / n;
    double inner = 1.41421 * m;
    const double height = len(st.starHeight, 0.5 * _cm), ratio = st.starRatio.value_or(1.5);
    double outer = st.starByHeight ? inner + height : ratio * inner;
    if (outer < minimum) {
      // The outer radius grew to the minimum size: the inner follows by the ratio, or the point height.
      outer = minimum;
      inner = st.starByHeight ? outer - height : outer / ratio;
    }
    for (int k = 0; k < 2 * n; k++) corners.push_back(polar(90.0 + rotate + k * step, k % 2 == 0 ? outer : inner));
  } else {
    // An isosceles triangle, its apex right unless `shape border rotate` turns it. By default it is the smallest round the
    // text box (the turn is then a quarter turn at most, the box's sides swapped); with `shape border uses incircle` it is round
    // the circle that holds the text.
    const double half = 0.5 * st.apexAngle.value_or(45.f);
    const double sinH = std::sin(radians(half)), cosH = std::cos(radians(half)), tanH = std::tan(radians(half));
    double x = hx, y = hy, turn = rotate, xa = 0, ya = 0;
    if (st.borderIncircle) {
      x = 1.414213 * m;
      xa = x + x / sinH;
      ya = tanH * xa;
    } else {
      double r = std::fmod(rotate, 360.0);
      if (r < 0) r += 360.0;
      turn = std::floor((r + 45.0) / 90.0) * 90.0;
      if (turn == 90.0 || turn == 270.0) std::swap(x, y);
      xa = 2.0 * x;
      ya = tanH * xa;
      xa += y / tanH;
      ya += y;
    }
    if (ya < 0.5 * minW) {
      ya = 0.5 * minW;
      xa = ya / tanH;
    }
    if (xa < minH) {
      xa = minH;
      ya = tanH * xa;
    }
    // From the centre of the text back to the base.
    const double base = st.borderIncircle ? xa * sinH / (1.0 + sinH) : (cosH * (ya - y) - 2.0 * sinH * x) / (1.0 + sinH) + x;
    const double c = std::cos(radians(turn)), s = std::sin(radians(turn));
    for (const P& p : {P{xa - base, 0.0}, P{-base, ya}, P{-base, -ya}}) corners.push_back(P{c * p.x - s * p.y, s * p.x + c * p.y});
  }
  g.outline = corners;
  g.poly = offsetPolygon(corners, g.outer);
  double ex = 0, ey = 0;
  for (const P& p : g.poly) {
    ex = std::max(ex, std::fabs(p.x));
    ey = std::max(ey, std::fabs(p.y));
  }
  g.hw = std::max(0.0, ex - g.outer);
  g.hh = std::max(0.0, ey - g.outer);
  const auto side = [&](std::size_t i) { return (g.poly[i] + g.poly[(i + 1) % g.poly.size()]) * 0.5; };
  if (g.shape == "regular polygon") {
    for (std::size_t k = 0; k < g.poly.size(); k++) {
      g.named["corner " + std::to_string(k + 1)] = g.poly[k];
      g.named["side " + std::to_string(k + 1)] = side(k);
    }
  } else if (g.shape == "star") {
    for (std::size_t k = 0; k + 1 < g.poly.size(); k += 2) {
      g.named["outer point " + std::to_string(k / 2 + 1)] = g.poly[k];
      g.named["inner point " + std::to_string(k / 2 + 1)] = g.poly[k + 1];
    }
  } else {
    g.named["apex"] = g.poly[0];
    g.named["left corner"] = g.poly[1];
    g.named["right corner"] = g.poly[2];
    g.named["left side"] = side(0);
    g.named["lower side"] = side(1);
    g.named["right side"] = side(2);
  }
}

void Layout::nodeOutline(const Geom& g, const Style& st, Prim& prim) const {
  Sub s;
  if (!g.outline.empty()) {
    s.start = g.c + g.outline[0];
    for (std::size_t k = 1; k < g.outline.size(); k++) {
      Seg l;
      l.to = g.c + g.outline[k];
      s.segs.push_back(l);
    }
    s.closed = true;
    const double r = len(st.rounded, 0.0);
    if (r > 1e-9) {
      rounded({s}, r, prim.subs);
    } else {
      prim.subs.push_back(s);
    }
    return;
  }
  if (g.shape == "circle") {
    s.start = g.c + P{g.hw, 0};
    arcSegments(s, g.c, g.hw, g.hw, 0, 360);
    s.closed = true;
    prim.subs.push_back(s);
    return;
  }
  if (g.shape == "ellipse") {
    s.start = g.c + P{g.hw, 0};
    arcSegments(s, g.c, g.hw, g.hh, 0, 360);
    s.closed = true;
    prim.subs.push_back(s);
    return;
  }
  if (g.shape == "rounded rectangle") {
    const double a = g.halfArc;
    const double xe = g.hw - g.inset;
    s.start = g.c + P{0, g.hh};
    Seg top;
    top.to = g.c + P{xe, g.hh};
    s.segs.push_back(top);
    arcSegments(s, g.c + P{g.hw - g.arcR, 0}, g.arcR, g.arcR, a, -a);
    Seg bottom;
    bottom.to = g.c + P{-xe, -g.hh};
    s.segs.push_back(bottom);
    arcSegments(s, g.c + P{-(g.hw - g.arcR), 0}, g.arcR, g.arcR, 180 + a, 180 - a);
    s.closed = true;
    prim.subs.push_back(s);
    return;
  }
  if (g.shape == "diamond") {
    // pgf draws the outline in from the outer border by 1.414213 of the outer separation.
    const double x = g.hw + g.outer - 1.414213 * g.outer, y = g.hh + g.outer - 1.414213 * g.outer;
    s.start = g.c + P{x, 0};
    Seg a, b, c;
    a.to = g.c + P{0, y};
    b.to = g.c + P{-x, 0};
    c.to = g.c + P{0, -y};
    s.segs = {a, b, c};
    s.closed = true;
    prim.subs.push_back(s);
    return;
  }
  const P a = g.c + P{-g.hw, -g.hh}, b = g.c + P{g.hw, -g.hh}, c = g.c + P{g.hw, g.hh}, d = g.c + P{-g.hw, g.hh};
  s.start = a;
  Seg l1, l2, l3;
  l1.to = b;
  l2.to = c;
  l3.to = d;
  s.segs = {l1, l2, l3};
  s.closed = true;
  const double r = len(st.rounded, 0.0);
  if (r > 1e-9) {
    rounded({s}, r, prim.subs);
  } else {
    prim.subs.push_back(s);
  }
}

/** A node put on the picture: its size, then where it goes, its shape and its text. */
void Layout::layoutNode(const Node& n, const Style& st, P at, bool atGiven, const SegInfo* seg, double t) {
  sptr<Box> box;
  P textAt;
  // `fit=(a)(b)`: the box of the points, a node's four border anchors for a node named alone.
  std::optional<FitBox> fit;
  if (!st.fit.empty()) {
    double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
    const auto grow = [&](P p) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    };
    for (const Coord& c : st.fit) {
      Where w;
      if (!resolve(c, st, P{0, 0}, w)) continue;
      if (w.node != nullptr) {
        for (const char* a : {"west", "east", "north", "south"}) grow(anchorOf(*w.node, a));
      } else {
        grow(w.p);
      }
    }
    if (x0 <= x1) fit = FitBox{P{0.5 * (x0 + x1), 0.5 * (y0 + y1)}, x1 - x0, y1 - y0};
  }
  Geom g = makeGeom(n, st, box, textAt, fit ? &*fit : nullptr);
  // Where its anchor is to stand.
  std::string anchor = fit ? "center" : st.anchor;
  // `at` comes after the options in the source, so it wins over where `fit` puts the node: the size alone is fitted.
  P target = fit && !n.hasAt ? fit->centre : at;
  static const struct Place {
    const char* name;
    const char* anchor;
    const char* ref;
    double dx, dy, single;
  } places[] = {{"above", "south", "north", 0, 1, 1},
                {"below", "north", "south", 0, -1, 1},
                {"left", "east", "west", -1, 0, 1},
                {"right", "west", "east", 1, 0, 1},
                {"above left", "south east", "north west", -1, 1, 0.707106781},
                {"above right", "south west", "north east", 1, 1, 0.707106781},
                {"below left", "north east", "south west", -1, -1, 0.707106781},
                {"below right", "north west", "south east", 1, -1, 0.707106781}};
  const Place* place = nullptr;
  for (const Place& p : places) {
    if (st.placement == p.name) place = &p;
  }
  if (place != nullptr && !fit) {
    const Dist nd = st.nodeDist.value_or(Dist());
    const bool haveNd = st.nodeDist.has_value();
    const auto distances = [&](const std::optional<Dist>& d, double factor, double& dxLen, double& dyLen) {
      // `1cm and 2cm` is y and x; one number is both, shortened on the diagonals.
      if (d && d->pair) {
        dyLen = len(d->y);
        dxLen = len(d->x);
      } else if (d) {
        dxLen = dyLen = len(d->y) * factor;
      } else if (haveNd && nd.pair) {
        dyLen = len(nd.y);
        dxLen = len(nd.x);
      } else if (haveNd) {
        dxLen = dyLen = len(nd.y) * factor;
      } else {
        dxLen = dyLen = _cm * factor;
      }
    };
    double dxl = 0, dyl = 0;
    if (!st.placeOf.empty()) {
      const auto ref = _nodes.find(st.placeOf);
      if (ref != _nodes.end()) {
        if (st.placeCentres) {
          distances(st.placeDist, 1.0, dxl, dyl);
          target = ref->second.c + P{place->dx * dxl, place->dy * dyl};
          anchor = anchor.empty() ? "center" : anchor;
        } else {
          distances(st.placeDist, place->single, dxl, dyl);
          target = anchorOf(ref->second, place->ref) + P{place->dx * dxl, place->dy * dyl};
          if (anchor.empty()) anchor = place->anchor;
        }
      }
    } else {
      // Beside the point the node is put at.
      if (st.placeDist) {
        distances(st.placeDist, place->single, dxl, dyl);
        target = at + P{place->dx * dxl, place->dy * dyl};
      }
      if (anchor.empty()) anchor = place->anchor;
    }
  } else if (!fit && !atGiven && !n.hasAt) {
    target = at;
  }
  // The node's own `shift`, `xshift` and `yshift` (those the path round it does not already carry) move where it is put.
  for (std::size_t k = n.ownFrom; k < st.transform.size(); k++) {
    const XStep& s = st.transform[k];
    if (s.kind == XStep::Kind::shift) target = target + P{len(s.dx), len(s.dy)};
    if (s.kind == XStep::Kind::xshift) target = target + P{len(s.dx), 0.0};
    if (s.kind == XStep::Kind::yshift) target = target + P{0.0, len(s.dx)};
  }
  // `auto` and `swap`: the node stands on the left of the way the path goes (or the right), clear of it.
  if (anchor.empty() && seg != nullptr && seg->valid && (st.autoSide || st.swap) && !g.point) {
    P dir = seg->direction(t);
    P normal = left(dir);
    if (st.swap) normal = normal * -1.0;
    // The border of the node towards the path.
    Geom at0 = g;
    at0.c = {0, 0};
    const P border = borderToward(at0, normal * -1.0);
    target = target - border + P{0, 0};
    g.c = target;
  } else {
    Geom at0 = g;
    at0.c = {0, 0};
    const P offset = anchorOf(at0, anchor);
    g.c = target - offset;
  }
  if (!n.name.empty()) _nodes[n.name] = g;
  // Its shape and its text.
  const bool stroke = st.draw.mode == Paint::Mode::current || st.draw.mode == Paint::Mode::color;
  const bool fill = st.fill.mode == Paint::Mode::current || st.fill.mode == Paint::Mode::color;
  Prim prim = primOf(st, stroke, fill);
  if (!g.point) {
    // The picture holds the node's outline -- not its outer separation, which only keeps other things off it -- and, when
    // it is drawn, half the line on either side (pdfTeX: `outer sep=5pt` is no wider, `line width=2pt` is 2pt wider).
    Prim shape;
    nodeOutline(g, st, shape);
    double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
    const auto grow = [&](P p) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    };
    for (const Sub& s : shape.subs) {
      grow(s.start);
      for (const Seg& sg : s.segs) {
        grow(sg.to);
        if (sg.cubic) {
          grow(sg.c1);
          grow(sg.c2);
        }
      }
    }
    const double half = stroke ? 0.5 * prim.lw : 0.0;
    see(P{x0 - half, y0 - half});
    see(P{x1 + half, y1 + half});
    if (stroke || fill) prim.subs = std::move(shape.subs);
  } else {
    see(g.c);
  }
  if (box != nullptr) {
    prim.content = box;
    prim.at = g.c + textAt;
  }
  if (!prim.subs.empty() || prim.content != nullptr) _prims.push_back(std::move(prim));
  for (const Label& lb : n.labels) placeLabel(st, g, lb);
  if (!n.children.empty()) placeChildren(n, g);
}

/** The children of a node, as pgf grows a tree: each one `level distance` from the parent's centre in the direction of
 *  `grow`, and, where `grow` was not set at its own level, spread along the line across it, `sibling distance` apart
 *  and centred on the parent (`(n + 1) / 2` units to the left, then `j` to the right; `grow'` the other way). A
 *  `missing` child has its place and no node. */
void Layout::placeChildren(const Node& parent, const Geom& g) {
  const int count = static_cast<int>(parent.children.size());
  for (int j = 0; j < count; j++) {
    const Child& c = parent.children[static_cast<std::size_t>(j)];
    if (c.missing) continue;
    const double ld = len(c.style.levelDistance, 1.5 * _cm), sd = len(c.style.siblingDistance, 1.5 * _cm);
    const double angle = c.style.growAngle.value_or(-90.f);
    P shift = polar(angle, ld);
    if (c.style.growLevel != c.level) {
      const double left = c.style.growSwap ? angle + 90 : angle - 90, right = c.style.growSwap ? angle - 90 : angle + 90;
      shift = shift + polar(left, 0.5 * (count + 1) * sd) + polar(right, (j + 1) * sd);
    }
    Node child = c.node;
    child.hasAt = false;
    layoutNode(child, child.style, g.c + shift, true, nullptr, 0.5);
  }
}

/** A label or a pin: a node put on the border of `owner` at an angle, `label distance` (`pin distance`, 3ex)
 *  beyond it, its anchor the one that faces the owner, as `tikz@compute@direction` chooses it. A pin has a
 *  line to its node as well. */
void Layout::placeLabel(const Style& ownerStyle, const Geom& owner, const Label& lb) {
  static const struct {
    const char* name;
    double angle;
  } words[] = {{"right", 0},       {"above right", 45}, {"above", 90},       {"above left", 135},
               {"left", 180},      {"below left", 225}, {"below", 270},      {"below right", 315}};
  Style ls = lb.style;
  P at = owner.c;
  if (lb.angle != "center") {
    double angle = 90;
    bool known = false;
    for (const auto& w : words) {
      if (lb.angle == w.name) {
        angle = w.angle;
        known = true;
      }
    }
    if (!known) {
      char* end = nullptr;
      const double a = std::strtod(lb.angle.c_str(), &end);
      if (end != lb.angle.c_str() && *end == '\0') angle = a;
    }
    const double distance = lb.pin ? len(ownerStyle.pinDistance, len(Dimen{3.f, UnitType::ex}))
                                   : len(ownerStyle.labelDistance, 0.0);
    at = borderToward(owner, owner.c + polar(angle, 1.0)) + polar(angle, distance);
    if (ls.anchor.empty()) {
      double a = std::fmod(angle, 360.0);
      if (a < 0) a += 360.0;
      const int k = static_cast<int>(a);
      ls.anchor = k < 4     ? "west"
                  : k < 87  ? "south west"
                  : k < 94  ? "south"
                  : k < 177 ? "south east"
                  : k < 184 ? "east"
                  : k < 267 ? "north east"
                  : k < 274 ? "north"
                  : k < 357 ? "north west"
                            : "west";
    }
  } else if (ls.anchor.empty()) {
    ls.anchor = "center";
  }
  Node ln;
  ln.name = "\x01label" + std::to_string(_labelCount++);
  ln.style = ls;
  ln.content = lb.content;
  layoutNode(ln, ls, at, true, nullptr, 0.5);
  if (!lb.pin) return;
  const auto it = _nodes.find(ln.name);
  if (it == _nodes.end() || owner.point) return;
  const Geom& pin = it->second;
  Prim p = primOf(lb.edge, true, false);
  Sub sub;
  sub.start = borderToward(owner, pin.c);
  Seg line;
  line.to = borderToward(pin, owner.c);
  sub.segs.push_back(line);
  std::vector<Sub> subs{sub};
  std::vector<Prim> tips;
  applyTips(lb.edge, subs, p.lw, tips);
  p.subs = std::move(subs);
  _prims.push_back(std::move(p));
  for (Prim& t : tips) _prims.push_back(std::move(t));
}

// --- connections --------------------------------------------------------------------------------

/** A `to` or an `edge` from one place to another: a line, or the curve its options ask for, between the
 *  borders of the nodes it joins. */
void Layout::connect(const PathItem& item, const PathOp& op, const Where& from, const Where& to, std::vector<Sub>& out,
                     SegInfo& info, const Style& st) {
  (void)item;
  (void)op;
  const ToSpec& ts = st.to;
  P s = from.p, e = to.p;
  Sub sub;
  if (!ts.curved()) {
    if (from.node != nullptr) s = borderToward(*from.node, to.p);
    if (to.node != nullptr) e = borderToward(*to.node, from.p);
    sub.start = s;
    Seg l;
    l.to = e;
    sub.segs.push_back(l);
    info = {true, false, s, {}, {}, e};
    out.push_back(sub);
    see(s);
    see(e);
    return;
  }
  // A curve. The angles the two ends leave and arrive at: absolute, or turned by the way the line goes.
  const double alphaDeg = degrees(std::atan2(to.p.y - from.p.y, to.p.x - from.p.x));
  double outA = 0, inA = 180;
  if (ts.bend) {
    outA = alphaDeg + *ts.bend;
    inA = alphaDeg + 180.0 - *ts.bend;
  } else {
    outA = ts.out.value_or(alphaDeg);
    inA = ts.in.value_or(alphaDeg + 180.0);
  }
  // Ends on the borders of nodes, at those angles.
  if (from.node != nullptr) s = anchorOf(*from.node, std::to_string(outA));
  if (to.node != nullptr) e = anchorOf(*to.node, std::to_string(inA));
  const double d = length(e - s);
  double dist = 0.3915 * d * ts.looseness;
  if (ts.minDistance) dist = std::max(dist, len(*ts.minDistance));
  sub.start = s;
  Seg c;
  c.cubic = true;
  c.c1 = s + polar(outA, dist);
  c.c2 = e + polar(inA, dist);
  c.to = e;
  sub.segs.push_back(c);
  info = {true, true, s, c.c1, c.c2, e};
  out.push_back(sub);
  see(s);
  see(c.c1);
  see(c.c2);
  see(e);
}

// --- a matrix of nodes --------------------------------------------------------------------------

namespace {

/** A box of a given size that draws nothing: the matrix, to the node that holds it. */
class FixedAtom : public Atom {
public:
  FixedAtom(float width, float height, float depth) : _w(width), _h(height), _d(depth) {}
  sptr<Box> createBox(Env&) override { return sptrOf<StrutBox>(_w, _h, _d, 0.f); }

private:
  float _w, _h, _d;
};

}  // namespace

/** pgfmodulematrix: the cells' origins stand on one vertical line in a column and one horizontal line in a row; a
 *  column is as wide as its cells reach to either side of the line, a row as high as they reach above it and low below;
 *  `column sep` and `row sep` go between the cells' borders (between their origins, if asked). The matrix is a node
 *  whose text is that box, and each cell a node of its own, anchored at `base` where it does not say otherwise. */
void Layout::layoutNodeMatrix(const NodeMatrix& gm, const Style& st) {
  (void)st;
  const int rows = gm.rows, cols = gm.cols;
  if (rows == 0 || cols == 0) return;
  std::vector<double> left(static_cast<std::size_t>(cols) + 1, 0.0), right = left, above(static_cast<std::size_t>(rows) + 1, 0.0),
                      below = above;
  const auto anchorName = [](const Node& n) { return n.style.anchor.empty() ? std::string("base") : n.style.anchor; };
  for (const MatrixCell& cell : gm.cells) {
    if (cell.empty) continue;
    sptr<Box> box;
    P textAt;
    const Geom g = makeGeom(cell.node, cell.node.style, box, textAt, nullptr);
    const P a = anchorOf(g, anchorName(cell.node));
    // A cell is as big as its node without the outer separation (pdfTeX: tools/tikz-ref x1, x2).
    const double w = g.point ? 0.0 : g.hw, h = g.point ? 0.0 : g.hh;
    const auto c = static_cast<std::size_t>(cell.col), r = static_cast<std::size_t>(cell.row);
    left[c] = std::max(left[c], a.x + w);
    right[c] = std::max(right[c], w - a.x);
    below[r] = std::max(below[r], a.y + h);
    above[r] = std::max(above[r], h - a.y);
  }
  const Style& ms = gm.node.style;
  const double colSep = len(ms.columnSep, 0.0), rowSep = len(ms.rowSep, 0.0);
  std::vector<double> x(static_cast<std::size_t>(cols) + 1, 0.0), y(static_cast<std::size_t>(rows) + 1, 0.0);
  x[1] = left[1];
  for (int c = 2; c <= cols; c++) {
    const auto k = static_cast<std::size_t>(c);
    x[k] = ms.columnBetweenOrigins ? x[k - 1] + colSep : x[k - 1] + right[k - 1] + colSep + left[k];
  }
  y[1] = -above[1];
  for (int r = 2; r <= rows; r++) {
    const auto k = static_cast<std::size_t>(r);
    const double skip = k - 1 < gm.rowSkip.size() ? len(gm.rowSkip[k - 1]) : 0.0;
    y[k] = ms.rowBetweenOrigins ? y[k - 1] - rowSep - skip : y[k - 1] - below[k - 1] - rowSep - skip - above[k];
  }
  const double width = x[static_cast<std::size_t>(cols)] + right[static_cast<std::size_t>(cols)];
  const double height = -(y[static_cast<std::size_t>(rows)] - below[static_cast<std::size_t>(rows)]);
  // The matrix as a node round a box of that size, placed as any node is.
  Node mn = gm.node;
  if (mn.name.empty()) mn.name = "\x01matrix";
  mn.content = sptrOf<FixedAtom>(static_cast<float>(width), static_cast<float>(height), 0.f);
  P at = matrixOf(st).apply(P{0, 0});   // the origin of its scope, shifted as the scope says
  bool given = false;
  if (mn.hasAt) {
    Where w;
    given = resolve(mn.at, mn.style, P{0, 0}, w);
    if (given) at = w.p;
  }
  layoutNode(mn, mn.style, at, given, nullptr, 0.5);
  const auto it = _nodes.find(mn.name);
  if (it == _nodes.end()) return;
  const P origin{it->second.c.x - 0.5 * width, it->second.c.y + 0.5 * height};
  for (const MatrixCell& cell : gm.cells) {
    if (cell.empty) continue;
    Node cn = cell.node;
    cn.hasAt = false;
    Style cs = cn.style;
    if (cs.anchor.empty()) cs.anchor = "base";
    cn.style = cs;
    const P where{origin.x + x[static_cast<std::size_t>(cell.col)], origin.y + y[static_cast<std::size_t>(cell.row)]};
    layoutNode(cn, cs, where, true, nullptr, 0.5);
  }
}

// --- a path -------------------------------------------------------------------------------------

/** An item, with the clip paths in force for it: the part of the picture it adds is only what lies inside them. */
void Layout::layoutItem(const PathItem& item) {
  _clipped = false;
  for (const int id : item.style.clips) {
    const auto it = _regions.find(id);
    if (it == _regions.end() || it->second.empty()) continue;
    double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
    for (const P& p : it->second) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    }
    if (_clipped) {
      x0 = std::max(x0, _cx0);
      x1 = std::min(x1, _cx1);
      y0 = std::max(y0, _cy0);
      y1 = std::min(y1, _cy1);
    }
    _cx0 = x0;
    _cx1 = std::max(x0, x1);
    _cy0 = y0;
    _cy1 = std::max(y0, y1);
    _clipped = true;
  }
  layoutPath(item);
  _clipped = false;
}

// --- clipping --------------------------------------------------------------------------------------

namespace {

/** Which side of the line a to b the point is on: positive to its left. */
double sideOf(P a, P b, P p) {
  return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

bool insideRegion(const std::vector<P>& region, P p) {
  for (std::size_t i = 0; i < region.size(); i++) {
    if (sideOf(region[i], region[(i + 1) % region.size()], p) < -1e-9) return false;
  }
  return true;
}

/** The polygon `in` cut to the convex, counter-clockwise `region` (Sutherland and Hodgman). */
std::vector<P> clipPolygon(std::vector<P> in, const std::vector<P>& region) {
  for (std::size_t e = 0; e < region.size() && !in.empty(); e++) {
    const P a = region[e], b = region[(e + 1) % region.size()];
    std::vector<P> out;
    for (std::size_t i = 0; i < in.size(); i++) {
      const P cur = in[i], prev = in[(i + in.size() - 1) % in.size()];
      const double sc = sideOf(a, b, cur), sp = sideOf(a, b, prev);
      if (sc >= 0) {
        if (sp < 0) out.push_back(prev + (cur - prev) * (sp / (sp - sc)));
        out.push_back(cur);
      } else if (sp >= 0) {
        out.push_back(prev + (cur - prev) * (sp / (sp - sc)));
      }
    }
    in = std::move(out);
  }
  return in;
}

/** The pieces of the polyline `pts` that lie inside the convex `region` (Liang and Barsky). */
std::vector<std::vector<P>> clipPolyline(const std::vector<P>& pts, const std::vector<P>& region) {
  std::vector<std::vector<P>> pieces;
  std::vector<P> run;
  const auto flush = [&]() {
    if (run.size() > 1 && length(run.back() - run.front()) > 1e-9) pieces.push_back(run);
    run.clear();
  };
  for (std::size_t i = 1; i < pts.size(); i++) {
    const P p = pts[i - 1], q = pts[i];
    double t0 = 0, t1 = 1;
    bool outside = false;
    for (std::size_t e = 0; e < region.size() && !outside; e++) {
      const P a = region[e], b = region[(e + 1) % region.size()];
      const double sp = sideOf(a, b, p), sq = sideOf(a, b, q);
      if (sp < 0 && sq < 0) {
        outside = true;
      } else if (sp < 0) {
        t0 = std::max(t0, sp / (sp - sq));
      } else if (sq < 0) {
        t1 = std::min(t1, sp / (sp - sq));
      }
    }
    if (outside || t0 > t1) {
      flush();
      continue;
    }
    if (t0 > 0 || run.empty()) {
      flush();
      run.push_back(p + (q - p) * t0);
    }
    run.push_back(p + (q - p) * t1);
    if (t1 < 1) flush();
  }
  flush();
  return pieces;
}

}  // namespace

/** One primitive cut to a clip region: a filled shape becomes the polygon inside it, a stroked path the pieces of line
 *  inside it (so the clip's own edge is not stroked), and a node's text stays when its point is inside. What lies wholly
 *  inside is kept as it was, curves and all. */
void Layout::clipOne(const Prim& p, const std::vector<P>& region, std::vector<Prim>& out) const {
  const bool keepText = p.content == nullptr || insideRegion(region, p.at);
  const auto inside = [&](const Sub& s) {
    if (!insideRegion(region, s.start)) return false;
    for (const Seg& g : s.segs) {
      if (!insideRegion(region, g.to) || (g.cubic && (!insideRegion(region, g.c1) || !insideRegion(region, g.c2)))) return false;
    }
    return true;
  };
  Prim fillP = p, strokeP = p;
  fillP.stroke = false;
  fillP.content = nullptr;
  fillP.subs.clear();
  strokeP.fill = false;
  strokeP.subs.clear();
  if (!keepText) strokeP.content = nullptr;
  const auto polyline = [](const std::vector<P>& pts, bool closed) {
    Sub s;
    s.start = pts.front();
    for (std::size_t k = 1; k < pts.size(); k++) {
      Seg g;
      g.to = pts[k];
      s.segs.push_back(g);
    }
    s.closed = closed;
    return s;
  };
  for (const Sub& s : p.subs) {
    if (inside(s)) {
      if (p.fill) fillP.subs.push_back(s);
      if (p.stroke) strokeP.subs.push_back(s);
      continue;
    }
    std::vector<P> pts = flatten(s, 24);
    if (p.fill) {
      std::vector<P> poly = pts;
      if (s.closed && poly.size() > 1) poly.pop_back();
      poly = clipPolygon(poly, region);
      if (poly.size() >= 3) fillP.subs.push_back(polyline(poly, true));
    }
    if (p.stroke) {
      for (const std::vector<P>& piece : clipPolyline(pts, region)) strokeP.subs.push_back(polyline(piece, false));
    }
  }
  if (p.fill && !fillP.subs.empty()) out.push_back(std::move(fillP));
  if (!strokeP.subs.empty() || strokeP.content != nullptr) out.push_back(std::move(strokeP));
}

/** Every primitive drawn inside clip paths, cut to them (each in turn: the regions are convex, so one after the other is
 *  their intersection). */
void Layout::clipPrims() {
  std::vector<Prim> out;
  for (Prim& p : _prims) {
    if (p.clips.empty()) {
      out.push_back(std::move(p));
      continue;
    }
    const std::vector<int> ids = p.clips;
    std::vector<Prim> parts;
    parts.push_back(std::move(p));
    for (const int id : ids) {
      const auto it = _regions.find(id);
      if (it == _regions.end() || it->second.empty()) continue;
      std::vector<Prim> next;
      for (const Prim& q : parts) clipOne(q, it->second, next);
      parts = std::move(next);
    }
    for (Prim& q : parts) out.push_back(std::move(q));
  }
  _prims = std::move(out);
}

void Layout::layoutPath(const PathItem& item) {
  const Style& st = item.style;
  if (item.grid != nullptr) {
    layoutNodeMatrix(*item.grid, st);
    return;
  }
  if (item.isNode) {
    const Node& n = item.nodes.front();
    P at = matrixOf(st).apply(P{0, 0});  // the origin, shifted as the path says
    bool given = false;
    Where w;
    if (n.hasAt) {
      given = resolve(n.at, st, P{0, 0}, w);
      if (given) at = w.p;
    }
    layoutNode(n, n.style, at, given, nullptr, 0.5);
    return;
  }
  const Matrix m = matrixOf(st);
  const bool stroke = st.draw.mode == Paint::Mode::current || st.draw.mode == Paint::Mode::color;
  const bool fill = st.fill.mode == Paint::Mode::current || st.fill.mode == Paint::Mode::color;
  const double lw = len(st.lineWidth, 0.4 * _pt);

  std::vector<Sub> subs;
  Sub cur;
  bool open = false;
  P at, start, rel, nominal;          // the current point, where the subpath began, the base of `+`, the last vertex's own place
  const Geom* startNode = nullptr;    // the subpath began on a node, its border not yet found
  const Geom* vertexNode = nullptr;   // the last vertex is a node named without an anchor
  std::vector<SegInfo> segs(item.ops.size());
  struct Put {
    int node;
    P at;
    int seg;
    double t;
  };
  std::vector<Put> puts;
  int lastSeg = -1;
  std::vector<Prim> extra;            // the to and edge paths

  const auto res = [&](const Coord& c, Where& out) {
    const bool ok = resolve(c, st, rel, out);
    if (ok && c.relative != 1) rel = out.p;
    return ok;
  };
  const auto begin = [&](P p) {
    if (open && !cur.segs.empty()) subs.push_back(cur);
    cur = Sub();
    cur.start = p;
    open = true;
    start = p;
    at = p;
    see(p);
  };
  const auto lineTo = [&](P p) {
    if (!open) begin(at);
    Seg g;
    g.to = p;
    cur.segs.push_back(g);
    at = p;
    see(p);
  };
  // The first line of a subpath that began on a node starts on its border, towards where the line goes.
  const auto fixStart = [&](P target) {
    if (startNode != nullptr && cur.segs.empty()) {
      cur.start = borderToward(*startNode, target);
      at = cur.start;
      see(at);
    }
    startNode = nullptr;
  };

  for (std::size_t i = 0; i < item.ops.size(); i++) {
    const PathOp& op = item.ops[i];
    Where w;
    switch (op.kind) {
      case PathOp::Kind::move:
        if (!res(op.p, w)) break;
        begin(w.p);
        nominal = w.p;
        startNode = w.node;
        vertexNode = w.node;
        break;
      case PathOp::Kind::line:
      case PathOp::Kind::hline:
      case PathOp::Kind::vline: {
        if (!res(op.p, w)) break;
        const P target = w.p;
        const P from = at;
        if (op.kind == PathOp::Kind::line) {
          fixStart(w.node != nullptr ? w.node->c : target);
          P end = target;
          if (w.node != nullptr) end = borderToward(*w.node, nominal);
          const P before = at;
          lineTo(end);
          segs[i] = {true, false, before, {}, {}, end};
        } else {
          const P corner = op.kind == PathOp::Kind::hline ? P{target.x, nominal.y} : P{nominal.x, target.y};
          fixStart(corner);
          const P before = at;
          lineTo(corner);
          segs[i] = {true, false, before, {}, {}, corner};
          P end = target;
          if (w.node != nullptr) end = borderToward(*w.node, corner);
          const P mid = at;
          lineTo(end);
          segs[i] = {true, false, mid, {}, {}, end};
        }
        (void)from;
        nominal = target;
        vertexNode = w.node;
        lastSeg = static_cast<int>(i);
        break;
      }
      case PathOp::Kind::curve: {
        Where a, b;
        if (!res(op.c1, a) || !res(op.p, w)) break;
        if (!open) begin(at);
        Seg g;
        g.cubic = true;
        g.c1 = a.p;
        g.c2 = a.p;
        if (op.c2.kind != Coord::Kind::xy || op.c2.x.isValid()) {
          if (res(op.c2, b)) g.c2 = b.p;
        }
        g.to = w.p;
        segs[i] = {true, true, at, g.c1, g.c2, g.to};
        cur.segs.push_back(g);
        see(g.c1);
        see(g.c2);
        see(g.to);
        at = w.p;
        nominal = w.p;
        lastSeg = static_cast<int>(i);
        break;
      }
      case PathOp::Kind::rect: {
        if (!res(op.p, w)) break;
        const P a = at, b = w.p;
        begin(a);
        lineTo({b.x, a.y});
        lineTo(b);
        lineTo({a.x, b.y});
        cur.closed = true;
        // A node on a rectangle (`rectangle node{x} (3,1)`, `rectangle (3,1) node[midway]{x}`) goes on its diagonal: half way
        // is the centre.
        segs[i] = {true, false, a, P(), P(), b};
        lastSeg = static_cast<int>(i);
        at = b;
        nominal = b;
        break;
      }
      case PathOp::Kind::circle:
      case PathOp::Kind::ellipse: {
        const double rs = m.scale();
        const double rx = len(op.r) * rs;
        const double ry = (op.r2.isValid() ? len(op.r2) : len(op.r)) * rs;
        const P centre = at;
        begin(centre + P{rx, 0});
        arcSegments(cur, centre, rx, ry, 0, 360);
        for (const Seg& g : cur.segs) {
          see(g.c1);
          see(g.c2);
          see(g.to);
        }
        cur.closed = true;
        at = centre;
        break;
      }
      case PathOp::Kind::arc: {
        const double rs = m.scale();
        const double rx = len(op.r) * rs;
        const double ry = (op.r2.isValid() ? len(op.r2) : len(op.r)) * rs;
        const P centre = at - P{rx * std::cos(radians(op.a0)), ry * std::sin(radians(op.a0))};
        if (!open) begin(at);
        const std::size_t before = cur.segs.size();
        arcSegments(cur, centre, rx, ry, op.a0, op.a1);
        for (std::size_t k = before; k < cur.segs.size(); k++) {
          see(cur.segs[k].c1);
          see(cur.segs[k].c2);
          see(cur.segs[k].to);
        }
        at = centre + P{rx * std::cos(radians(op.a1)), ry * std::sin(radians(op.a1))};
        nominal = at;
        break;
      }
      case PathOp::Kind::close:
        if (open) {
          cur.closed = true;
          at = start;
          nominal = start;
          subs.push_back(cur);
          cur = Sub();
          open = false;
        }
        break;
      case PathOp::Kind::node: {
        const Node& n = item.nodes[static_cast<std::size_t>(op.node)];
        if (n.between) break;       // placed once the segment it sits on is known
        Put p;
        p.node = op.node;
        p.at = at;
        p.seg = n.posGiven ? lastSeg : -1;
        p.t = n.pos;
        puts.push_back(p);
        break;
      }
      case PathOp::Kind::grid: {
        if (!res(op.p, w)) break;
        const double sx = len(st.xstep ? st.xstep : st.step, _cm), sy = len(st.ystep ? st.ystep : st.step, _cm);
        const P a{std::min(at.x, w.p.x), std::min(at.y, w.p.y)}, b{std::max(at.x, w.p.x), std::max(at.y, w.p.y)};
        if (open && !cur.segs.empty()) subs.push_back(cur);
        cur = Sub();
        open = false;
        for (double x = a.x; x <= b.x + 1e-6; x += std::max(sx, 1e-6)) {
          Sub l;
          l.start = {x, a.y};
          Seg g;
          g.to = {x, b.y};
          l.segs.push_back(g);
          subs.push_back(l);
        }
        for (double y = a.y; y <= b.y + 1e-6; y += std::max(sy, 1e-6)) {
          Sub l;
          l.start = {a.x, y};
          Seg g;
          g.to = {b.x, y};
          l.segs.push_back(g);
          subs.push_back(l);
        }
        see(a);
        see(b);
        at = w.p;
        break;
      }
      case PathOp::Kind::to: {
        if (!res(op.p, w)) break;
        // From the vertex the path is at: the node it stands on (its border found towards the target), or the point.
        Where from;
        from.p = nominal;
        from.node = cur.segs.empty() && open ? startNode : vertexNode;
        if (!open && from.node == nullptr) from.node = vertexNode;
        const Style own = op.own.value_or(st);
        std::vector<Sub> made;
        SegInfo info;
        connect(item, op, from, w, made, info, own);
        segs[i] = info;
        lastSeg = static_cast<int>(i);
        if (op.edge || op.own) {
          // An edge is a path of its own, and the current point stays; so is a `to` with options.
          const bool drawn = op.edge || own.draw.mode == Paint::Mode::current || own.draw.mode == Paint::Mode::color;
          Prim p = primOf(own, drawn, false);
          std::vector<Sub> sh = made;
          if (drawn) applyTips(own, sh, p.lw, extra);
          p.subs = sh;
          extra.insert(extra.begin(), std::move(p));
          if (!op.edge) {
            at = made.front().segs.front().to;
            nominal = w.p;
            vertexNode = w.node;
            open = false;
            if (!cur.segs.empty()) subs.push_back(cur);
            cur = Sub();
          }
        } else {
          // A plain `to` goes on in the path: a line or a segment of curve from where it is.
          const Seg g = made.front().segs.front();
          if (!open) begin(made.front().start);
          if (cur.segs.empty() && startNode != nullptr) {
            cur.start = made.front().start;
            at = cur.start;
            startNode = nullptr;
          }
          cur.segs.push_back(g);
          at = g.to;
          see(g.to);
          nominal = w.p;
          vertexNode = w.node;
        }
        break;
      }
    }
    if (op.kind != PathOp::Kind::to && op.kind != PathOp::Kind::move && op.kind != PathOp::Kind::line &&
        op.kind != PathOp::Kind::hline && op.kind != PathOp::Kind::vline && op.kind != PathOp::Kind::node) {
      vertexNode = nullptr;
    }
  }
  if (open && !cur.segs.empty()) subs.push_back(cur);

  if (item.clipId >= 0) {
    // A clip path draws nothing; its first subpath, if it is a convex polygon once flattened, is the region.
    std::vector<P> region;
    for (const Sub& s : subs) {
      std::vector<P> pts = flatten(s, 24);
      if (pts.size() > 1 && length(pts.back() - pts.front()) < 1e-9) pts.pop_back();
      if (pts.size() >= 3) {
        region = std::move(pts);
        break;
      }
    }
    double area = 0;
    for (std::size_t k = 0; k < region.size(); k++) area += sideOf(P{0, 0}, region[k], region[(k + 1) % region.size()]);
    if (area < 0) std::reverse(region.begin(), region.end());
    for (std::size_t k = 0; k < region.size(); k++) {
      const P e1 = region[(k + 1) % region.size()] - region[k], e2 = region[(k + 2) % region.size()] - region[(k + 1) % region.size()];
      if (e1.x * e2.y - e1.y * e2.x < -1e-7 * length(e1) * length(e2)) region.clear();   // a corner turns the wrong way
      if (region.empty()) break;
    }
    _regions[item.clipId] = region;
  }

  // The picture holds a stroked path's line, half its width each side of the path (and an arrow tip's the same): pdfTeX,
  // `\draw[line width=8pt] (0,0) -- (1,0)` is 8pt wider and 8pt taller than the path. The control points count, as ever.
  const auto holdLine = [&](const Prim& p) {
    if (!p.stroke) return;
    double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
    const auto grow = [&](P q) {
      x0 = std::min(x0, q.x);
      x1 = std::max(x1, q.x);
      y0 = std::min(y0, q.y);
      y1 = std::max(y1, q.y);
    };
    for (const Sub& s : p.subs) {
      grow(s.start);
      for (const Seg& sg : s.segs) {
        grow(sg.to);
        if (sg.cubic) {
          grow(sg.c1);
          grow(sg.c2);
        }
      }
    }
    if (x0 > x1) return;
    const double half = 0.5 * p.lw;
    see(P{x0 - half, y0 - half});
    see(P{x1 + half, y1 + half});
  };

  if (!subs.empty() && (stroke || fill)) {
    Prim prim = primOf(st, stroke, fill);
    std::vector<Sub> drawn;
    const double r = len(st.rounded, 0.0);
    if (r > 1e-9) {
      rounded(subs, r, drawn);
    } else {
      drawn = subs;
    }
    if (st.decorate && st.decoration == "brace") braces(drawn, st);
    if (stroke) applyTips(st, drawn, lw, extra);
    prim.subs = std::move(drawn);
    holdLine(prim);
    _prims.push_back(std::move(prim));
  }
  for (Prim& p : extra) {
    holdLine(p);
    _prims.push_back(std::move(p));
  }

  // The nodes: those at a point of the path, and those on a segment of it.
  for (const Put& p : puts) {
    const Node& n = item.nodes[static_cast<std::size_t>(p.node)];
    if (p.seg >= 0 && segs[static_cast<std::size_t>(p.seg)].valid) {
      const SegInfo& s = segs[static_cast<std::size_t>(p.seg)];
      layoutNode(n, n.style, s.at(p.t), true, &s, p.t);
    } else {
      layoutNode(n, n.style, p.at, true, nullptr, 0.5);
    }
  }
  for (const PathOp& op : item.ops) {
    if (op.kind != PathOp::Kind::node) continue;
    const Node& n = item.nodes[static_cast<std::size_t>(op.node)];
    if (!n.between || n.alongOp < 0) continue;
    const SegInfo& s = segs[static_cast<std::size_t>(n.alongOp)];
    if (!s.valid) continue;
    layoutNode(n, n.style, s.at(n.pos), true, &s, n.pos);
  }
}

sptr<Box> Layout::run() {
  for (const PathItem& p : _pic.paths) layoutItem(p);
  // Nothing, or a picture too large for a box to hold (its size would not be a number the host can read).
  if (_minx > _maxx || !(_maxx - _minx < 1e7) || !(_maxy - _miny < 1e7)) return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
  clipPrims();

  class TikzBox : public Box {
  public:
    std::vector<Prim> prims;
    P origin;                  // picture coordinates of the box's lower left corner

    void draw(Graphics2D& g2, float x, float y) override {
      const Stroke oldStroke = g2.getStroke();
      const color oldColor = g2.getColor();
      const auto map = [&](P p) { return P{x + (p.x - origin.x), y - (p.y - origin.y)}; };
      const auto trace = [&](const Sub& s) {
        const P a = map(s.start);
        g2.moveTo(static_cast<float>(a.x), static_cast<float>(a.y));
        for (const Seg& g : s.segs) {
          const P to = map(g.to);
          if (g.cubic) {
            const P c1 = map(g.c1), c2 = map(g.c2);
            g2.cubicTo(static_cast<float>(c1.x), static_cast<float>(c1.y), static_cast<float>(c2.x),
                       static_cast<float>(c2.y), static_cast<float>(to.x), static_cast<float>(to.y));
          } else {
            g2.lineTo(static_cast<float>(to.x), static_cast<float>(to.y));
          }
        }
        if (s.closed) g2.closePath();
      };
      for (const Prim& p : prims) {
        if (p.fill) {
          g2.setColor(p.fillCol);
          g2.beginPath(-1);
          for (const Sub& s : p.subs) trace(s);
          g2.fillPath(-1);
        }
        if (p.stroke) {
          g2.setColor(p.strokeCol);
          g2.setStroke(Stroke(static_cast<float>(p.lw), static_cast<Cap>(p.cap), static_cast<Join>(p.join), 10.f));
          if (p.dash.empty()) {
            g2.beginPath(-1);
            for (const Sub& s : p.subs) trace(s);
            g2.strokePath(-1);
          } else {
            for (const Sub& s : p.subs) {
              for (const std::vector<P>& piece : dashes(flatten(s), p.dash, p.phase)) {
                g2.beginPath(-1);
                const P a = map(piece.front());
                g2.moveTo(static_cast<float>(a.x), static_cast<float>(a.y));
                for (std::size_t k = 1; k < piece.size(); k++) {
                  const P q = map(piece[k]);
                  g2.lineTo(static_cast<float>(q.x), static_cast<float>(q.y));
                }
                g2.strokePath(-1);
              }
            }
          }
        }
        if (p.content != nullptr) {
          g2.setColor(p.textCol);
          const P a = map(p.at);
          p.content->draw(g2, static_cast<float>(a.x), static_cast<float>(a.y));
        }
      }
      g2.setStroke(oldStroke);
      g2.setColor(oldColor);
    }

    // The pieces of a polyline that a dash pattern leaves: on, off, on, off ... from `phase` into it.
    static std::vector<std::vector<P>> dashes(const std::vector<P>& pts, const std::vector<double>& pattern, double phase) {
      std::vector<std::vector<P>> out;
      double total = 0;
      for (double d : pattern) total += d;
      if (pts.size() < 2 || total <= 1e-9) return out;
      // A pattern so fine that the line would be a hundred thousand dashes is a solid line (it took minutes, and no one can tell).
      double pathLength = 0;
      for (std::size_t i = 1; i < pts.size(); i++) pathLength += length(pts[i] - pts[i - 1]);
      if (pathLength / total * static_cast<double>(pattern.size()) > 50000.0) {
        out.push_back(pts);
        return out;
      }
      std::size_t k = 0;
      double into = std::fmod(std::max(0.0, phase), total);
      while (into >= pattern[k % pattern.size()]) {
        into -= pattern[k % pattern.size()];
        k++;
      }
      double leftOver = pattern[k % pattern.size()] - into;
      bool on = k % 2 == 0;
      std::vector<P> run;
      if (on) run.push_back(pts[0]);
      for (std::size_t i = 1; i < pts.size(); i++) {
        P a = pts[i - 1];
        const P b = pts[i];
        double seg = length(b - a);
        while (seg > leftOver) {
          const P cut = a + (b - a) * (leftOver / seg);
          if (on) {
            run.push_back(cut);
            out.push_back(run);
            run.clear();
          } else {
            run.assign(1, cut);
          }
          seg -= leftOver;
          a = cut;
          on = !on;
          k++;
          leftOver = pattern[k % pattern.size()];
          if (leftOver <= 1e-9 && seg <= 1e-9) break;
        }
        leftOver -= seg;
        if (on) run.push_back(b);
      }
      if (on && run.size() > 1) out.push_back(run);
      return out;
    }

    boxname(TikzBox);
  };

  auto box = sptrOf<TikzBox>();
  // Background layers first, in the order they were written within one.
  std::stable_sort(_prims.begin(), _prims.end(), [](const Prim& a, const Prim& b) { return a.layer < b.layer; });
  box->prims = std::move(_prims);
  box->origin = {_minx, _miny};
  box->_width = static_cast<float>(_maxx - _minx);
  box->_height = static_cast<float>(_maxy - _miny);
  box->_depth = 0.f;
  return box;
}

}  // namespace

TikzAtom::TikzAtom() = default;

sptr<Box> TikzAtom::createBox(Env& env) {
  Layout layout(*this, env);
  return layout.run();
}

}  // namespace microtex
