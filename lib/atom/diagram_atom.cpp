#include "atom/diagram_atom.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>

#include "atom/atom_row.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "env/env.h"
#include "env/units.h"
#include "graphic/graphic.h"

namespace microtex {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kPi = 3.14159265358979f;

// Geometry here is the screen's: x to the right, y down.
struct Pt {
  float x = 0.f, y = 0.f;
};

Pt operator+(Pt a, Pt b) { return {a.x + b.x, a.y + b.y}; }
Pt operator-(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
Pt operator*(Pt a, float k) { return {a.x * k, a.y * k}; }
float length(Pt a) { return std::sqrt(a.x * a.x + a.y * a.y); }
Pt unit(Pt a) {
  const float n = length(a);
  return n > 1e-6f ? Pt{a.x / n, a.y / n} : Pt{1.f, 0.f};
}
/** The side that is to the left of someone walking along `d`. */
Pt leftOf(Pt d) { return {d.y, -d.x}; }
/** `v` turned `deg` degrees anticlockwise, as seen on the screen. */
Pt turn(Pt v, float deg) {
  const float a = deg * kPi / 180.f;
  const float c = std::cos(a), s = std::sin(a);
  return {v.x * c + v.y * s, -v.x * s + v.y * c};
}

struct Seg {
  Pt a, b;
  bool tint = false;
  color col = 0;
  /** The line's width; 0 is the diagram's own. */
  float lw = 0.f;
};

/** A filled rectangle: a cell's `fill`, a label that has a background. */
struct Fill {
  float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
  color col = 0;
  /** The ellipse in the box. */
  bool round = false;
};

/** An arrow tip that is a polygon: arrows.meta's Latex and Stealth. */
struct Poly {
  std::vector<Pt> pts;
  bool fill = false, stroke = false, round = false;
  float lw = 0.f;
  bool tint = false;
  color col = 0;
};

struct Frame {
  float l = 0.f, t = 0.f, r = 0.f, b = 0.f;
  /** The node is the ellipse in the box, not the box. */
  bool round = false;
};

/** How far a ray from `p`, inside `r`, goes along the unit vector `d`
 *  before it leaves. */
float exitDistance(Pt p, Pt d, const Frame& r) {
  if (r.round) {
    // Where `p + t d` meets the ellipse in the box, t > 0 (p inside it).
    const float a = (r.r - r.l) / 2.f, b = (r.b - r.t) / 2.f;
    if (a <= 0.f || b <= 0.f) return 0.f;
    const Pt q{p.x - (r.l + r.r) / 2.f, p.y - (r.t + r.b) / 2.f};
    const float A = (d.x * d.x) / (a * a) + (d.y * d.y) / (b * b);
    const float B = 2.f * (q.x * d.x / (a * a) + q.y * d.y / (b * b));
    const float C = (q.x * q.x) / (a * a) + (q.y * q.y) / (b * b) - 1.f;
    const float disc = B * B - 4.f * A * C;
    if (A <= 0.f || disc < 0.f) return 0.f;
    return std::max(0.f, (-B + std::sqrt(disc)) / (2.f * A));
  }
  const float tx = d.x > 1e-6f ? (r.r - p.x) / d.x : d.x < -1e-6f ? (r.l - p.x) / d.x : kInf;
  const float ty = d.y > 1e-6f ? (r.b - p.y) / d.y : d.y < -1e-6f ? (r.t - p.y) / d.y : kInf;
  const float t = std::min(tx, ty);
  return std::isfinite(t) ? std::max(0.f, t) : 0.f;
}

/** The pieces of segment `s` that are outside every rectangle of `holes`. */
void clipOutside(const Seg& s, const std::vector<Frame>& holes, std::vector<Seg>& out) {
  std::vector<std::pair<float, float>> cut;
  const Pt d = s.b - s.a;
  for (const Frame& r : holes) {
    float t0 = 0.f, t1 = 1.f;
    const auto clip = [&](float p, float q) {
      // Liang-Barsky: the part of the segment where p * t <= q.
      if (std::fabs(p) < 1e-9f) return q >= 0.f;
      const float t = q / p;
      if (p < 0.f) {
        if (t > t1) return false;
        t0 = std::max(t0, t);
      } else {
        if (t < t0) return false;
        t1 = std::min(t1, t);
      }
      return true;
    };
    if (clip(-d.x, s.a.x - r.l) && clip(d.x, r.r - s.a.x) && clip(-d.y, s.a.y - r.t) &&
        clip(d.y, r.b - s.a.y) && t1 > t0) {
      cut.emplace_back(t0, t1);
    }
  }
  std::sort(cut.begin(), cut.end());
  float from = 0.f;
  const auto emit = [&](float t0, float t1) {
    if (t1 - t0 > 1e-4f) out.push_back({s.a + d * t0, s.a + d * t1});
  };
  for (const auto& c : cut) {
    if (c.first > from) emit(from, c.first);
    from = std::max(from, c.second);
  }
  emit(from, 1.f);
}

float pathLength(const std::vector<Pt>& p) {
  float n = 0.f;
  for (std::size_t i = 1; i < p.size(); i++) n += length(p[i] - p[i - 1]);
  return n;
}

/** The point at distance `s` along the path, and the direction there. */
Pt pointAt(const std::vector<Pt>& p, float s, Pt* dir) {
  for (std::size_t i = 1; i < p.size(); i++) {
    const float n = length(p[i] - p[i - 1]);
    if (s <= n || i + 1 == p.size()) {
      if (dir != nullptr) *dir = unit(p[i] - p[i - 1]);
      return n > 1e-6f ? p[i - 1] + (p[i] - p[i - 1]) * (std::min(s, n) / n) : p[i - 1];
    }
    s -= n;
  }
  if (dir != nullptr) *dir = Pt{1.f, 0.f};
  return p.empty() ? Pt{} : p[0];
}

/** The path with `from` taken off its start and `to` off its end; a negative one adds to it, straight on. */
std::vector<Pt> trimmed(const std::vector<Pt>& p, float from, float to) {
  const float total = pathLength(p);
  if (p.size() < 2 || std::max(from, 0.f) + std::max(to, 0.f) >= total) return {};
  std::vector<Pt> out;
  Pt dir;
  if (from < 0.f) {
    pointAt(p, 0.f, &dir);
    out.push_back(p.front() + dir * from);
  }
  out.push_back(pointAt(p, std::max(from, 0.f), nullptr));
  float walked = 0.f;
  for (std::size_t i = 1; i < p.size(); i++) {
    walked += length(p[i] - p[i - 1]);
    if (walked > std::max(from, 0.f) + 1e-4f && walked < total - std::max(to, 0.f) - 1e-4f) out.push_back(p[i]);
  }
  out.push_back(pointAt(p, total - std::max(to, 0.f), nullptr));
  if (to < 0.f) {
    pointAt(p, total, &dir);
    out.push_back(p.back() - dir * to);
  }
  return out;
}

/** The path moved `d` to its left, a point at a time. */
std::vector<Pt> offset(const std::vector<Pt>& p, float d) {
  std::vector<Pt> out;
  for (std::size_t i = 0; i < p.size(); i++) {
    const Pt before = i > 0 ? p[i] - p[i - 1] : p[i + 1] - p[i];
    const Pt after = i + 1 < p.size() ? p[i + 1] - p[i] : before;
    out.push_back(p[i] + leftOf(unit(unit(before) + unit(after))) * d);
  }
  return out;
}

/** The path as the pieces of a dash pattern (all of it when `on` is 0). */
void dashed(const std::vector<Pt>& p, float on, float off, std::vector<Seg>& out) {
  if (p.size() < 2) return;
  if (on <= 0.f) {
    for (std::size_t i = 1; i < p.size(); i++) out.push_back({p[i - 1], p[i]});
    return;
  }
  const float total = pathLength(p);
  for (float s = 0.f; s < total - 1e-4f; s += on + off) {
    const float end = std::min(total, s + on);
    std::vector<Pt> piece = trimmed(p, s, total - end);
    for (std::size_t i = 1; i < piece.size(); i++) out.push_back({piece[i - 1], piece[i]});
  }
}

/** The path as the pieces of a pattern of alternating on and off lengths, which repeats. */
void patterned(const std::vector<Pt>& p, const std::vector<float>& pattern, std::vector<Seg>& out) {
  float period = 0.f;
  for (const float v : pattern) period += v;
  if (p.size() < 2 || pattern.size() < 2 || period <= 1e-4f) {
    dashed(p, 0.f, 0.f, out);
    return;
  }
  const float total = pathLength(p);
  float at = 0.f;
  for (std::size_t k = 0; at < total - 1e-4f; k++) {
    const float len = pattern[k % pattern.size()];
    if (k % 2 == 0 && len > 0.f) {
      std::vector<Pt> piece = trimmed(p, at, std::max(0.f, total - std::min(total, at + len)));
      for (std::size_t i = 1; i < piece.size(); i++) out.push_back({piece[i - 1], piece[i]});
    } else if (k % 2 == 0) {
      // A dot of no length is a point, which a round cap makes a dot.
      const Pt q = pointAt(p, at, nullptr);
      out.push_back({q, q});
    }
    at += len;
  }
}

/** The box `f` and `by` more on every side. */
Frame grown(const Frame& f, float by) { return {f.l - by, f.t - by, f.r + by, f.b + by, f.round}; }

/** The point `x` along the way `u` goes from `at` and `y` to the right of it
 *  (down, when it goes right), both in line widths: the frame pgf draws an
 *  arrow tip in. */
Pt local(Pt at, Pt u, float lw, float x, float y) {
  return at + u * (x * lw) + Pt{-u.y, u.x} * (y * lw);
}

void cubic(Pt a, Pt b, Pt c, Pt d, std::vector<Seg>& out, int n = 10) {
  Pt prev = a;
  for (int k = 1; k <= n; k++) {
    const float t = static_cast<float>(k) / static_cast<float>(n), v = 1.f - t;
    const Pt p = a * (v * v * v) + b * (3.f * v * v * t) + c * (3.f * v * t * t) + d * (t * t * t);
    out.push_back({prev, p});
    prev = p;
  }
}

/** The Computer Modern arrow tip as pgf draws it (its curves, lifted from a
 *  LaTeX run, in line widths): the apex at `at`, pointing the way `u` goes.
 *  `implies` is the double arrow's, wider and longer. `side` draws one barb
 *  only: 1 the one to the left of the way it goes, -1 to the right. */
/** arrows.meta's Latex and Stealth in the frame of the tip (x along the arrow, y up), by pgf's formulas, as
 *  tikz_atom.cpp's tipGeom has them (checked there against pdfTeX); `lw` is the arrow's line width, `pt` a point. */
struct MetaShape {
  std::vector<Pt> pts;
  bool fill = false, stroke = false, round = false;
  float lw = 0.f;
  /** The origin is this far behind the end of the path, and the shaft stops this far ahead of it. */
  float tipEnd = 0.f, lineEnd = 0.f;
};

MetaShape metaShape(const MetaTip& tip, std::optional<float> length, std::optional<float> width, std::optional<float> inset,
                    std::optional<float> lineWidth, float lw, float pt) {
  MetaShape m;
  const float lwpt = lw / pt, sc = tip.scale;
  const float L = length.value_or((3.f + 4.5f * lwpt) * pt) * sc;
  const float W = width.value_or(0.75f * L / sc) * sc;
  float lwA = lineWidth.value_or(lw);
  const auto cubicPts = [&](Pt a, Pt b, Pt c, Pt d) {
    for (int k = 1; k <= 8; k++) {
      const float t = static_cast<float>(k) / 8.f, v = 1.f - t;
      m.pts.push_back(a * (v * v * v) + b * (3.f * v * v * t) + c * (3.f * v * t * t) + d * (t * t * t));
    }
  };
  if (tip.kind == MetaTip::Kind::latex) {
    if (0.2f * L < lwA) lwA = 0.2f * L;
    const float q = L / W;
    const float xc = lwA * std::sqrt(9.f * q * q + 1.f);
    const float a = L - 0.5f * xc - 0.5f * lwA;
    // the back corner's miter, from the normalised vector (.3 L, .2333 W)
    const Pt nv = unit({0.3f * L, 0.2333333f * W});
    const float ratio = (nv.y + 1.f) / nv.x;
    const float b = 0.5f * W - 0.5f * ratio * lwA;
    m.pts.push_back({a, 0.f});
    cubicPts({a, 0.f}, {0.877192f * a, 0.077922f * b}, {0.337381f * a, 0.51948f * b}, {0.f, b});
    m.pts.push_back({0.f, -b});
    cubicPts({0.f, -b}, {0.337381f * a, -0.51948f * b}, {0.877192f * a, -0.077922f * b}, {a, 0.f});
    m.pts.pop_back();
    m.tipEnd = tip.round ? a + 0.5f * lwA : L - 0.5f * lwA;
    m.lineEnd = 0.f;
  } else {
    const float I = inset.value_or(0.325f * L / sc) * sc;
    if (0.25f * (L - I) < lwA) lwA = 0.25f * (L - I);
    const float q = L / W;
    const float xc = 0.5f * lwA * std::sqrt(4.f * q * q + 1.f);
    const float A1 = std::atan2(L, 0.5f * W), A2 = std::atan2(I, 0.5f * W);
    const float half = 0.5f * (A1 - A2);
    const float yc0 = 0.5f * lwA / std::tan(half);
    const float ang = A2 + half;
    float ya = std::sin(ang) * yc0;
    const float yb = std::cos(ang) * yc0;
    if (I == 0.f) ya = 0.5f * lwA;
    const float q2 = I / W;
    const float ycInset = 0.5f * lwA * std::sqrt(4.f * q2 * q2 + 1.f);
    const float inner = L - xc - ya;
    const float h = 0.5f * W - yb;
    m.pts = {{inner + ya, 0.f}, {ya, h}, {I + ycInset, 0.f}, {ya, -h}};
    m.tipEnd = tip.round ? inner + ya + 0.5f * lwA : L;
    m.lineEnd = I + ycInset - 0.25f * lwA;
  }
  m.fill = !tip.open;
  m.stroke = lwA > 0.f;
  m.lw = lwA;
  m.round = tip.round;
  return m;
}

void tip(Pt at, Pt u, float lw, bool implies, int side, std::vector<Seg>& out) {
  const float x0 = implies ? -8.236f : -5.2006f, y0 = implies ? 6.417f : 6.0f;
  const float x1 = implies ? -6.663f : -4.2501f, y1 = implies ? 3.026f : 2.4003f;
  const float x2 = implies ? -2.424f : -2.1331f, y2 = implies ? 0.120f : 0.7002f;
  // Up is to the left of the way it goes: a negative y.
  if (side >= 0) {
    cubic(local(at, u, lw, x0, -y0), local(at, u, lw, x1, -y1), local(at, u, lw, x2, -y2), at, out);
  }
  if (side <= 0) {
    cubic(at, local(at, u, lw, x2, y2), local(at, u, lw, x1, y1), local(at, u, lw, x0, y0), out);
  }
}

/** A hook that ends at `at`, the start of the shaft, curling to the left
 *  of the way it goes (`side` -1) or the right (1): 3.1 line widths back
 *  and 4.9 across. */
void hook(Pt at, Pt u, float lw, float side, std::vector<Seg>& out) {
  const auto p = [&](float x, float y) { return local(at, u, lw, x, side * y); };
  cubic(p(0.f, 4.9f), p(-1.712f, 4.9f), p(-3.1f, 3.803f), p(-3.1f, 2.45f), out, 8);
  cubic(p(-3.1f, 2.45f), p(-3.1f, 1.097f), p(-1.712f, 0.f), p(0.f, 0.f), out, 8);
}

/** The shaft as pgf's zigzag decoration draws it, for tikz-cd's `squiggly`:
 *  straight for `ends` at each end, between them a triangle wave of the
 *  given amplitude and half period (stretched to a whole number), whose
 *  first peak is on the left of the way it goes. */
std::vector<Pt> zigzag(const std::vector<Pt>& p, float amp, float half, float ends) {
  const float total = pathLength(p);
  const float region = total - 2.f * ends;
  if (p.size() < 2 || region < 2.f * half) return p;
  const int n = std::max(2, static_cast<int>(std::lround(region / half)));
  const float step = region / static_cast<float>(n);
  std::vector<Pt> out{p.front(), pointAt(p, ends, nullptr)};
  for (int k = 0; k < n; k++) {
    Pt dir;
    const Pt q = pointAt(p, ends + (static_cast<float>(k) + 0.5f) * step, &dir);
    out.push_back(q + leftOf(dir) * ((k % 2 == 0) ? amp : -amp));
  }
  out.push_back(pointAt(p, ends + region, nullptr));
  out.push_back(p.back());
  return out;
}

float distance(Pt q, Pt a, Pt b) {
  const Pt d = b - a;
  const float n = d.x * d.x + d.y * d.y;
  const float t = n > 1e-9f ? std::min(1.f, std::max(0.f, ((q.x - a.x) * d.x + (q.y - a.y) * d.y) / n)) : 0.f;
  return length(q - (a + d * t));
}

/** The pieces of `in` farther than `half` from `path`: a gap cut in them
 *  where it crosses, as tikz-cd's `crossing over` makes. */
std::vector<Seg> cutBand(const std::vector<Seg>& in, const std::vector<Pt>& path, float half) {
  std::vector<Seg> out;
  for (const Seg& s : in) {
    const int n = std::max(1, static_cast<int>(std::ceil(length(s.b - s.a) / (half / 2.f))));
    const auto outside = [&](int i) {
      const Pt q = s.a + (s.b - s.a) * ((static_cast<float>(i) + 0.5f) / static_cast<float>(n));
      for (std::size_t k = 1; k < path.size(); k++) {
        if (distance(q, path[k - 1], path[k]) <= half) return false;
      }
      return true;
    };
    for (int i = 0; i < n;) {
      if (!outside(i)) {
        i++;
        continue;
      }
      int j = i;
      while (j < n && outside(j)) j++;
      Seg piece = s;
      piece.a = s.a + (s.b - s.a) * (static_cast<float>(i) / static_cast<float>(n));
      piece.b = s.a + (s.b - s.a) * (static_cast<float>(j) / static_cast<float>(n));
      out.push_back(piece);
      i = j;
    }
  }
  return out;
}

class DiagramBox : public Box {
public:
  struct Placed {
    sptr<Box> box;
    float x = 0.f, y = 0.f;
    /** Turned this many degrees, anticlockwise, about (px, py) of the box's own frame (its baseline-left origin). */
    float angle = 0.f, px = 0.f, py = 0.f;
  };
  std::vector<Placed> children;
  std::vector<Seg> segments;
  std::vector<Fill> fills;
  std::vector<Poly> polys;
  float thickness = 0.f;

  void draw(Graphics2D& g2, float x, float y) override {
    const color oldColor = g2.getColor();
    for (const Fill& f : fills) {
      g2.setColor(f.col);
      if (!f.round) {
        g2.fillRect(x + f.x, y + f.y, f.w, f.h);
        continue;
      }
      g2.beginPath(-1);
      for (int k = 0; k < 64; k++) {
        const float t = 2.f * kPi * static_cast<float>(k) / 64.f;
        const float px = x + f.x + f.w / 2.f * (1.f + std::cos(t)), py = y + f.y + f.h / 2.f * (1.f + std::sin(t));
        if (k == 0) g2.moveTo(px, py);
        else g2.lineTo(px, py);
      }
      g2.closePath();
      g2.fillPath(-1);
    }
    g2.setColor(oldColor);
    for (const Placed& c : children) {
      if (c.angle == 0.f) {
        c.box->draw(g2, x + c.x, y + c.y);
        continue;
      }
      const float cx = x + c.x + c.px, cy = y + c.y + c.py;
      g2.rotate(-c.angle * kPi / 180.f, cx, cy);
      c.box->draw(g2, x + c.x, y + c.y);
      g2.rotate(c.angle * kPi / 180.f, cx, cy);
    }
    const Stroke old = g2.getStroke();
    float width = -1.f;
    for (const Seg& s : segments) {
      const float w = s.lw > 0.f ? s.lw : thickness;
      if (w != width) {
        g2.setStroke(Stroke(w, CAP_ROUND, JOIN_ROUND));
        width = w;
      }
      if (s.tint) g2.setColor(s.col);
      g2.drawLine(x + s.a.x, y + s.a.y, x + s.b.x, y + s.b.y);
      if (s.tint) g2.setColor(oldColor);
    }
    for (const Poly& p : polys) {
      if (p.pts.size() < 3) continue;
      if (p.tint) g2.setColor(p.col);
      const auto trace = [&] {
        g2.beginPath(-1);
        for (std::size_t k = 0; k < p.pts.size(); k++) {
          if (k == 0) g2.moveTo(x + p.pts[k].x, y + p.pts[k].y);
          else g2.lineTo(x + p.pts[k].x, y + p.pts[k].y);
        }
        g2.closePath();
      };
      if (p.fill) {
        trace();
        g2.fillPath(-1);
      }
      if (p.stroke) {
        g2.setStroke(Stroke(p.lw, CAP_BUTT, p.round ? JOIN_ROUND : JOIN_MITER, 10.f));
        trace();
        g2.strokePath(-1);
      }
      if (p.tint) g2.setColor(oldColor);
    }
    g2.setStroke(old);
  }

  std::vector<sptr<Box>> descendants() const override {
    std::vector<sptr<Box>> out;
    for (const Placed& c : children) out.push_back(c.box);
    return out;
  }

  boxname(DiagramBox);
};

}  // namespace

DiagramAtom::DiagramAtom() {
  // tikz-cd's own: `row sep=normal` and `column sep=normal`, and a cell's
  // `inner xsep` and `inner ysep`.
  rowSep = Units::getDimen("1.8em");
  colSep = Units::getDimen("2.4em");
  innerX = Units::getDimen("1ex");
  innerY = Units::getDimen("0.85ex");
}

namespace {

std::string trimmedName(const std::string& s) {
  std::size_t from = 0, to = s.size();
  while (from < to && std::isspace(static_cast<unsigned char>(s[from])) != 0) from++;
  while (to > from && std::isspace(static_cast<unsigned char>(s[to - 1])) != 0) to--;
  return s.substr(from, to - from);
}

/** The offset of a label node's centre from the point its `anchor` stands on, in half sizes (y down). */
Pt anchorShift(DiagramLabel::Anchor a, float hw, float hh) {
  using L = DiagramLabel::Anchor;
  switch (a) {
    case L::north: return {0.f, hh};
    case L::south: return {0.f, -hh};
    case L::east: return {-hw, 0.f};
    case L::west: return {hw, 0.f};
    case L::northEast: return {-hw, hh};
    case L::northWest: return {hw, hh};
    case L::southEast: return {-hw, -hh};
    case L::southWest: return {hw, -hh};
    default: return {0.f, 0.f};
  }
}

}  // namespace

sptr<Box> DiagramAtom::createBox(Env& env) {
  const auto size = [&](const Dimen& d) { return d.isValid() ? Units::fsize(d, env) : 0.f; };
  const float em = Units::fsize(Units::getDimen("1em"), env);
  // `cramped`: 0.3em in a cell, and none around the diagram's cells.
  const float padX = cramped ? 0.3f * em : size(innerX), padY = cramped ? 0.3f * em : size(innerY);
  const float trim = cramped ? 0.3f * em : 0.f;
  const float axis = env.mathConsts().axisHeight() * env.scale();
  // As tikz-cd: the line width is the math font's rule thickness, and the heads,
  // hooks and dashes are measured in it.
  const float lw = env.mathConsts().fractionRuleThickness() * env.scale();
  const float thick = lw;
  const float ex = Units::fsize(Units::getDimen("1ex"), env);
  const float pt = Units::fsize(Units::getDimen("1pt"), env);
  // How far above its baseline a cell's anchors are (tikz-cd's `center yshift`).
  const float centreY = centerShift.isValid() ? size(centerShift) : axis;

  const int rows = static_cast<int>(cells.size());
  int cols = 0;
  for (const auto& row : cells) cols = std::max(cols, static_cast<int>(row.size()));
  auto result = sptrOf<DiagramBox>();
  if (rows == 0 || cols == 0) return result;

  const auto cellOpt = [&](int r, int c) -> const DiagramCell* {
    if (r < static_cast<int>(cellOptions.size()) && c < static_cast<int>(cellOptions[r].size())) return &cellOptions[r][c];
    return nullptr;
  };
  const auto extra = [&](const std::vector<Dimen>& v, int i) { return i >= 0 && i < static_cast<int>(v.size()) ? size(v[i]) : 0.f; };

  // The cells, and their boxes: the content and the space around it.
  std::vector<std::vector<sptr<Box>>> content(rows, std::vector<sptr<Box>>(cols));
  std::vector<std::vector<float>> w(rows, std::vector<float>(cols)), h(rows, std::vector<float>(cols)),
    d(rows, std::vector<float>(cols));
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const DiagramCell* o = cellOpt(r, c);
      w[r][c] = 2.f * (o != nullptr && o->innerX.isValid() ? size(o->innerX) : padX);
      h[r][c] = d[r][c] = o != nullptr && o->innerY.isValid() ? size(o->innerY) : padY;
    }
  }
  std::vector<float> colWidth(cols, 0.f), rowHeight(rows, 0.f), rowDepth(rows, 0.f);
  // How far above its baseline a cell's node is centred: tikz-cd's `center yshift`, or, for a node of a shape, the
  // middle of its text box.
  std::vector<std::vector<float>> centreOf(rows, std::vector<float>(cols, centreY));
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < static_cast<int>(cells[r].size()); c++) {
      if (cells[r][c] == nullptr) continue;
      const sptr<Box> b = RowAtom::withItalic(cells[r][c])->createBox(env);
      content[r][c] = b;
      w[r][c] += b->_width;
      h[r][c] += b->_height;
      d[r][c] += b->_depth;
      // A node's own shape and least size, as pgf sets them: round the text and its `inner sep` (a circle
      // reaches its corners, an ellipse is the box times the square root of two), and at least as big as asked.
      const DiagramCell* o = cellOpt(r, c);
      if (o == nullptr) continue;
      const float minW = o->minWidth.isValid() ? size(o->minWidth) : 0.f;
      const float minH = o->minHeight.isValid() ? size(o->minHeight) : 0.f;
      if (o->shape == DiagramCell::Shape::rectangle) {
        if (minW > w[r][c]) w[r][c] = minW;
        const float more = minH - (h[r][c] + d[r][c]);
        if (more > 0.f) {
          h[r][c] += more / 2.f;
          d[r][c] += more / 2.f;
        }
        continue;
      }
      float rx = w[r][c] / 2.f, ry = (h[r][c] + d[r][c]) / 2.f;
      if (o->shape == DiagramCell::Shape::circle) {
        rx = ry = std::sqrt(rx * rx + ry * ry);
        rx = ry = std::max(rx, std::max(minW, minH) / 2.f);
      } else {
        rx = std::max(rx * 1.41421356f, minW / 2.f);
        ry = std::max(ry * 1.41421356f, minH / 2.f);
      }
      // The outline is centred on the text box, which stays at its baseline.
      const float mid = (h[r][c] - d[r][c]) / 2.f;
      centreOf[r][c] = mid;
      w[r][c] = 2.f * rx;
      h[r][c] = mid + ry;
      d[r][c] = ry - mid;
    }
    for (int c = 0; c < cols; c++) {
      colWidth[c] = std::max(colWidth[c], w[r][c]);
      rowHeight[r] = std::max(rowHeight[r], h[r][c]);
      rowDepth[r] = std::max(rowDepth[r], d[r][c]);
    }
  }

  // In TeX's rows a cell is as wide as its column, whose edges the arrows leave from.
  if (texRows) {
    for (int r = 0; r < rows; r++) {
      for (int c = 0; c < cols; c++) w[r][c] = colWidth[c];
    }
  }

  // The labels, small as a script is unless they say otherwise, and the room they and the arrows'
  // least lengths ask of the columns.
  std::vector<std::vector<sptr<Box>>> labelBox(arrows.size());
  std::vector<float> gap(std::max(cols - 1, 0), size(colSep));
  for (int c = 1; c < cols; c++) gap[c - 1] += extra(columnExtra, c);
  // texRows: the rows of arrows between the object rows, as TeX sets them. A
  // vertical arrow is 1.8em, centred on the axis of its row.
  std::vector<float> arrowRowHeight(std::max(rows - 1, 0), 1.15f * em), arrowRowDepth(std::max(rows - 1, 0), 0.65f * em);
  std::vector<bool> hasVertical(std::max(rows - 1, 0), false);
  std::vector<float> arrowRowBase(std::max(rows - 1, 0), 0.f);
  const auto inside = [&](int r, int c) { return r >= 0 && r < rows && c >= 0 && c < cols; };
  for (std::size_t i = 0; i < arrows.size(); i++) {
    const DiagramArrow& a = arrows[i];
    float widest = 0.f;
    for (const DiagramLabel& l : a.labels) {
      const auto make = [&](Env& e) { return l.body == nullptr ? sptr<Box>() : l.body->createBox(e); };
      sptr<Box> b = l.fullSize ? make(env) : env.withStyle(TexStyle::script, make);
      if (b != nullptr && l.fontScale != 1.f) b = sptrOf<ScaleBox>(b, l.fontScale);
      if (b == nullptr && (l.point || !l.name.empty())) b = sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
      if (b != nullptr) widest = std::max(widest, b->_width);
      labelBox[i].push_back(b);
    }
    if (texRows && inside(a.row, a.col) && inside(a.toRow, a.toCol)) {
      if (a.row == a.toRow) {
        // A label over or under an arrow, the way TeX sets limits: the
        // baseline of the one above is 0.278em + 0.2em over the row's
        // baseline, plus the label's depth; of the one below, 0.6em under it
        // or 1/6em and the label's height.
        for (std::size_t k = 0; k < a.labels.size(); k++) {
          const sptr<Box>& b = labelBox[i][k];
          if (b == nullptr) continue;
          const bool right = a.toCol > a.col;
          if ((a.labels[k].side == DiagramLabel::Side::left) == right) {
            rowHeight[a.row] = std::max(rowHeight[a.row], 0.478f * em + b->_depth + b->_height);
          } else {
            rowDepth[a.row] = std::max(rowDepth[a.row], std::max(em / 6.f + b->_height, 0.6f * em) + b->_depth);
          }
        }
      } else if (a.col == a.toCol && std::abs(a.row - a.toRow) == 1) {
        const int g = std::min(a.row, a.toRow);
        for (const sptr<Box>& b : labelBox[i]) {
          if (b == nullptr) continue;
          arrowRowHeight[g] = std::max(arrowRowHeight[g], b->_height);
          arrowRowDepth[g] = std::max(arrowRowDepth[g], b->_depth);
        }
        hasVertical[g] = true;
      }
    }
    if (!inside(a.row, a.col) || !inside(a.toRow, a.toCol) || a.row != a.toRow ||
        std::abs(a.col - a.toCol) != 1 || !a.minLength.isValid()) {
      continue;
    }
    float need = size(a.minLength);
    if (a.fitLabels && widest > 0.f) need = std::max(need, widest + 5.f * em / 6.f);
    const int c = std::min(a.col, a.toCol);
    const float spare = (colWidth[c] - w[a.row][c]) / 2.f + (colWidth[c + 1] - w[a.row][c + 1]) / 2.f;
    gap[c] = std::max(gap[c], need - spare);
  }

  // Columns are centred on their cells, rows share the cells' baselines.
  std::vector<float> colX(cols), base(rows);
  colX[0] = colWidth[0] / 2.f;
  for (int c = 1; c < cols; c++) colX[c] = colX[c - 1] + colWidth[c - 1] / 2.f + gap[c - 1] + colWidth[c] / 2.f;
  base[0] = rowHeight[0];
  for (int r = 1; r < rows; r++) {
    if (texRows) {
      // Two rows, TeX's: each at least a baselineskip (2em) after the one
      // before, or a lineskip (0.4em) from its neighbour's depth to its height.
      const int g = r - 1;
      const float a = hasVertical[g] ? arrowRowHeight[g] : 0.f, b = hasVertical[g] ? arrowRowDepth[g] : 0.f;
      const float first = std::max(2.f * em, rowDepth[g] + 0.4f * em + a);
      const float second = std::max(2.f * em, b + 0.4f * em + rowHeight[r]);
      arrowRowBase[g] = base[g] + first;
      base[r] = base[g] + first + second;
    } else {
      base[r] = base[r - 1] + std::max(size(minRowPitch), rowDepth[r - 1] + size(rowSep) + rowHeight[r]) + extra(rowExtra, r);
    }
  }
  const auto cellRect = [&](int r, int c) {
    const DiagramCell* o = cellOpt(r, c);
    return Frame{colX[c] - w[r][c] / 2.f, base[r] - h[r][c], colX[c] + w[r][c] / 2.f, base[r] + d[r][c],
                 o != nullptr && o->shape != DiagramCell::Shape::rectangle};
  };

  // The extent of everything drawn.
  float minX = trim, minY = trim;
  float maxY = base[rows - 1] + rowDepth[rows - 1] - trim;
  float maxX = colX[cols - 1] + colWidth[cols - 1] / 2.f - trim;
  const auto grow = [&](Pt p, float by) {
    minX = std::min(minX, p.x - by);
    maxX = std::max(maxX, p.x + by);
    minY = std::min(minY, p.y - by);
    maxY = std::max(maxY, p.y + by);
  };

  struct Label {
    sptr<Box> box;
    Pt at;  // its left edge, at its baseline
    float angle = 0.f;
  };
  std::vector<Label> placedLabels;
  std::vector<Fill> fills;
  std::vector<Poly> polys;     // arrows.meta's tips
  std::vector<Seg> extraSegs;  // outlines of cells and labels
  std::vector<std::vector<Seg>> perArrow(arrows.size());
  std::vector<std::vector<Pt>> paths(arrows.size());
  // A label that has a name is a node other arrows can start from and end at.
  std::map<std::string, Frame> labelNodes;
  std::vector<float> arrowWidth(arrows.size(), lw);

  // What an arrow runs between: a cell's node, or a named label's.
  struct NodeRef {
    Frame inner, outer;
    Pt centre;
    bool isCell = false;
    int r = 0, c = 0;
  };
  const float outerSep = texRows ? 0.f : lw / 2.f;
  const auto cellNode = [&](int r, int c) {
    NodeRef n;
    n.isCell = true;
    n.r = r;
    n.c = c;
    n.inner = cellRect(r, c);
    n.outer = grown(n.inner, outerSep);
    n.centre = {colX[c], base[r] - centreOf[r][c]};
    return n;
  };
  const auto labelNode = [&](const Frame& f) {
    NodeRef n;
    n.inner = f;
    n.outer = grown(f, outerSep);
    n.centre = {(f.l + f.r) / 2.f, (f.t + f.b) / 2.f};
    return n;
  };
  // A point of a node, pgf's anchors of the asymmetrical rectangle a cell is (`east` and `west` at the
  // height of `center`, the `real` ones at the box's middle), or the border at an angle.
  const auto anchorPoint = [&](const NodeRef& n, const std::string& given, Pt& out) {
    const std::string name = trimmedName(given);
    const Frame& f = n.outer;
    const float cx = n.centre.x, cy = n.centre.y;
    const float baseY = n.isCell ? base[n.r] : (f.t + f.b) / 2.f;
    const float realY = (f.t + f.b) / 2.f, midX = (f.l + f.r) / 2.f;
    if (name == "center") out = {cx, cy};
    else if (name == "north") out = {cx, f.t};
    else if (name == "south") out = {cx, f.b};
    else if (name == "east") out = {f.r, cy};
    else if (name == "west") out = {f.l, cy};
    else if (f.round && (name == "north east" || name == "north west" || name == "south east" || name == "south west")) {
      // The border of an ellipse in a diagonal direction, as TikZ's compass anchors of a circle are.
      const float k = 0.70710678f;
      const float sx = name.find("east") != std::string::npos ? 1.f : -1.f;
      const float sy = name.find("north") != std::string::npos ? -1.f : 1.f;
      out = {midX + sx * k * (f.r - f.l) / 2.f, realY + sy * k * (f.b - f.t) / 2.f};
    }
    else if (name == "north east") out = {f.r, f.t};
    else if (name == "north west") out = {f.l, f.t};
    else if (name == "south east") out = {f.r, f.b};
    else if (name == "south west") out = {f.l, f.b};
    else if (name == "real center") out = {midX, realY};
    else if (name == "real east") out = {f.r, realY};
    else if (name == "real west") out = {f.l, realY};
    else if (name == "base") out = {cx, baseY};
    else if (name == "base east") out = {f.r, baseY};
    else if (name == "base west") out = {f.l, baseY};
    else if (name == "mid") out = {cx, baseY - ex / 2.f};
    else if (name == "mid east") out = {f.r, baseY - ex / 2.f};
    else if (name == "mid west") out = {f.l, baseY - ex / 2.f};
    else {
      char* end = nullptr;
      const float deg = std::strtof(name.c_str(), &end);
      if (name.empty() || end == name.c_str()) return false;
      const Pt dir{std::cos(deg * kPi / 180.f), -std::sin(deg * kPi / 180.f)};
      out = n.centre + dir * exitDistance(n.centre, dir, f);
    }
    return true;
  };

  // An arrow is laid out when the nodes it names exist: those that start at a label wait for it to be placed.
  std::vector<std::size_t> order;
  for (std::size_t i = 0; i < arrows.size(); i++) {
    if (arrows[i].fromName.empty() && arrows[i].toName.empty()) order.push_back(i);
  }
  for (std::size_t i = 0; i < arrows.size(); i++) {
    if (!arrows[i].fromName.empty() || !arrows[i].toName.empty()) order.push_back(i);
  }

  for (const std::size_t i : order) {
    const DiagramArrow& a = arrows[i];
    const bool named = !a.fromName.empty() || !a.toName.empty();
    if (!named && (!inside(a.row, a.col) || !inside(a.toRow, a.toCol))) continue;
    const bool looping = a.row == a.toRow && a.col == a.toCol && a.fromName.empty() && a.toName.empty();
    if (looping && !(a.hasOut || a.hasIn || a.hasControls)) continue;
    NodeRef from, to;
    const auto find = [&](const std::string& name, int r, int c, NodeRef& out) {
      if (name.empty()) {
        if (!inside(r, c)) return false;
        out = cellNode(r, c);
        return true;
      }
      const auto it = labelNodes.find(name);
      if (it == labelNodes.end()) return false;
      out = labelNode(it->second);
      return true;
    };
    if (!find(a.fromName, a.row, a.col, from) || !find(a.toName, a.toRow, a.toCol, to)) continue;

    std::vector<Seg>& segs = perArrow[i];
    const float lwA = a.lineWidth.isValid() ? std::max(size(a.lineWidth), 0.01f) : lw;
    arrowWidth[i] = lwA;

    // The two ends: a cell's centre, clipped at its outer border toward the other end, or an anchor, exactly.
    Pt fromPt = from.centre, toPt = to.centre;
    const Frame* fromFrame = &from.outer;
    const Frame* toFrame = &to.outer;
    if (!a.startAnchor.empty() && anchorPoint(from, a.startAnchor, fromPt)) {
      fromPt = fromPt + Pt{size(a.startDx), -size(a.startDy)};
      fromFrame = nullptr;
    }
    if (!a.endAnchor.empty() && anchorPoint(to, a.endAnchor, toPt)) {
      toPt = toPt + Pt{size(a.endDx), -size(a.endDy)};
      toFrame = nullptr;
    }

    // The path, as TeX draws it: from the cells' outer borders (the box and half a line width of node
    // `outer sep`), straight, or a curve leaving each at an angle to the straight one, its controls 0.3915 of
    // the distance between the two ends out along those angles.
    const auto build = [&](Pt fp, Pt tp, const Frame* ff, const Frame* tf) {
      const Pt chord = tp - fp;
      const float dist = length(chord);
      const Pt u = unit(chord);
      const auto clipAt = [&](Pt p, Pt dir, const Frame* f) { return f != nullptr ? p + unit(dir) * exitDistance(p, unit(dir), *f) : p; };
      std::vector<Pt> path;
      const auto bezier = [&](Pt s, Pt c1, Pt c2, Pt e) {
        for (int k = 0; k <= 48; k++) {
          const float t = static_cast<float>(k) / 48.f, v = 1.f - t;
          path.push_back(s * (v * v * v) + c1 * (3.f * v * v * t) + c2 * (3.f * v * t * t) + e * (t * t * t));
        }
      };
      if (a.hasControls) {
        const Pt r1 = fp + Pt{size(a.control1x), -size(a.control1y)}, r2 = tp + Pt{size(a.control2x), -size(a.control2y)};
        bezier(clipAt(fp, r1 - fp, ff), r1, r2, clipAt(tp, r2 - tp, tf));
      } else if (a.hasOut || a.hasIn) {
        // Absolute angles, from the east, anticlockwise; the controls at 0.3915 of the distance between the
        // points on the borders, at least `min distance` (a loop is a curve from a node to itself).
        const float oa = (a.hasOut ? a.outAngle : 45.f) * kPi / 180.f, ia = (a.hasIn ? a.inAngle : 135.f) * kPi / 180.f;
        const Pt dout{std::cos(oa), -std::sin(oa)}, din{std::cos(ia), -std::sin(ia)};
        const Pt s = clipAt(fp, dout, ff), e = clipAt(tp, din, tf);
        float k = 0.3915f * a.looseness * length(e - s);
        if (a.minDistance.isValid()) k = std::max(k, size(a.minDistance));
        if (a.maxDistance.isValid()) k = std::min(k, size(a.maxDistance));
        bezier(s, s + dout * k, e + din * k, e);
      } else if (a.bend != 0.f) {
        const Pt rough1 = fp + turn(u, a.bend) * (0.3915f * a.looseness * dist);
        const Pt rough2 = tp + turn(u * -1.f, -a.bend) * (0.3915f * a.looseness * dist);
        const Pt s = clipAt(fp, rough1 - fp, ff), e = clipAt(tp, rough2 - tp, tf);
        const float k = 0.3915f * a.looseness * length(e - s);
        bezier(s, s + unit(rough1 - fp) * k, e + unit(rough2 - tp) * k, e);
      } else if (a.route != DiagramArrow::Route::direct) {
        const Pt corner = a.route == DiagramArrow::Route::verticalFirst ? Pt{fp.x, tp.y} : Pt{tp.x, fp.y};
        const Pt s = length(corner - fp) < 1e-3f ? fp : clipAt(fp, corner - fp, ff);
        const Pt e = length(corner - tp) < 1e-3f ? tp : clipAt(tp, corner - tp, tf);
        if (!a.rounded || length(corner - s) < 1e-3f || length(e - corner) < 1e-3f) {
          path = {s, corner, e};
        } else {
          // The corner is rounded by a curve that starts `radius` before it and ends as far after.
          const float r = std::min(a.cornerRadius.isValid() ? size(a.cornerRadius) : 4.f * pt,
                                   std::min(length(corner - s), length(e - corner)) / 2.f);
          const Pt p0 = corner - unit(corner - s) * r, p2 = corner + unit(e - corner) * r;
          path.push_back(s);
          for (int k = 0; k <= 12; k++) {
            const float t = static_cast<float>(k) / 12.f, v = 1.f - t;
            path.push_back(p0 * (v * v) + corner * (2.f * v * t) + p2 * (t * t));
          }
          path.push_back(e);
        }
      } else {
        Pt s = clipAt(fp, u, ff), e = tf != nullptr ? tp - u * exitDistance(tp, u * -1.f, *tf) : tp;
        if (a.fixedLength.isValid() && length(e - s) > size(a.fixedLength)) {
          Pt mid = (s + e) * 0.5f;
          // In TeX's rows the arrow is centred on its own row's axis.
          if (texRows && a.col == a.toCol) mid.y = arrowRowBase[std::min(a.row, a.toRow)] - axis;
          s = mid - u * (size(a.fixedLength) / 2.f);
          e = mid + u * (size(a.fixedLength) / 2.f);
        }
        path = {s, e};
      }
      return path;
    };
    std::vector<Pt> path = build(fromPt, toPt, fromFrame, toFrame);
    if (path.size() < 2) continue;
    const Pt chordDir = unit(to.centre - from.centre);
    // `start anchor={[xshift=-1ex]}` with no anchor moves where the path starts; the curve is drawn again
    // between the new ends, as tikz-cd does with the points it has put there.
    if ((a.startAnchor.empty() && (a.startDx.isValid() || a.startDy.isValid())) ||
        (a.endAnchor.empty() && (a.endDx.isValid() || a.endDy.isValid()))) {
      const Pt s = path.front() + (a.startAnchor.empty() ? Pt{size(a.startDx), -size(a.startDy)} : Pt{});
      const Pt e = path.back() + (a.endAnchor.empty() ? Pt{size(a.endDx), -size(a.endDy)} : Pt{});
      path = build(s, e, nullptr, nullptr);
      if (path.size() < 2) continue;
    }
    Pt by{};
    if (a.shiftLength.isValid()) by = by + leftOf(chordDir) * size(a.shiftLength);
    else if (a.shift != 0.f) by = by + leftOf(chordDir) * (a.shift * 0.56f * ex);
    if (a.xshift.isValid() || a.yshift.isValid()) by = by + Pt{size(a.xshift), -size(a.yshift)};
    if (by.x != 0.f || by.y != 0.f) {
      for (Pt& p : path) p = p + by;
    }
    if (a.shortenStart.isValid() || a.shortenEnd.isValid()) {
      const std::vector<Pt> shorter = trimmed(path, size(a.shortenStart), size(a.shortenEnd));
      if (!shorter.empty()) path = shorter;
    }
    const float total = pathLength(path);

    // The labels: beside the path, a box that touches it, or on it.
    std::vector<Frame> holes;
    for (std::size_t k = 0; k < a.labels.size(); k++) {
      const sptr<Box>& b = labelBox[i][k];
      if (b == nullptr) continue;
      const DiagramLabel& l = a.labels[k];
      const float inner = l.point ? 0.f : l.innerSep.isValid() ? size(l.innerSep) : ex / 2.f;
      const float hw = b->_width / 2.f + inner, hh = (b->_height + b->_depth) / 2.f + inner;
      Pt dir;
      const Pt at = pointAt(path, total * std::min(std::max(l.pos, 0.f), 1.f), &dir);
      Pt centre = at;
      if (texRows && !a.phantom && l.side != DiagramLabel::Side::center) {
        // amscd's: over and under a horizontal arrow as limits are set, and a
        // vertical arrow's on the baseline of its row, 3.8pt from its stem.
        const Pt n = leftOf(dir) * (l.side == DiagramLabel::Side::left ? 1.f : -1.f);
        float x = at.x - b->_width / 2.f, y;
        if (a.row == a.toRow) {
          const float rowBase = base[a.row];
          y = n.y < 0.f ? rowBase - (0.478f * em + b->_depth) : rowBase + std::max(em / 6.f + b->_height, 0.6f * em);
        } else {
          y = arrowRowBase[std::min(a.row, a.toRow)];
          x = n.x > 0.f ? at.x + 0.38f * em : at.x - 0.38f * em - b->_width;
        }
        placedLabels.push_back({b, {x, y}, 0.f});
        grow({x, y - b->_height}, 0.f);
        grow({x + b->_width, y + b->_depth}, 0.f);
        continue;
      }
      // A phantom arrow's labels are on its path, as tikz-cd's phantom style has it.
      const DiagramLabel::Side side = a.phantom ? DiagramLabel::Side::center : l.side;
      // Sloped: turned to the path, and kept upright; the label then stands off it by half its height.
      float angle = l.rotate;
      if (l.sloped) {
        float slope = std::atan2(-dir.y, dir.x) * 180.f / kPi;
        if (slope > 90.f) slope -= 180.f;
        if (slope <= -90.f) slope += 180.f;
        angle += slope;
      }
      if (l.anchor != DiagramLabel::Anchor::none) {
        const Pt off = anchorShift(l.anchor, hw, hh);
        centre = at + off;
      } else if (side != DiagramLabel::Side::center) {
        const Pt n = leftOf(dir) * (side == DiagramLabel::Side::left ? 1.f : -1.f);
        if (l.sloped) {
          centre = at + n * hh;
        } else {
          const float tx = std::fabs(n.x) > 1e-6f ? hw / std::fabs(n.x) : kInf;
          const float ty = std::fabs(n.y) > 1e-6f ? hh / std::fabs(n.y) : kInf;
          centre = at + n * std::min(tx, ty);
        }
      }
      centre = centre + Pt{size(l.xshift), -size(l.yshift)};
      // A label with a background (`description`) interrupts the arrow it is on; one that has none (`marking`) is
      // drawn over it.
      if (l.anchor == DiagramLabel::Anchor::none && side == DiagramLabel::Side::center && !a.phantom &&
          (l.hasFill || l.interrupts)) {
        holes.push_back({centre.x - hw, centre.y - hh, centre.x + hw, centre.y + hh});
      }
      const Frame node{centre.x - hw, centre.y - hh, centre.x + hw, centre.y + hh};
      if (l.hasFill) fills.push_back({node.l, node.t, node.r - node.l, node.b - node.t, l.fill});
      if (l.hasDraw) {
        const Seg top{{node.l, node.t}, {node.r, node.t}}, bottom{{node.l, node.b}, {node.r, node.b}};
        const Seg left{{node.l, node.t}, {node.l, node.b}}, right{{node.r, node.t}, {node.r, node.b}};
        for (Seg s : {top, bottom, left, right}) {
          s.tint = l.hasDrawInk || a.hasColor;
          s.col = l.hasDrawInk ? l.drawInk : a.ink;
          s.lw = lwA;
          extraSegs.push_back(s);
        }
      }
      if (!l.name.empty()) labelNodes[l.name] = node;
      Label placed{b, {centre.x - b->_width / 2.f, centre.y + (b->_height - b->_depth) / 2.f}, angle};
      placedLabels.push_back(placed);
      grow({node.l, node.t}, 0.f);
      grow({node.r, node.b}, 0.f);
    }
    if (a.phantom) continue;
    paths[i] = path;

    // How much of the path the ends take, in line widths, as pgf's arrow
    // tips do: the apex of a head is half a line width before the path's
    // end, and a plain shaft stops half a line width before the apex.
    using A = DiagramArrow;
    float endTrim = 0.f, startTrim = 0.f;
    switch (a.head) {
      case A::Head::none: break;
      case A::Head::implies: endTrim = a.doubled ? 5.5f : 1.f; break;
      case A::Head::bar: endTrim = 0.75f; break;
      case A::Head::hook:
      case A::Head::hookBack: endTrim = 3.6f; break;
      case A::Head::tail: endTrim = 5.2006f; break;
      default: endTrim = 1.f; break;
    }
    switch (a.tail) {
      case A::Tail::none: break;
      case A::Tail::hook:
      case A::Tail::hookBack: startTrim = 3.6f; break;
      case A::Tail::tail: startTrim = 5.2006f; break;
      case A::Tail::bar: startTrim = 0.75f; break;
      case A::Tail::head:
      case A::Tail::two:
      case A::Tail::harpoonLeft:
      case A::Tail::harpoonRight: startTrim = 1.f; break;
      case A::Tail::implies: startTrim = 5.5f; break;
    }
    // amscd's arrow glyphs end with the tip at the edge of their box.
    // TikZ's `double` draws the arrow as one thick line and then one thinner on top, and pgf measures its tips in
    // the thick one's width.
    const float tl = a.doubled && a.doubleInner.isValid() ? 2.f * lwA + size(a.doubleInner) : lwA;
    const float tipBack = texRows ? 0.f : 0.5f * tl;
    if (texRows && endTrim >= 1.f) endTrim -= 0.5f;
    // arrows.meta's tips are polygons of pgf's own sizes, and trim the shaft by what they say
    MetaShape metaHead, metaTail;
    const auto metaOf = [&](const MetaTip& t) {
      const auto opt = [&](const Dimen& d) { return d.isValid() ? std::optional<float>(size(d)) : std::nullopt; };
      return metaShape(t, opt(t.length), opt(t.width), opt(t.inset), opt(t.lineWidth), lwA, pt);
    };
    if (a.headTip.kind != MetaTip::Kind::none) metaHead = metaOf(a.headTip);
    if (a.tailTip.kind != MetaTip::Kind::none) metaTail = metaOf(a.tailTip);
    float startAbs = startTrim * tl, endAbs = endTrim * tl;
    if (!metaHead.pts.empty()) endAbs = metaHead.tipEnd - metaHead.lineEnd;
    if (!metaTail.pts.empty()) startAbs = metaTail.tipEnd - metaTail.lineEnd;
    std::vector<Pt> shaft = trimmed(path, startAbs, endAbs);
    if (a.squiggly) shaft = zigzag(shaft, 1.9f * lwA, 4.625f * lwA, 6.f * lwA);
    std::vector<std::vector<Pt>> lines;
    if (a.doubled) {
      // Two lines a line width thick, 0.0969em between their centres (TikZ's `double`: the line width and
      // its own `double distance`).
      const float half = a.doubleInner.isValid() ? (lwA + size(a.doubleInner)) / 2.f : texRows ? 0.12f * em : 0.0969f * em;
      lines.push_back(offset(shaft, half));
      lines.push_back(offset(shaft, -half));
    } else {
      lines.push_back(shaft);
    }
    std::vector<Seg> pieces;
    for (const auto& line : lines) {
      if (a.dash == A::Dash::custom) {
        std::vector<float> pattern;
        for (const Dimen& v : a.pattern) pattern.push_back(v.unit == UnitType::none ? v.val * lwA : size(v));
        patterned(line, pattern, pieces);
      } else {
        const float on = a.dash == A::Dash::dashed ? 7.f * lwA : a.dash == A::Dash::dotted ? lwA : 0.f;
        const float off = a.dash == A::Dash::dashed ? 4.f * lwA : 5.f * lwA;
        dashed(line, on, off, pieces);
      }
    }
    for (const Seg& piece : pieces) clipOutside(piece, holes, segs);

    Pt endDir, startDir;
    pointAt(path, total, &endDir);
    pointAt(path, 0.f, &startDir);
    const Pt endApex = pointAt(path, std::max(0.f, total - tipBack), nullptr);
    // Heads at the end.
    switch (a.head) {
      case A::Head::none: break;
      case A::Head::one: tip(endApex, endDir, tl, false, 0, segs); break;
      case A::Head::two:
        tip(endApex, endDir, tl, false, 0, segs);
        tip(endApex - endDir * (3.6f * tl), endDir, tl, false, 0, segs);
        break;
      case A::Head::implies: tip(endApex, endDir, tl, true, 0, segs); break;
      case A::Head::harpoonLeft:
        tip(endApex, endDir, tl, false, 1, segs);
        segs.push_back({endApex, endApex - endDir * tl});
        break;
      case A::Head::harpoonRight:
        tip(endApex, endDir, tl, false, -1, segs);
        segs.push_back({endApex, endApex - endDir * tl});
        break;
      case A::Head::bar: {
        const Pt at = pointAt(path, std::max(0.f, total - 0.5f * tl), nullptr);
        segs.push_back({at + leftOf(endDir) * (4.1f * tl), at - leftOf(endDir) * (4.1f * tl)});
        break;
      }
      case A::Head::hook:
      case A::Head::hookBack:
      case A::Head::tail: {
        // What the start of an arrow has, at the end: the same drawn from the other end of the path.
        std::vector<Pt> back(path.rbegin(), path.rend());
        Pt backDir;
        pointAt(back, 0.f, &backDir);
        if (a.head == A::Head::tail) {
          tip(pointAt(back, 5.7006f * tl, nullptr), backDir, tl, false, 0, segs);
        } else {
          hook(pointAt(back, 3.6f * tl, nullptr), backDir, tl, a.head == A::Head::hook ? -1.f : 1.f, segs);
        }
        break;
      }
    }
    // Tails at the start: the rear of a tail's head, and the end of a hook
    // or a bar, are half a line width in from the path's start.
    switch (a.tail) {
      case A::Tail::none: break;
      case A::Tail::hook:
      case A::Tail::hookBack: {
        const Pt at = pointAt(path, 3.6f * tl, nullptr);
        hook(at, startDir, tl, a.tail == A::Tail::hook ? -1.f : 1.f, segs);
        break;
      }
      case A::Tail::tail:
        tip(pointAt(path, 5.7006f * tl, nullptr), startDir, tl, false, 0, segs);
        break;
      case A::Tail::bar: {
        const Pt at = pointAt(path, 0.5f * tl, nullptr);
        segs.push_back({at + leftOf(startDir) * (4.1f * tl), at - leftOf(startDir) * (4.1f * tl)});
        break;
      }
      case A::Tail::head:
        tip(pointAt(path, 0.5f * tl, nullptr), startDir * -1.f, tl, false, 0, segs);
        break;
      case A::Tail::two: {
        const Pt apex = pointAt(path, 0.5f * tl, nullptr);
        tip(apex, startDir * -1.f, tl, false, 0, segs);
        tip(apex + startDir * (3.6f * tl), startDir * -1.f, tl, false, 0, segs);
        break;
      }
      case A::Tail::harpoonLeft:
      case A::Tail::harpoonRight: {
        const Pt apex = pointAt(path, 0.5f * tl, nullptr);
        tip(apex, startDir * -1.f, tl, false, a.tail == A::Tail::harpoonLeft ? 1 : -1, segs);
        segs.push_back({apex, apex + startDir * tl});
        break;
      }
      case A::Tail::implies:
        tip(pointAt(path, 0.5f * tl, nullptr), startDir * -1.f, tl, true, 0, segs);
        break;
    }
    const auto placeMeta = [&](const MetaShape& m, Pt end, Pt dir) {
      if (m.pts.empty()) return;
      const Pt origin = end - dir * m.tipEnd;
      Poly poly;
      for (const Pt& p : m.pts) poly.pts.push_back(origin + dir * p.x + Pt{-dir.y, dir.x} * (-p.y));
      poly.fill = m.fill;
      poly.stroke = m.stroke;
      poly.round = m.round;
      poly.lw = m.lw;
      poly.tint = a.hasColor;
      poly.col = a.ink;
      polys.push_back(poly);
    };
    placeMeta(metaHead, pointAt(path, total, nullptr), endDir);
    placeMeta(metaTail, pointAt(path, 0.f, nullptr), startDir * -1.f);
    for (Seg& sg : segs) sg.lw = lwA;
  }
  // An arrow that crosses over cuts a gap in those drawn before it; each
  // arrow's lines then take its colour.
  const float clearance = crossingClearance.isValid() ? size(crossingClearance) : 1.5f * ex;
  for (std::size_t k = 0; k < arrows.size(); k++) {
    if (!arrows[k].crossing || arrows[k].phantom || paths[k].empty()) continue;
    for (std::size_t j = 0; j < k; j++) perArrow[j] = cutBand(perArrow[j], paths[k], clearance / 2.f);
  }
  std::vector<Seg> segs;
  for (std::size_t k = 0; k < arrows.size(); k++) {
    for (Seg sg : perArrow[k]) {
      sg.tint = arrows[k].hasColor;
      sg.col = arrows[k].ink;
      segs.push_back(sg);
    }
  }
  for (const Seg& sg : extraSegs) segs.push_back(sg);
  // The nodes' own outlines and fills, under what is on top of them.
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      const DiagramCell* o = cellOpt(r, c);
      if (o == nullptr || content[r][c] == nullptr) continue;
      const Frame f = cellRect(r, c);
      if (o->hasFill) fills.insert(fills.begin(), {f.l, f.t, f.r - f.l, f.b - f.t, o->fill, f.round});
      if (o->hasDraw && f.round) {
        // The ellipse, as 64 short lines.
        const float cx = (f.l + f.r) / 2.f, cy = (f.t + f.b) / 2.f, rx = (f.r - f.l) / 2.f, ry = (f.b - f.t) / 2.f;
        for (int k = 0; k < 64; k++) {
          const float t0 = 2.f * kPi * static_cast<float>(k) / 64.f, t1 = 2.f * kPi * static_cast<float>(k + 1) / 64.f;
          Seg s{{cx + rx * std::cos(t0), cy + ry * std::sin(t0)}, {cx + rx * std::cos(t1), cy + ry * std::sin(t1)}};
          s.tint = o->hasDrawInk;
          s.col = o->drawInk;
          s.lw = 0.4f * pt;
          segs.push_back(s);
        }
      } else if (o->hasDraw) {
        const Seg top{{f.l, f.t}, {f.r, f.t}}, bottom{{f.l, f.b}, {f.r, f.b}};
        const Seg left{{f.l, f.t}, {f.l, f.b}}, right{{f.r, f.t}, {f.r, f.b}};
        for (Seg s : {top, bottom, left, right}) {
          s.tint = o->hasDrawInk;
          s.col = o->drawInk;
          s.lw = 0.4f * pt;
          segs.push_back(s);
        }
      }
    }
  }
  for (const Seg& sg : segs) {
    grow(sg.a, (sg.lw > 0.f ? sg.lw : thick) / 2.f);
    grow(sg.b, (sg.lw > 0.f ? sg.lw : thick) / 2.f);
  }
  for (const Poly& p : polys) {
    for (const Pt& q : p.pts) grow(q, p.lw / 2.f);
  }

  // The baseline: that of the cells' row when there is one, else the middle
  // of the diagram, as tikz-cd's own picture sits.
  const float baseline = rows == 1 ? base[0] : (base[rows - 1] + rowDepth[rows - 1]) / 2.f;
  const float shiftX = -minX;
  result->_width = maxX - minX;
  result->_height = baseline - minY;
  result->_depth = std::max(0.f, maxY - baseline);
  result->thickness = thick;
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      if (content[r][c] == nullptr) continue;
      DiagramBox::Placed p{content[r][c], colX[c] - content[r][c]->_width / 2.f + shiftX, base[r] - baseline};
      const DiagramCell* o = cellOpt(r, c);
      if (o != nullptr && o->rotate != 0.f) {
        p.angle = o->rotate;
        p.px = content[r][c]->_width / 2.f;
        p.py = (content[r][c]->_depth - content[r][c]->_height) / 2.f;
      }
      result->children.push_back(p);
    }
  }
  for (const Label& l : placedLabels) {
    DiagramBox::Placed p{l.box, l.at.x + shiftX, l.at.y - baseline};
    if (l.angle != 0.f) {
      p.angle = l.angle;
      p.px = l.box->_width / 2.f;
      p.py = (l.box->_depth - l.box->_height) / 2.f;
    }
    result->children.push_back(p);
  }
  for (const Fill& f : fills) result->fills.push_back({f.x + shiftX, f.y - baseline, f.w, f.h, f.col, f.round});
  for (const Seg& sg : segs) {
    Seg moved = sg;
    moved.a = {sg.a.x + shiftX, sg.a.y - baseline};
    moved.b = {sg.b.x + shiftX, sg.b.y - baseline};
    result->segments.push_back(moved);
  }
  for (const Poly& p : polys) {
    Poly moved = p;
    for (Pt& q : moved.pts) q = {q.x + shiftX, q.y - baseline};
    result->polys.push_back(moved);
  }
  return result;
}

