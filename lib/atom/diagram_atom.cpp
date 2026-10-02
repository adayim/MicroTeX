#include "atom/diagram_atom.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
};

struct Frame {
  float l = 0.f, t = 0.f, r = 0.f, b = 0.f;
};

/** How far a ray from `p`, inside `r`, goes along the unit vector `d`
 *  before it leaves. */
float exitDistance(Pt p, Pt d, const Frame& r) {
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

/** The path with `from` taken off its start and `to` off its end. */
std::vector<Pt> trimmed(const std::vector<Pt>& p, float from, float to) {
  const float total = pathLength(p);
  if (from + to >= total) return {};
  std::vector<Pt> out;
  out.push_back(pointAt(p, from, nullptr));
  float walked = 0.f;
  for (std::size_t i = 1; i < p.size(); i++) {
    walked += length(p[i] - p[i - 1]);
    if (walked > from + 1e-4f && walked < total - to - 1e-4f) out.push_back(p[i]);
  }
  out.push_back(pointAt(p, total - to, nullptr));
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

/** The box `f` and `by` more on every side. */
Frame grown(const Frame& f, float by) { return {f.l - by, f.t - by, f.r + by, f.b + by}; }

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
  };
  std::vector<Placed> children;
  std::vector<Seg> segments;
  float thickness = 0.f;

  void draw(Graphics2D& g2, float x, float y) override {
    for (const Placed& c : children) c.box->draw(g2, x + c.x, y + c.y);
    const Stroke old = g2.getStroke();
    const color oldColor = g2.getColor();
    g2.setStroke(Stroke(thickness, CAP_ROUND, JOIN_ROUND));
    for (const Seg& s : segments) {
      if (s.tint) g2.setColor(s.col);
      g2.drawLine(x + s.a.x, y + s.a.y, x + s.b.x, y + s.b.y);
      if (s.tint) g2.setColor(oldColor);
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

  const int rows = static_cast<int>(cells.size());
  int cols = 0;
  for (const auto& row : cells) cols = std::max(cols, static_cast<int>(row.size()));
  auto result = sptrOf<DiagramBox>();
  if (rows == 0 || cols == 0) return result;

  // The cells, and their boxes: the content and the space around it.
  std::vector<std::vector<sptr<Box>>> content(rows, std::vector<sptr<Box>>(cols));
  std::vector<std::vector<float>> w(rows, std::vector<float>(cols, 2.f * padX));
  std::vector<std::vector<float>> h(rows, std::vector<float>(cols, padY));
  std::vector<std::vector<float>> d(rows, std::vector<float>(cols, padY));
  std::vector<float> colWidth(cols, 0.f), rowHeight(rows, 0.f), rowDepth(rows, 0.f);
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < static_cast<int>(cells[r].size()); c++) {
      if (cells[r][c] == nullptr) continue;
      const sptr<Box> b = cells[r][c]->createBox(env);
      content[r][c] = b;
      w[r][c] += b->_width;
      h[r][c] += b->_height;
      d[r][c] += b->_depth;
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

  // The labels, small as a script is, and the room they and the arrows'
  // least lengths ask of the columns.
  std::vector<std::vector<sptr<Box>>> labelBox(arrows.size());
  std::vector<float> gap(std::max(cols - 1, 0), size(colSep));
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
      sptr<Box> b = env.withStyle(TexStyle::script, [&](Env& e) {
        return l.body == nullptr ? sptr<Box>() : l.body->createBox(e);
      });
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
      base[r] = base[r - 1] + std::max(size(minRowPitch), rowDepth[r - 1] + size(rowSep) + rowHeight[r]);
    }
  }
  const auto cellRect = [&](int r, int c) {
    return Frame{colX[c] - w[r][c] / 2.f, base[r] - h[r][c], colX[c] + w[r][c] / 2.f, base[r] + d[r][c]};
  };
  const auto anchor = [&](int r, int c) { return Pt{colX[c], base[r] - axis}; };

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
  };
  std::vector<Label> placedLabels;
  std::vector<std::vector<Seg>> perArrow(arrows.size());
  std::vector<std::vector<Pt>> paths(arrows.size());

  for (std::size_t i = 0; i < arrows.size(); i++) {
    const DiagramArrow& a = arrows[i];
    if (!inside(a.row, a.col) || !inside(a.toRow, a.toCol) || (a.row == a.toRow && a.col == a.toCol)) {
      continue;
    }
    std::vector<Seg>& segs = perArrow[i];
    const Pt from = anchor(a.row, a.col), to = anchor(a.toRow, a.toCol);
    const Frame fromBox = cellRect(a.row, a.col), toBox = cellRect(a.toRow, a.toCol);
    const float dist = length(to - from);
    const Pt u = unit(to - from);

    // The path, as TeX draws it: from the cells' outer borders (the box and
    // half a line width of node `outer sep`), straight, or a curve leaving
    // each at an angle to the straight one, its controls 0.3915 of the
    // distance between the two ends out along those angles.
    const float outer = texRows ? 0.f : lw / 2.f;
    const Frame outerFrom = grown(fromBox, outer), outerTo = grown(toBox, outer);
    Pt s, e, c1, c2;
    const bool curved = a.bend != 0.f;
    if (curved) {
      const Pt rough1 = from + turn(u, a.bend) * (0.3915f * dist);
      const Pt rough2 = to + turn(u * -1.f, -a.bend) * (0.3915f * dist);
      s = from + unit(rough1 - from) * exitDistance(from, unit(rough1 - from), outerFrom);
      e = to + unit(rough2 - to) * exitDistance(to, unit(rough2 - to), outerTo);
      const float k = 0.3915f * length(e - s);
      c1 = s + unit(rough1 - from) * k;
      c2 = e + unit(rough2 - to) * k;
    } else {
      s = from + u * exitDistance(from, u, outerFrom);
      e = to - u * exitDistance(to, u * -1.f, outerTo);
      if (a.fixedLength.isValid() && length(e - s) > size(a.fixedLength)) {
        Pt mid = (s + e) * 0.5f;
        // In TeX's rows the arrow is centred on its own row's axis.
        if (texRows && a.col == a.toCol) mid.y = arrowRowBase[std::min(a.row, a.toRow)] - axis;
        s = mid - u * (size(a.fixedLength) / 2.f);
        e = mid + u * (size(a.fixedLength) / 2.f);
      }
    }
    if (a.shift != 0.f) {
      const Pt by = leftOf(u) * (a.shift * 0.56f * ex);
      s = s + by;
      e = e + by;
      c1 = c1 + by;
      c2 = c2 + by;
    }
    std::vector<Pt> path;
    if (curved) {
      for (int k = 0; k <= 48; k++) {
        const float t = static_cast<float>(k) / 48.f, v = 1.f - t;
        path.push_back(s * (v * v * v) + c1 * (3.f * v * v * t) + c2 * (3.f * v * t * t) + e * (t * t * t));
      }
    } else {
      path = {s, e};
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
      const float hw = b->_width / 2.f + ex / 2.f, hh = (b->_height + b->_depth) / 2.f + ex / 2.f;
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
        placedLabels.push_back({b, {x, y}});
        grow({x, y - b->_height}, 0.f);
        grow({x + b->_width, y + b->_depth}, 0.f);
        continue;
      }
      // A phantom arrow's labels are on its path, as tikz-cd's phantom style has it.
      const DiagramLabel::Side side = a.phantom ? DiagramLabel::Side::center : l.side;
      if (side != DiagramLabel::Side::center) {
        const Pt n = leftOf(dir) * (side == DiagramLabel::Side::left ? 1.f : -1.f);
        const float tx = std::fabs(n.x) > 1e-6f ? hw / std::fabs(n.x) : kInf;
        const float ty = std::fabs(n.y) > 1e-6f ? hh / std::fabs(n.y) : kInf;
        centre = at + n * std::min(tx, ty);
      } else if (!a.phantom) {
        holes.push_back({centre.x - hw, centre.y - hh, centre.x + hw, centre.y + hh});
      }
      placedLabels.push_back({b, {centre.x - b->_width / 2.f, centre.y + (b->_height - b->_depth) / 2.f}});
      grow({centre.x - hw, centre.y - hh}, 0.f);
      grow({centre.x + hw, centre.y + hh}, 0.f);
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
      default: endTrim = 1.f; break;
    }
    switch (a.tail) {
      case A::Tail::none: break;
      case A::Tail::hook:
      case A::Tail::hookBack: startTrim = 3.6f; break;
      case A::Tail::tail: startTrim = 5.2006f; break;
      case A::Tail::bar: startTrim = 0.75f; break;
      case A::Tail::head: startTrim = 1.f; break;
      case A::Tail::implies: startTrim = 5.5f; break;
    }
    // amscd's arrow glyphs end with the tip at the edge of their box.
    const float tipBack = texRows ? 0.f : 0.5f * lw;
    if (texRows && endTrim >= 1.f) endTrim -= 0.5f;
    std::vector<Pt> shaft = trimmed(path, startTrim * lw, endTrim * lw);
    if (a.squiggly) shaft = zigzag(shaft, 1.9f * lw, 4.625f * lw, 6.f * lw);
    std::vector<std::vector<Pt>> lines;
    if (a.doubled) {
      // Two lines a line width thick, 0.0969em between their centres.
      const float half = texRows ? 0.12f * em : 0.0969f * em;
      lines.push_back(offset(shaft, half));
      lines.push_back(offset(shaft, -half));
    } else {
      lines.push_back(shaft);
    }
    const float on = a.dash == A::Dash::dashed ? 7.f * lw : a.dash == A::Dash::dotted ? lw : 0.f;
    const float off = a.dash == A::Dash::dashed ? 4.f * lw : 5.f * lw;
    std::vector<Seg> pieces;
    for (const auto& line : lines) dashed(line, on, off, pieces);
    for (const Seg& piece : pieces) clipOutside(piece, holes, segs);

    Pt endDir, startDir;
    pointAt(path, total, &endDir);
    pointAt(path, 0.f, &startDir);
    const Pt endApex = pointAt(path, std::max(0.f, total - tipBack), nullptr);
    // Heads at the end.
    switch (a.head) {
      case A::Head::none: break;
      case A::Head::one: tip(endApex, endDir, lw, false, 0, segs); break;
      case A::Head::two:
        tip(endApex, endDir, lw, false, 0, segs);
        tip(endApex - endDir * (3.6f * lw), endDir, lw, false, 0, segs);
        break;
      case A::Head::implies: tip(endApex, endDir, lw, true, 0, segs); break;
      case A::Head::harpoonLeft:
        tip(endApex, endDir, lw, false, 1, segs);
        segs.push_back({endApex, endApex - endDir * lw});
        break;
      case A::Head::harpoonRight:
        tip(endApex, endDir, lw, false, -1, segs);
        segs.push_back({endApex, endApex - endDir * lw});
        break;
      case A::Head::bar: {
        const Pt at = pointAt(path, std::max(0.f, total - 0.5f * lw), nullptr);
        segs.push_back({at + leftOf(endDir) * (4.1f * lw), at - leftOf(endDir) * (4.1f * lw)});
        break;
      }
    }
    // Tails at the start: the rear of a tail's head, and the end of a hook
    // or a bar, are half a line width in from the path's start.
    switch (a.tail) {
      case A::Tail::none: break;
      case A::Tail::hook:
      case A::Tail::hookBack: {
        const Pt at = pointAt(path, 3.6f * lw, nullptr);
        hook(at, startDir, lw, a.tail == A::Tail::hook ? -1.f : 1.f, segs);
        break;
      }
      case A::Tail::tail:
        tip(pointAt(path, 5.7006f * lw, nullptr), startDir, lw, false, 0, segs);
        break;
      case A::Tail::bar: {
        const Pt at = pointAt(path, 0.5f * lw, nullptr);
        segs.push_back({at + leftOf(startDir) * (4.1f * lw), at - leftOf(startDir) * (4.1f * lw)});
        break;
      }
      case A::Tail::head:
        tip(pointAt(path, 0.5f * lw, nullptr), startDir * -1.f, lw, false, 0, segs);
        break;
      case A::Tail::implies:
        tip(pointAt(path, 0.5f * lw, nullptr), startDir * -1.f, lw, true, 0, segs);
        break;
    }
  }
  // An arrow that crosses over cuts a gap in those drawn before it; each
  // arrow's lines then take its colour.
  for (std::size_t k = 0; k < arrows.size(); k++) {
    if (!arrows[k].crossing || arrows[k].phantom || paths[k].empty()) continue;
    for (std::size_t j = 0; j < k; j++) perArrow[j] = cutBand(perArrow[j], paths[k], 0.75f * ex);
  }
  std::vector<Seg> segs;
  for (std::size_t k = 0; k < arrows.size(); k++) {
    for (Seg sg : perArrow[k]) {
      sg.tint = arrows[k].hasColor;
      sg.col = arrows[k].ink;
      segs.push_back(sg);
    }
  }
  for (const Seg& sg : segs) {
    grow(sg.a, thick / 2.f);
    grow(sg.b, thick / 2.f);
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
      result->children.push_back({content[r][c], colX[c] - content[r][c]->_width / 2.f + shiftX, base[r] - baseline});
    }
  }
  for (const Label& l : placedLabels) result->children.push_back({l.box, l.at.x + shiftX, l.at.y - baseline});
  for (const Seg& sg : segs) {
    Seg moved = sg;
    moved.a = {sg.a.x + shiftX, sg.a.y - baseline};
    moved.b = {sg.b.x + shiftX, sg.b.y - baseline};
    result->segments.push_back(moved);
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
