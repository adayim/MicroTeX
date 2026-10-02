#include "atom/diagram_atom.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

/** One side of an arrow's head: a curve out of the tip, back and out, its
 *  tangent along the shaft at the tip, as the Computer Modern arrow's. */
void barb(Pt tip, Pt u, float back, float wide, float shape, float side, std::vector<Seg>& out) {
  const Pt n = leftOf(u);
  Pt prev = tip;
  for (int k = 1; k <= 8; k++) {
    const float s = static_cast<float>(k) / 8.f;
    const Pt p = tip - u * (back * s) + n * (side * wide * std::pow(s, shape));
    out.push_back({prev, p});
    prev = p;
  }
}

/** The path as a zigzag, straight for `ends` at each end: tikz's `squiggly`. */
std::vector<Pt> zigzag(const std::vector<Pt>& p, float amp, float seg, float ends) {
  const float total = pathLength(p);
  if (p.size() < 2 || total < 2.f * ends + seg) return p;
  const int n = std::max(2, static_cast<int>(std::lround((total - 2.f * ends) / (seg / 2.f))));
  const float step = (total - 2.f * ends) / static_cast<float>(n);
  std::vector<Pt> out{p.front(), pointAt(p, ends, nullptr)};
  for (int k = 1; k < n; k++) {
    Pt dir;
    const Pt q = pointAt(p, ends + static_cast<float>(k) * step, &dir);
    out.push_back(q + leftOf(dir) * ((k % 2 == 1) ? amp : -amp));
  }
  out.push_back(pointAt(p, total - ends, nullptr));
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
  const float thick = env.mathConsts().fractionRuleThickness() * env.scale();
  // tikz-cd sizes heads, hooks and dashes in the Computer Modern rule, 0.4pt
  // at 10pt, whatever the math font's own is.
  const float cm = 0.04f * Units::fsize(Units::getDimen("1em"), env);
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

  // The labels, small as a script is, and the room they and the arrows'
  // least lengths ask of the columns.
  std::vector<std::vector<sptr<Box>>> labelBox(arrows.size());
  std::vector<float> gap(std::max(cols - 1, 0), size(colSep));
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
    base[r] = base[r - 1] + std::max(size(minRowPitch), rowDepth[r - 1] + size(rowSep) + rowHeight[r]);
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

    // The path: straight between the cells' borders, or a curve leaving
    // each at an angle to the straight one.
    Pt s, e, c1, c2;
    const bool curved = a.bend != 0.f;
    if (curved) {
      const float k = 0.3915f * dist;
      c1 = from + turn(u, a.bend) * k;
      c2 = to + turn(u * -1.f, -a.bend) * k;
      s = from + unit(c1 - from) * exitDistance(from, unit(c1 - from), fromBox);
      e = to + unit(c2 - to) * exitDistance(to, unit(c2 - to), toBox);
    } else {
      s = from + u * exitDistance(from, u, fromBox);
      e = to - u * exitDistance(to, u * -1.f, toBox);
      if (a.fixedLength.isValid() && length(e - s) > size(a.fixedLength)) {
        const Pt mid = (s + e) * 0.5f;
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
      for (int k = 0; k <= 32; k++) {
        const float t = static_cast<float>(k) / 32.f, v = 1.f - t;
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

    // The shaft, less what a head or a tail covers, and the ends.
    const float headBack = (a.head == DiagramArrow::Head::implies ? 8.5f : 6.2f) * cm;
    float cutStart = 0.f, cutEnd = 0.f;
    if (a.tail == DiagramArrow::Tail::tail) cutStart = 6.2f * cm;
    if (a.doubled && a.head != DiagramArrow::Head::none) cutEnd = 0.7f * headBack;
    if (a.doubled && a.tail == DiagramArrow::Tail::implies) cutStart = 0.7f * 8.5f * cm;
    paths[i] = path;
    std::vector<Pt> shaft = trimmed(path, cutStart, cutEnd);
    if (a.squiggly) shaft = zigzag(shaft, 1.9f * cm, 9.25f * cm, 6.f * cm);
    std::vector<std::vector<Pt>> lines;
    if (a.doubled) {
      lines.push_back(offset(shaft, 2.5f * cm));
      lines.push_back(offset(shaft, -2.5f * cm));
    } else {
      lines.push_back(shaft);
    }
    const float on = a.dash == DiagramArrow::Dash::dashed ? 7.f * cm : a.dash == DiagramArrow::Dash::dotted ? cm : 0.f;
    const float off = a.dash == DiagramArrow::Dash::dashed ? 4.f * cm : 5.f * cm;
    std::vector<Seg> pieces;
    for (const auto& line : lines) dashed(line, on, off, pieces);
    for (const Seg& piece : pieces) clipOutside(piece, holes, segs);

    Pt endDir, startDir;
    pointAt(path, total, &endDir);
    pointAt(path, 0.f, &startDir);
    // Heads at the end, tails at the start.
    switch (a.head) {
      case DiagramArrow::Head::none: break;
      case DiagramArrow::Head::one:
      case DiagramArrow::Head::two:
        for (const float side : {1.f, -1.f}) {
          barb(path.back(), endDir, 6.2f * cm, 6.2f * cm, 2.5f, side, segs);
          if (a.head == DiagramArrow::Head::two) {
            barb(path.back() - endDir * (3.6f * cm), endDir, 6.2f * cm, 6.2f * cm, 2.5f, side, segs);
          }
        }
        break;
      case DiagramArrow::Head::implies:
        for (const float side : {1.f, -1.f}) barb(path.back(), endDir, 8.5f * cm, 6.4f * cm, 2.2f, side, segs);
        break;
      case DiagramArrow::Head::harpoonLeft:
      case DiagramArrow::Head::harpoonRight:
        barb(path.back(), endDir, 6.2f * cm, 6.2f * cm, 2.5f, a.head == DiagramArrow::Head::harpoonLeft ? 1.f : -1.f, segs);
        break;
      case DiagramArrow::Head::bar:
        segs.push_back({path.back() + leftOf(endDir) * (4.1f * cm), path.back() - leftOf(endDir) * (4.1f * cm)});
        break;
    }
    switch (a.tail) {
      case DiagramArrow::Tail::none: break;
      case DiagramArrow::Tail::hook:
      case DiagramArrow::Tail::hookBack: {
        const Pt n = leftOf(startDir) * (a.tail == DiagramArrow::Tail::hook ? 1.f : -1.f);
        Pt prev = path.front();
        for (int k = 1; k <= 10; k++) {
          const float phi = kPi * static_cast<float>(k) / 10.f;
          const Pt p = path.front() - startDir * (3.f * cm * std::sin(phi)) + n * (3.2f * cm * (1.f - std::cos(phi)));
          segs.push_back({prev, p});
          prev = p;
        }
        break;
      }
      case DiagramArrow::Tail::tail:
        for (const float side : {1.f, -1.f}) {
          barb(path.front() + startDir * (6.2f * cm), startDir, 6.2f * cm, 6.2f * cm, 2.5f, side, segs);
        }
        break;
      case DiagramArrow::Tail::bar:
        segs.push_back({path.front() + leftOf(startDir) * (4.1f * cm), path.front() - leftOf(startDir) * (4.1f * cm)});
        break;
      case DiagramArrow::Tail::head:
        for (const float side : {1.f, -1.f}) barb(path.front(), startDir * -1.f, 6.2f * cm, 6.2f * cm, 2.5f, side, segs);
        break;
      case DiagramArrow::Tail::implies:
        for (const float side : {1.f, -1.f}) barb(path.front(), startDir * -1.f, 8.5f * cm, 6.4f * cm, 2.2f, side, segs);
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

}  // namespace microtex