sptr<Box> StretchArrowAtom::createBox(Env& env) {
  const float em = Units::fsize(Units::getDimen("1em"), env);
  const Dimen none = Units::getDimen("0em");
  DiagramAtom d;
  d.cells = {{nullptr, nullptr}};
  d.rowSep = d.innerX = d.innerY = none;
  d.colSep = {std::max(_length(env), 0.f) / em, UnitType::em};
  const auto arrow = [&](bool right, float shift) {
    DiagramArrow a;
    a.row = a.toRow = 0;
    a.col = right ? 0 : 1;
    a.toCol = right ? 1 : 0;
    a.head = DiagramArrow::Head::one;
    a.shift = shift;
    return a;
  };
  switch (_kind) {
    case Kind::twoHeadRight:
    case Kind::twoHeadLeft: {
      DiagramArrow a = arrow(_kind == Kind::twoHeadRight, 0.f);
      a.head = DiagramArrow::Head::two;
      d.arrows.push_back(a);
      break;
    }
    case Kind::longEqual: {
      DiagramArrow a = arrow(true, 0.f);
      a.doubled = true;
      a.head = DiagramArrow::Head::none;
      d.arrows.push_back(a);
      break;
    }
    case Kind::toFrom:
      d.arrows.push_back(arrow(true, 1.f));
      d.arrows.push_back(arrow(false, 1.f));
      break;
    case Kind::doubleLeft:
    case Kind::doubleBoth: {
      // mathtools' \xLeftarrow and \xLeftrightarrow: the font has no stretching glyph for them.
      DiagramArrow a = arrow(_kind == Kind::doubleBoth, 0.f);
      a.doubled = true;
      a.head = DiagramArrow::Head::implies;
      if (_kind == Kind::doubleBoth) a.tail = DiagramArrow::Tail::implies;
      d.arrows.push_back(a);
      break;
    }
  }
  // As tall as an arrow's head, so the labels stand as far from a line.
  const sptr<Box> box = d.createBox(env);
  const float axis = env.mathConsts().axisHeight() * env.scale();
  const float lw = env.mathConsts().fractionRuleThickness() * env.scale();
  box->_height = std::max(box->_height, axis + 6.f * lw);
  box->_depth = std::max(box->_depth, 6.f * lw - axis);
  return box;
}

sptr<Box> EncloseAtom::createBox(Env& env) {
  const float em = Units::fsize(Units::getDimen("1em"), env);
  const float lw = env.mathConsts().fractionRuleThickness() * env.scale();
  // A command with no argument at the end of the input has none.
  const sptr<Box> b = _base == nullptr ? sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f) : _base->createBox(env);
  const float w = b->_width, h = b->_height, d = b->_depth;
  auto box = sptrOf<DiagramBox>();
  std::vector<Seg>& s = box->segments;
  // The screen's y, down, from the baseline.
  float top = -h, bottom = d, dx = 0.f, width = w;
  switch (_kind) {
    case Kind::overSegment: {
      const float y = -(h + 0.28f * em);
      s = {{{0.f, y}, {w, y}}, {{0.f, y}, {0.f, y + 0.25f * em}}, {{w, y}, {w, y + 0.25f * em}}};
      top = y - lw / 2.f;
      break;
    }
    case Kind::underSegment: {
      const float y = d + 0.28f * em;
      s = {{{0.f, y}, {w, y}}, {{0.f, y}, {0.f, y - 0.25f * em}}, {{w, y}, {w, y - 0.25f * em}}};
      bottom = y + lw / 2.f;
      break;
    }
    case Kind::angle: {
      const float y = -(h + 0.2f * em), x = w + 0.1f * em;
      s = {{{0.f, y}, {x, y}}, {{x, y}, {x, d}}};
      top = y - lw / 2.f;
      width = x + lw / 2.f;
      break;
    }
    case Kind::phase: {
      const float u = d + 0.2f * em, up = -(h + 0.1f * em), slant = 0.5f * (u - up);
      dx = slant + 0.1f * em;
      width = dx + w + 0.1f * em;
      s = {{{0.f, u}, {slant, up}}, {{0.f, u}, {width, u}}};
      top = up - lw / 2.f;
      bottom = u + lw / 2.f;
      break;
    }
    case Kind::harpoonLeft:
    case Kind::harpoonRight:
    case Kind::doubleArrow: {
      // An arrow over the content, as long as it is: a harpoon has one barb,
      // up, and \Overrightarrow two lines and the double head.
      const float y = -(h + 0.22f * em);
      const bool left = _kind == Kind::harpoonLeft;
      const Pt u{left ? -1.f : 1.f, 0.f}, from{left ? w : 0.f, y}, apex{left ? 0.f : w, y};
      if (_kind == Kind::doubleArrow) {
        const float half = 0.0969f * em;
        const Pt end = apex - u * (5.5f * lw);
        s.push_back({{from.x, y - half}, {end.x, y - half}});
        s.push_back({{from.x, y + half}, {end.x, y + half}});
        tip(apex, u, lw, true, 0, s);
      } else {
        s.push_back({from, apex - u * lw});
        // Up is to the right of the way a leftward harpoon goes.
        tip(apex, u, lw, false, left ? -1 : 1, s);
        s.push_back({apex, apex - u * lw});
      }
      top = y - 6.5f * lw;
      break;
    }
    case Kind::circle: {
      const float r = std::max(w, h + d) / 2.f + 0.15f * em, cy = (d - h) / 2.f;
      dx = r - w / 2.f;
      width = 2.f * r;
      Pt prev{r + r, cy};
      for (int k = 1; k <= 40; k++) {
        const float t = 2.f * kPi * static_cast<float>(k) / 40.f;
        const Pt p{r + r * std::cos(t), cy + r * std::sin(t)};
        s.push_back({prev, p});
        prev = p;
      }
      top = cy - r - lw / 2.f;
      bottom = cy + r + lw / 2.f;
      break;
    }
  }
  box->children.push_back({b, dx, 0.f});
  box->thickness = lw;
  box->_width = width;
  box->_height = std::max(h, -top);
  box->_depth = std::max(d, bottom);
  return box;
}

}  // namespace microtex
