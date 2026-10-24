// TikZ pictures: the stroked path the recorder keeps for them, and the
// tikzpicture environment itself (engine.h). Positions come from the stub text
// measurer, so node sizes are compared with each other and with what the same
// text is without them.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

// A path that is stroked is a record of its own, with its width, cap and join,
// where a filled one is a shape: the stroke is not turned into segments of a
// line or into a polygon.
TEST(tikz_the_recorder_keeps_a_stroked_path_with_its_width_cap_and_join) {
  microtex::Graphics2D_Recorder g;
  g.setColor(0xFFFF0000u);
  g.setStroke(microtex::Stroke(2.f, microtex::CAP_ROUND, microtex::JOIN_MITER));
  g.beginPath(-1);
  g.moveTo(1.f, 2.f);
  g.lineTo(3.f, 4.f);
  g.cubicTo(5.f, 6.f, 7.f, 8.f, 9.f, 10.f);
  g.strokePath(-1);
  g.beginPath(-1);
  g.moveTo(0.f, 0.f);
  g.lineTo(1.f, 0.f);
  g.lineTo(1.f, 1.f);
  g.closePath();
  g.fillPath(-1);
  const auto& records = g.records();
  CHECK_EQ(records.size(), std::size_t(2));
  CHECK(records[0].type == DrawRecord::STROKE_PATH);
  CHECK_EQ(records[0].col, 0xFFFF0000u);
  CHECK(std::fabs(records[0].line_width - 2.f) < 1e-4f);
  CHECK_EQ(records[0].cap, int(microtex::CAP_ROUND));
  CHECK_EQ(records[0].join, int(microtex::JOIN_MITER));
  CHECK_EQ(records[0].path_segments.size(), std::size_t(3));
  CHECK(records[0].path_segments[2].cmd == DrawRecord::PathSegment::CUBIC);
  CHECK(records[1].type == DrawRecord::PATH);
  CHECK_EQ(records[1].path_segments.size(), std::size_t(4));
}

namespace {

// At a size of 10 a TeX point is a pixel and a centimetre 28.45 of them: lengths compare to a pixel.
check::Layout pic(const std::string& body, InputMode mode = InputMode::math) {
  Options opt(mode);
  opt.path = false;
  opt.textSize = 10;
  return check::render("\\begin{tikzpicture}" + body + "\\end{tikzpicture}", opt);
}

check::Layout plain(const std::string& tex) {
  Options opt(InputMode::math);
  opt.path = false;
  opt.textSize = 10;
  return check::render(tex, opt);
}

bool near(float a, float b, float tolerance = 1.5f) { return std::fabs(a - b) <= tolerance; }

Records strokes(const check::Layout& l) { return check::of(l, DrawRecord::STROKE_PATH); }
Records fills(const check::Layout& l) { return check::of(l, DrawRecord::PATH); }

}  // namespace

// A picture is the box of its path: pgf's bounding box, in centimetres (a bare number is one), with
// nothing below the baseline. TeX's \wd of `\tikz \draw (0,0) -- (2,1);` is 56.9pt, 2cm.
TEST(tikz_a_picture_is_as_big_as_its_path) {
  const auto line = pic("\\draw (0,0) -- (2,1);");
  CHECK(near(static_cast<float>(line.width), static_cast<float>(plain("\\hspace{2cm}").width)));
  CHECK(near(static_cast<float>(line.height), static_cast<float>(plain("\\rule{0pt}{1cm}").height)));
  CHECK_EQ(line.depth, 0);
  CHECK(line.warnings.empty());
  // Units and the spelling of a coordinate.
  CHECK(near(static_cast<float>(pic("\\draw (0,0) -- (20mm,1cm);").width), static_cast<float>(line.width)));
  CHECK(near(static_cast<float>(pic("\\draw (1,1) -- (3,2);").width), static_cast<float>(line.width)));
}

// \draw is \path[draw]: one stroked path of the line width 0.4pt; \fill fills and does not stroke.
TEST(tikz_draw_strokes_and_fill_fills) {
  const auto d = pic("\\draw (0,0) -- (2,1);");
  CHECK_EQ(strokes(d).size(), std::size_t(1));
  CHECK_EQ(fills(d).size(), std::size_t(0));
  const auto rule = check::of(plain("\\rule{1cm}{0.4pt}"), DrawRecord::LINE);
  CHECK_EQ(rule.size(), std::size_t(1));
  CHECK(near(strokes(d)[0].line_width, rule[0].line_width, 0.2f));
  // The line from (0,0) to (2cm,1cm): the second point is right and up, up being smaller y.
  const auto s = strokes(d)[0].path_segments;
  CHECK_EQ(s.size(), std::size_t(2));
  CHECK(near(s[1].coords[0] - s[0].coords[0], static_cast<float>(plain("\\hspace{2cm}").width)));
  CHECK(near(s[0].coords[1] - s[1].coords[1], static_cast<float>(plain("\\rule{0pt}{1cm}").height)));
  const auto f = pic("\\fill (0,0) rectangle (1,1);");
  CHECK_EQ(strokes(f).size(), std::size_t(0));
  CHECK_EQ(fills(f).size(), std::size_t(1));
  const auto both = pic("\\filldraw[fill=red, draw=blue] (0,0) rectangle (1,1);");
  CHECK_EQ(strokes(both).size(), std::size_t(1));
  CHECK_EQ(fills(both).size(), std::size_t(1));
  CHECK_EQ(strokes(both)[0].col, 0xFF0000FFu);
  CHECK_EQ(fills(both)[0].col, 0xFFFF0000u);
}

// xcolor's mixes are colours here too, and opacity is the colour's alpha.
TEST(tikz_colours_are_xcolors_and_opacity_is_their_alpha) {
  CHECK_EQ(strokes(pic("\\draw[red] (0,0) -- (1,0);"))[0].col, 0xFFFF0000u);
  CHECK_EQ(strokes(pic("\\draw[color=blue!50!black] (0,0) -- (1,0);"))[0].col, 0xFF000080u);
  CHECK_EQ(fills(pic("\\fill[blue!30] (0,0) rectangle (1,1);"))[0].col, 0xFFB3B3FFu);
  CHECK_EQ(fills(pic("\\fill[red, fill opacity=0.5] (0,0) rectangle (1,1);"))[0].col, 0x80FF0000u);
  CHECK_EQ(strokes(pic("\\draw[red, draw opacity=0.5] (0,0) -- (1,0);"))[0].col, 0x80FF0000u);
}

// pgf's line widths: thin 0.4pt, thick 0.8pt, very thick 1.2pt, ultra thick 1.6pt, line width=3pt.
TEST(tikz_line_widths_are_pgfs) {
  const auto lw = [](const std::string& options) { return strokes(pic("\\draw[" + options + "] (0,0) -- (1,0);"))[0].line_width; };
  const float thin = lw("thin");
  CHECK(near(lw("thick"), 2 * thin, 0.2f));
  CHECK(near(lw("very thick"), 3 * thin, 0.2f));
  CHECK(near(lw("ultra thick"), 4 * thin, 0.2f));
  CHECK(near(lw("line width=3pt"), 7.5f * thin, 0.3f));
  CHECK(near(lw(""), thin, 0.2f));
}

// A path closed by `cycle` or `rectangle` is one subpath ending in a close; a circle is four cubics.
TEST(tikz_closed_paths_and_circles) {
  const auto rect = strokes(pic("\\draw (0,0) rectangle (3,2);"));
  CHECK_EQ(rect.size(), std::size_t(1));
  CHECK_EQ(rect[0].path_segments.size(), std::size_t(5));  // move, three lines, close
  CHECK(rect[0].path_segments.back().cmd == DrawRecord::PathSegment::CLOSE);
  const auto tri = strokes(pic("\\draw (0,0) -- (2,0) -- (1,1.5) -- cycle;"));
  CHECK(tri[0].path_segments.back().cmd == DrawRecord::PathSegment::CLOSE);
  const auto circle = pic("\\draw (1,1) circle (0.8);");
  int cubics = 0;
  const auto circleStrokes = strokes(circle);
  for (const auto& seg : circleStrokes[0].path_segments) cubics += seg.cmd == DrawRecord::PathSegment::CUBIC;
  CHECK_EQ(cubics, 4);
  CHECK(near(static_cast<float>(circle.width), static_cast<float>(plain("\\hspace{1.6cm}").width)));
  const auto radius = pic("\\draw (3,1) circle [radius=5mm];");
  CHECK(near(static_cast<float>(radius.width), static_cast<float>(plain("\\hspace{1cm}").width)));
  const auto ellipse = pic("\\draw (0,0) ellipse (1.5 and 0.6);");
  CHECK(near(static_cast<float>(ellipse.width), static_cast<float>(plain("\\hspace{3cm}").width)));
  CHECK(near(static_cast<float>(ellipse.height), static_cast<float>(plain("\\rule{0pt}{1.2cm}").height)));
}

// Polar and relative coordinates: (30:2cm) is (1.732cm, 1cm); ++ moves the current point, + does not.
TEST(tikz_polar_and_relative_coordinates) {
  const auto polar = pic("\\draw (0,0) -- (30:2cm);");
  CHECK(near(static_cast<float>(polar.width), static_cast<float>(plain("\\hspace{1.7320508cm}").width)));
  CHECK(near(static_cast<float>(polar.height), static_cast<float>(plain("\\rule{0pt}{1cm}").height)));
  const auto moving = pic("\\draw (0,0) -- ++(1,0) -- ++(0,1);");
  CHECK(near(static_cast<float>(moving.width), static_cast<float>(plain("\\hspace{1cm}").width)));
  CHECK(near(static_cast<float>(moving.height), static_cast<float>(plain("\\rule{0pt}{1cm}").height)));
  const auto staying = pic("\\draw (3,0) -- +(1,1) -- +(2,0);");
  CHECK(near(static_cast<float>(staying.width), static_cast<float>(plain("\\hspace{2cm}").width)));
}

// `-|` goes across and then up; `|-` up and then across.
TEST(tikz_horizontal_and_vertical_lines) {
  const auto across = strokes(pic("\\draw (0,0) -| (2,1.5);"))[0].path_segments;
  CHECK_EQ(across.size(), std::size_t(3));
  CHECK(near(across[1].coords[1], across[0].coords[1]));           // the first leg is level
  CHECK(near(across[2].coords[0], across[1].coords[0]));           // the second is upright
  const auto up = strokes(pic("\\draw (0,0) |- (2,1.5);"))[0].path_segments;
  CHECK(near(up[1].coords[0], up[0].coords[0]));
  CHECK(near(up[2].coords[1], up[1].coords[1]));
}

// `scale=0.5` halves coordinates and not the line width.
TEST(tikz_scale_shrinks_the_coordinates_and_not_the_line) {
  const auto full = pic("\\draw (0,0) rectangle (4,2);");
  const auto half = pic("[scale=0.5]\\draw (0,0) rectangle (4,2);");
  CHECK(near(static_cast<float>(half.width) * 2, static_cast<float>(full.width)));
  CHECK(near(strokes(half)[0].line_width, strokes(full)[0].line_width, 0.05f));
}

// A dash pattern cuts the line: on 1cm off 1cm along 5cm makes three dashes of 1cm.
TEST(tikz_a_dash_pattern_cuts_the_line_into_dashes) {
  const auto d = strokes(pic("\\draw[dash pattern=on 1cm off 1cm] (0,0) -- (5,0);"));
  CHECK_EQ(d.size(), std::size_t(3));
  for (const auto& s : d) {
    CHECK_EQ(s.path_segments.size(), std::size_t(2));
    CHECK(near(s.path_segments[1].coords[0] - s.path_segments[0].coords[0], static_cast<float>(plain("\\hspace{1cm}").width)));
  }
  // dashed is on 3pt off 3pt: a 3cm line has ceil(85.04 / 6) = 15 of them.
  CHECK_EQ(strokes(pic("\\draw[dashed] (0,0) -- (3,0);")).size(), std::size_t(15));
  CHECK_EQ(strokes(pic("\\draw[solid] (0,0) -- (3,0);")).size(), std::size_t(1));
}

// pgf's rounded corners: each corner is a cubic, the line stopping short of it by the radius.
TEST(tikz_rounded_corners_are_cubics_that_stop_short_of_the_corner) {
  const auto r = strokes(pic("\\draw[rounded corners=4pt] (0,0) -- (2,0) -- (2,1.5) -- (0,1.5) -- cycle;"));
  int cubics = 0;
  for (const auto& seg : r[0].path_segments) cubics += seg.cmd == DrawRecord::PathSegment::CUBIC;
  CHECK_EQ(cubics, 4);
  const auto plainBox = strokes(pic("\\draw (0,0) -- (2,0) -- (2,1.5) -- (0,1.5) -- cycle;"));
  for (const auto& seg : plainBox[0].path_segments) CHECK(seg.cmd != DrawRecord::PathSegment::CUBIC);
}

// What is not read is said once and left out.
TEST(tikz_an_option_that_is_not_supported_warns_once) {
  const auto l = pic("\\draw[nosuchoption] (0,0) -- (1,0); \\draw[nosuchoption] (0,1) -- (1,1);");
  CHECK_EQ(l.warnings.size(), std::size_t(1));
  CHECK(check::warns(l, "nosuchoption"));
  CHECK_EQ(strokes(l).size(), std::size_t(2));
}

namespace {

// The extent of a stroked path's points.
struct Extent {
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
  float width() const { return x1 - x0; }
  float height() const { return y1 - y0; }
};

Extent extentOf(const DrawRecord& r) {
  Extent e;
  for (const auto& s : r.path_segments) {
    // The control points of a curve count: pgf's bounding box includes them.
    const int points = s.cmd == DrawRecord::PathSegment::CUBIC ? 3 : s.cmd == DrawRecord::PathSegment::CLOSE ? 0 : 1;
    for (int i = 0; i < points; i++) {
      e.x0 = std::min(e.x0, s.coords[2 * i]);
      e.x1 = std::max(e.x1, s.coords[2 * i]);
      e.y0 = std::min(e.y0, s.coords[2 * i + 1]);
      e.y1 = std::max(e.y1, s.coords[2 * i + 1]);
    }
  }
  return e;
}

float cm(double n) { return static_cast<float>(plain("\\hspace{" + std::to_string(n) + "cm}").width); }

}  // namespace

// pgf's rectangle node is its text and `inner sep` (0.3333em) on each side: the box drawn grows by
// twice the inner sep when it is not zero, and by the same text whatever the sep is.
TEST(tikz_a_node_is_its_text_and_its_inner_sep) {
  const auto bare = strokes(pic(R"(\node[draw, inner sep=0pt] {ab};)"));
  const auto def = strokes(pic(R"(\node[draw] {ab};)"));
  const auto big = strokes(pic(R"(\node[draw, inner sep=5pt] {ab};)"));
  CHECK_EQ(bare.size(), std::size_t(1));
  const float gap = static_cast<float>(plain(R"(\hspace{0.66667em})").width);
  CHECK(near(extentOf(def[0]).width() - extentOf(bare[0]).width(), gap));
  CHECK(near(extentOf(def[0]).height() - extentOf(bare[0]).height(), gap));
  CHECK(near(extentOf(big[0]).width() - extentOf(bare[0]).width(), static_cast<float>(plain(R"(\hspace{10pt})").width)));
  // A longer text is a wider box; the minimum size is a floor.
  CHECK(extentOf(strokes(pic(R"(\node[draw] {abcd};)"))[0]).width() > extentOf(def[0]).width());
  // (At this size two letters are 110 px and 3.9 cm is 110: the floors are above that.)
  const auto floor = strokes(pic(R"(\node[draw, minimum width=8cm, minimum height=7cm] {ab};)"));
  CHECK(near(extentOf(floor[0]).width(), cm(8)));
  CHECK(near(extentOf(floor[0]).height(), cm(7)));
}

// A circle node is the circle through the corners of its text box; its anchors lie on the circle.
TEST(tikz_a_circle_node_is_round) {
  const auto c = strokes(pic(R"(\node[draw, circle] {ab};)"));
  CHECK_EQ(c.size(), std::size_t(1));
  int cubics = 0;
  for (const auto& s : c[0].path_segments) cubics += s.cmd == DrawRecord::PathSegment::CUBIC;
  CHECK_EQ(cubics, 4);
  CHECK(near(extentOf(c[0]).width(), extentOf(c[0]).height()));
  const auto sized = strokes(pic(R"(\node[draw, circle, minimum size=4cm] {};)"));
  CHECK(near(extentOf(sized[0]).width(), cm(4)));
}

// `right=1cm of a` leaves 1cm between the two borders; `below=of a` leaves `node distance` (1cm).
TEST(tikz_positioning_puts_a_node_beside_another_with_a_gap_between_the_borders) {
  const auto h = strokes(pic(R"(\node[draw] (a) {A}; \node[draw, right=1cm of a] (b) {B};)"));
  CHECK_EQ(h.size(), std::size_t(2));
  CHECK(near(extentOf(h[1]).x0 - extentOf(h[0]).x1, cm(1)));
  CHECK(near(extentOf(h[1]).y0, extentOf(h[0]).y0));
  const auto v = strokes(pic(R"(\node[draw] (a) {A}; \node[draw, below=of a] (b) {B};)"));
  CHECK(near(extentOf(v[1]).y0 - extentOf(v[0]).y1, cm(1)));  // y grows downwards in the layout
  CHECK(near(extentOf(v[1]).x0, extentOf(v[0]).x0));
  const auto d = strokes(pic(R"(\node[draw] (a) {A}; \node[draw, above right=of a] (b) {B};)"));
  CHECK(extentOf(d[1]).x0 > extentOf(d[0]).x1 && extentOf(d[1]).y1 < extentOf(d[0]).y0);
  const auto nd = strokes(pic(R"([node distance=2cm]\node[draw] (a) {A}; \node[draw, right=of a] (b) {B};)"));
  CHECK(near(extentOf(nd[1]).x0 - extentOf(nd[0]).x1, cm(2)));
  // The old spelling is from centre to centre.
  const auto old = strokes(pic(R"([node distance=3cm]\node[draw] (a) {A}; \node[draw, right of=a] (b) {B};)"));
  CHECK(near((extentOf(old[1]).x0 + extentOf(old[1]).x1) / 2 - (extentOf(old[0]).x0 + extentOf(old[0]).x1) / 2, cm(3)));
}

// A path between two named nodes runs border to border, and `(a.north)` is the middle of the top edge.
TEST(tikz_a_line_between_nodes_ends_on_their_borders) {
  const auto l = strokes(pic(R"(\node[draw] (a) {A}; \node[draw, right=2cm of a] (b) {B}; \draw (a) -- (b);)"));
  CHECK_EQ(l.size(), std::size_t(3));
  CHECK(near(extentOf(l[2]).x0, extentOf(l[0]).x1));
  CHECK(near(extentOf(l[2]).x1, extentOf(l[1]).x0));
  const auto n = strokes(pic(R"(\node[draw] (a) at (0,0) {A}; \draw (a.north) -- ++(0,1);)"));
  CHECK(near(extentOf(n[1]).y1, extentOf(n[0]).y0));
  CHECK(near(extentOf(n[1]).x0, (extentOf(n[0]).x0 + extentOf(n[0]).x1) / 2));
  CHECK(near(extentOf(n[1]).height(), cm(1)));
}

// A node on a path: `midway` is half way along the segment before it, and `above` puts its south at that point.
TEST(tikz_a_node_on_a_path_sits_at_a_position_along_it) {
  const auto mid = pic(R"(\draw (0,0) -- (4,0) node[midway] {m};)");
  const auto start = pic(R"(\draw (0,0) -- (4,0) node[pos=0] {m};)");
  const auto end = pic(R"(\draw (0,0) -- (4,0) node[pos=1] {m};)");
  // Where the text stands from the start of the line: the picture grows with its nodes.
  const auto x = [](const check::Layout& l) {
    const auto t = check::of(l, DrawRecord::TEXT);
    return t.empty() ? -1e9f : t[0].x - extentOf(strokes(l)[0]).x0;
  };
  CHECK(near(x(mid) - x(start), cm(2), 2.5f));
  CHECK(near(x(end) - x(start), cm(4), 2.5f));
}

// `pos` on an arc is a fraction of the arc; on a circle or an ellipse pgf has no segment and the node stays at the
// centre (pdfTeX: `\draw circle (1cm) node[pos=.5]` is at the centre, `arc (0:180:1cm) node[pos=.5]` at the top).
TEST(tikz_pos_on_an_arc_follows_the_arc_and_on_a_circle_is_the_centre) {
  // Where the text stands from the lower left corner of the first stroke.
  const auto at = [](const check::Layout& l) {
    const auto t = check::of(l, DrawRecord::TEXT);
    const auto e = extentOf(strokes(l)[0]);
    return std::make_pair(t.empty() ? -1e9f : t[0].x - e.x0, t.empty() ? -1e9f : t[0].y - e.y0);
  };
  const auto centre = at(pic(R"(\draw (0,0) circle (1cm) node {m};)"));
  const auto onCircle = at(pic(R"(\draw (0,0) circle (1cm) node[pos=0.5] {m};)"));
  CHECK(near(onCircle.first, centre.first));
  CHECK(near(onCircle.second, centre.second));
  const auto top = at(pic(R"(\draw (0,0) arc (0:180:1cm); \node at (-1cm,1cm) {m};)"));
  const auto onArc = at(pic(R"(\draw (0,0) arc (0:180:1cm) node[pos=0.5] {m};)"));
  CHECK(near(onArc.first, top.first));
  CHECK(near(onArc.second, top.second));
}

// `->` is a tip of its own and the line stops short of it; `-latex` is filled and the line stops 9d short (d = 0.4pt).
TEST(tikz_an_arrow_tip_is_a_shape_of_its_own_and_the_line_stops_where_it_begins) {
  const auto plainLine = strokes(pic(R"(\draw (0,0) -- (2,0);)"));
  const auto to = pic(R"(\draw[->] (0,0) -- (2,0);)");
  CHECK_EQ(strokes(to).size(), std::size_t(2));
  CHECK(extentOf(strokes(to)[0]).x1 <= extentOf(plainLine[0]).x1);
  CHECK(near(extentOf(strokes(to)[1]).x1, extentOf(plainLine[0]).x1, 0.7f));  // the tip points at the end
  const auto latex = pic(R"(\draw[-latex] (0,0) -- (2,0);)");
  CHECK_EQ(fills(latex).size(), std::size_t(1));
  CHECK(near(extentOf(strokes(latex)[0]).x1, extentOf(plainLine[0]).x1 - static_cast<float>(plain(R"(\hspace{3.6pt})").width), 0.7f));
  CHECK(near(extentOf(fills(latex)[0]).x1, extentOf(plainLine[0]).x1, 0.7f));
  // The two ends, and the tip of arrows.meta with its own length.
  CHECK_EQ(strokes(pic(R"(\draw[<->] (0,0) -- (2,0);)")).size(), std::size_t(3));
  const auto meta = pic(R"(\draw[-{Stealth[length=4mm]}] (0,0) -- (2,0);)");
  CHECK(meta.warnings.empty());
  // The tip scales with its length: 8mm is twice 4mm.
  const float shortTip = extentOf(fills(meta)[0]).width();
  CHECK(near(extentOf(fills(pic(R"(\draw[-{Stealth[length=8mm]}] (0,0) -- (2,0);)"))[0]).width(), 2 * shortTip, 1.5f));
  CHECK(shortTip > cm(0.3) && shortTip < cm(0.45));
  // `>=latex` changes what `->` means.
  CHECK_EQ(fills(pic(R"(\draw[>=latex, ->] (0,0) -- (2,0);)")).size(), std::size_t(1));
}

// A `to` with `bend left` is a curve and so is `out=..., in=...`; a straight one is a line; a loop leaves and returns.
TEST(tikz_to_draws_a_line_or_a_curve) {
  const auto cubics = [](const check::Layout& l) {
    int n = 0;
    for (const auto& r : strokes(l)) {
      for (const auto& s : r.path_segments) n += s.cmd == DrawRecord::PathSegment::CUBIC;
    }
    return n;
  };
  CHECK_EQ(cubics(pic(R"(\draw (0,0) to (2,1);)")), 0);
  CHECK_EQ(cubics(pic(R"(\draw (0,0) to[bend left=30] (2,0);)")), 1);
  CHECK_EQ(cubics(pic(R"(\draw (0,0) to[out=90, in=180] (2,1);)")), 1);
  // pgf's loops have an arrow (`every loop`): the curve, and the tip's two curves.
  const auto loop = strokes(pic(R"(\node[draw] (a) {A}; \draw (a) to[loop above] (a);)"));
  CHECK_EQ(loop.size(), std::size_t(3));
  CHECK_EQ(loop[1].path_segments.size(), std::size_t(2));
  CHECK(loop[1].path_segments[1].cmd == DrawRecord::PathSegment::CUBIC);
  CHECK(extentOf(loop[1]).y0 < extentOf(loop[0]).y0);  // it goes up from the node
  // bend left=30: the curve leaves the straight line on the left, up for a line that runs right.
  const auto b = strokes(pic(R"(\draw (0,0) to[bend left=30] (3,0);)"));
  CHECK(extentOf(b[0]).height() > 5.f);
  const auto r = strokes(pic(R"(\draw (0,0) to[bend right=30] (3,0);)"));
  CHECK(near(extentOf(r[0]).height(), extentOf(b[0]).height(), 1.f));
  // The picture's box is that of the curve, so the ends are at the foot for the one and at the top for the other.
  CHECK(b[0].path_segments[0].coords[1] > extentOf(b[0]).y0 + 1.f);
  CHECK(near(r[0].path_segments[0].coords[1], extentOf(r[0]).y0, 1.f));
  // An edge is a path of its own and the current point stays.
  CHECK_EQ(strokes(pic(R"(\node[draw] (a) {A}; \node[draw, right=of a] (b) {B}; \path (a) edge (b);)")).size(), std::size_t(3));
}

// \foreach is the same picture written out; `...` counts on from the two values before it; two variables go together.
// pgfmath calls `3mm` and `\i*3mm` dimensions, and a coordinate is in centimetres: (2*3mm,0) is (0.6,0), and so on. pdfTeX
// gives the same positions; these were read as nothing, and drawn at the origin with no warning.
TEST(tikz_a_unit_in_a_coordinate_expression_is_a_length) {
  CHECK_PAIRS((check::Pairs{
    {R"(\begin{tikzpicture}\draw (0,0) -- (2*3mm, 1);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (0,0) -- (0.6, 1);\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\foreach \i in {1,2} \fill (-\i * 3mm, 0) circle (1pt);\end{tikzpicture})",
     R"(\begin{tikzpicture}\fill (-0.3, 0) circle (1pt);\fill (-0.6, 0) circle (1pt);\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\draw (0,0) -- (1cm + 5mm, 10pt*2);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (0,0) -- (1.5cm, 20pt);\end{tikzpicture})"}}));
}

// A coordinate that is neither a point nor a node drawn before it is an error in TikZ ("No shape"); here the path is
// left out, and the picture says so. Cells of a matrix and children of a tree are nodes, and so is `current bounding box`.
TEST(tikz_a_coordinate_that_is_no_node_warns) {
  const auto warns = [](const std::string& body) {
    return check::warns(check::render("\\begin{tikzpicture}" + body + "\\end{tikzpicture}"), "is not a node drawn before it");
  };
  CHECK(warns(R"(\draw (0,0) -- (nosuch);)"));
  CHECK(warns(R"(\draw (0,0) -- (foo bar, 1);)"));
  CHECK(warns(R"(\draw (b) -- (0,0); \node (b) {B};)"));
  CHECK(!warns(R"(\node (a) {A}; \node (b) at (1,0) {B}; \draw (a) -- (b.north);)"));
  CHECK(!warns(R"(\coordinate (c) at (1,1); \draw (0,0) -- (c);)"));
  CHECK(!warns(R"(\matrix (m) [matrix of nodes] {a & b \\}; \draw (m-1-1) -- (m-1-2);)"));
  CHECK(!warns(R"(\node (p) {P} child {node {c}}; \draw (p) -- (p-1);)"));
  CHECK(!warns(R"(\node {x}; \draw (current bounding box.south west) -- (0,0);)"));
  CHECK(!warns(R"(\foreach \i in {1,2} \node (n\i) at (\i,0) {}; \draw (n1) -- (n2);)"));
}

TEST(tikz_foreach_draws_what_the_loop_written_out_would) {
  CHECK_PAIRS((check::Pairs{
    {R"(\begin{tikzpicture}\foreach \x in {0,1,2} \draw (\x,0) -- (\x,1);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (0,0) -- (0,1);\draw (1,0) -- (1,1);\draw (2,0) -- (2,1);\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\foreach \x in {0,2,...,6} \fill (\x,0) circle (2pt);\end{tikzpicture})",
     R"(\begin{tikzpicture}\fill (0,0) circle (2pt);\fill (2,0) circle (2pt);\fill (4,0) circle (2pt);\fill (6,0) circle (2pt);\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\foreach \x/\y in {0/1,1/2} \draw (\x,0) -- (\x,\y);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (0,0) -- (0,1);\draw (1,0) -- (1,2);\end{tikzpicture})"},
    // \i and \j are macros of the format (the dotless letters), and a loop's own all the same.
    {R"(\begin{tikzpicture}\foreach \i in {1,2} \foreach \j in {1,2} \draw (\i,0) -- (\j,1);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (1,0) -- (1,1);\draw (1,0) -- (2,1);\draw (2,0) -- (1,1);\draw (2,0) -- (2,1);\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\foreach \i in {1,2} {\node[draw] (n\i) at (\i,0) {};} \draw (n1) -- (n2);\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (n1) at (1,0) {};\node[draw] (n2) at (2,0) {};\draw (n1) -- (n2);\end{tikzpicture})"},
  }));
}

// `\tikzset{name/.style={...}}` and `\tikzstyle{name}=[...]`, before the picture or in it, are what the options spell.
TEST(tikz_a_style_is_the_options_it_names) {
  CHECK_PAIRS((check::Pairs{
    {R"(\tikzset{box/.style={draw, fill=red}}\begin{tikzpicture}\node[box] {a};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw, fill=red] {a};\end{tikzpicture})"},
    {R"(\tikzstyle{box} = [draw, fill=red]\begin{tikzpicture}\node[box] {a};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw, fill=red] {a};\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\tikzset{box/.style={draw, fill=red}}\node[box] {a};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw, fill=red] {a};\end{tikzpicture})"},
    {R"(\begin{tikzpicture}[every node/.style={draw}]\node {a};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] {a};\end{tikzpicture})"},
  }));
}

// pgf's ellipse is the one through the corners of the box (1.4142136 of its half sides): a 1:1 box gives
// radii 1.4142 times half of it; its `north east` anchor is at 0.707107 of the radii; `(a) -- (b)` ends on it.
TEST(tikz_an_ellipse_is_the_ellipse_through_the_corners_of_its_box) {
  const auto box = strokes(pic(R"(\node[draw] {ab};)"));
  const auto ell = strokes(pic(R"(\node[draw, ellipse] {ab};)"));
  CHECK_EQ(ell.size(), std::size_t(1));
  // The outline is cubics; its extent is the radii, outer separation taken off.
  CHECK(near(extentOf(ell[0]).width(), 1.4142136f * extentOf(box[0]).width(), 2.f));
  CHECK(near(extentOf(ell[0]).height(), 1.4142136f * extentOf(box[0]).height(), 2.f));
  // (At this size the ellipse of two letters is some 250 px wide: the floors are above that.)
  const auto floor = strokes(pic(R"(\node[draw, ellipse, minimum width=12cm, minimum height=10cm] {ab};)"));
  CHECK(near(extentOf(floor[0]).width(), cm(12), 1.f));
  CHECK(near(extentOf(floor[0]).height(), cm(10), 1.f));
  // The line from an ellipse to a node on its right starts where the ray meets the ellipse: on its east tip.
  const auto l = strokes(pic(R"(\node[draw, ellipse] (a) {A}; \node[draw, right=2cm of a] (b) {B}; \draw (a) -- (b);)"));
  CHECK(near(extentOf(l[2]).x0, extentOf(l[0]).x1 + 0.0f, 1.5f));
}

// pgf's diamond: tips at the half box widened by the other half side times the aspect; `aspect=2` is twice as wide
// as high where the tips were level.
TEST(tikz_a_diamond_has_its_tips_where_pgf_puts_them) {
  const auto box = strokes(pic(R"(\node[draw] {ab};)"));
  const auto d = strokes(pic(R"(\node[draw, diamond] {ab};)"));
  CHECK_EQ(d.size(), std::size_t(1));
  CHECK_EQ(d[0].path_segments.size(), std::size_t(5));  // four tips and the close
  const float hw = extentOf(box[0]).width() / 2, hh = extentOf(box[0]).height() / 2;
  CHECK(near(extentOf(d[0]).width(), 2 * (hw + hh), 2.f));
  CHECK(near(extentOf(d[0]).height(), 2 * (hw + hh), 2.f));
  const auto wide = strokes(pic(R"(\node[draw, diamond, aspect=2] {ab};)"));
  CHECK(near(extentOf(wide[0]).width(), 2 * (hw + 2 * hh), 2.f));
  CHECK(near(extentOf(wide[0]).height(), 2 * (hw / 2 + hh), 2.f));
  // The line from a diamond starts on its edge: halfway between a tip and the next for the diagonal.
  const auto l = strokes(pic(R"(\node[draw, diamond] (a) at (0,0) {A}; \draw (a.north east) -- ++(0,1);)"));
  const Extent e = extentOf(l[0]);
  CHECK(near(l[1].path_segments[0].coords[0], e.x0 + extentOf(l[1]).width() / 2 + 0.f, 1e9f));
  CHECK(near(l[1].path_segments[0].coords[0], (e.x0 + e.x1) / 2 + (e.x1 - e.x0) / 4, 1.5f));
}

// A rounded rectangle is a stadium by default: two semicircles over its height at the ends, the tips farther out
// than the box by the chord the text leaves; its width keeps the minimum.
TEST(tikz_a_rounded_rectangle_has_semicircular_ends) {
  const auto r = strokes(pic(R"(\node[draw, rounded rectangle] {ab};)"));
  CHECK_EQ(r.size(), std::size_t(1));
  int cubics = 0;
  for (const auto& s : r[0].path_segments) cubics += s.cmd == DrawRecord::PathSegment::CUBIC;
  CHECK_EQ(cubics, 4);
  const auto box = strokes(pic(R"(\node[draw] {ab};)"));
  CHECK(near(extentOf(r[0]).height(), extentOf(box[0]).height(), 1.f));
  CHECK(extentOf(r[0]).width() > extentOf(box[0]).width());
  // pgf takes the extra width as `(minimum width - natural width) / 2` on each side, so the node ends up
  // 2 inner sep short of the minimum: pdfTeX's `Wide` of `minimum width=4cm` is that much narrower (read
  // from the same node set in both, tools/tikz-ref h05).
  const auto wide = strokes(pic(R"(\node[draw, rounded rectangle, minimum width=12cm] {ab};)"));
  CHECK(near(extentOf(wide[0]).width(), cm(12) - static_cast<float>(plain(R"(\hspace{0.66667em})").width), 2.f));
}

// A label is a node on the border of its owner, anchored on the side that faces it (`tikz@compute@direction`):
// south for `above`, west for `right`, `south west` for 45 degrees.
TEST(tikz_a_label_is_a_node_on_the_border_of_the_one_it_belongs_to) {
  CHECK_PAIRS((check::Pairs{
    {R"(\begin{tikzpicture}\node[draw, label=above:T] (a) at (0,0) {x};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (a) at (0,0) {x};\node[anchor=south] at (a.north) {T};\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\node[draw, label=right:T] (a) at (0,0) {x};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (a) at (0,0) {x};\node[anchor=west] at (a.east) {T};\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\node[draw, label=45:T] (a) at (0,0) {x};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (a) at (0,0) {x};\node[anchor=south west] at (a.45) {T};\end{tikzpicture})"},
    // With no angle it is above; the options in brackets are the label's own.
    {R"(\begin{tikzpicture}\node[draw, label=T] (a) at (0,0) {x};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw, label=above:T] (a) at (0,0) {x};\end{tikzpicture})"},
    {R"(\begin{tikzpicture}\node[draw, label={[red]below:T}] (a) at (0,0) {x};\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (a) at (0,0) {x};\node[anchor=north, red] at (a.south) {T};\end{tikzpicture})"},
  }));
}

// `label distance` is the enclosing scope's: pdfTeX leaves a node's own `label distance=3mm` unused (a picture of
// 49 px high, as with none) and spaces the label by it from the picture or the path (67 px).
TEST(tikz_label_distance_is_read_from_the_scope_round_the_node) {
  const auto height = [](const std::string& tex) { return static_cast<float>(pic(tex).height); };
  const std::string label = R"(\node[draw, label=above:Top] at (0,0) {y};)";
  const float none = height(label);
  CHECK(near(height("[label distance=3mm]" + label), none + cm(0.3), 1.f));
  CHECK(near(height(R"(\path[label distance=3mm] node[draw, label=above:Top] at (0,0) {y};)"), none + cm(0.3), 1.f));
  CHECK(near(height(R"(\node[draw, label distance=3mm, label=above:Top] at (0,0) {y};)"), none, 0.5f));
}

// A pin is a label with a line to it, gray and 0.2pt (`help lines`), and 3ex out by default.
TEST(tikz_a_pin_is_a_label_with_a_line) {
  const auto p = strokes(pic(R"(\node[draw, pin=above:T] at (0,0) {y};)"));
  CHECK_EQ(p.size(), std::size_t(2));
  CHECK((p[1].col & 0xFFFFFFu) == 0x808080u || (p[1].col & 0xFFFFFFu) == 0x7F7F7Fu);  // gray, half way
  CHECK(p[1].line_width < p[0].line_width);
  CHECK_EQ(strokes(pic(R"(\node[draw, label=above:T] at (0,0) {y};)")).size(), std::size_t(1));
  // The line is upright and as long as the pin distance, 3ex.
  CHECK(near(extentOf(p[1]).width(), 0.f, 0.5f));
  CHECK(near(extentOf(p[1]).height(), static_cast<float>(plain(R"(\hspace{3ex})").width), 1.5f));
  // The pin's own `pin edge` gives the line its tip.
  CHECK(strokes(pic(R"(\node[draw, pin={[pin edge={->}]above:T}] at (0,0) {y};)")).size() > 2);
}

// tikzlibrarycalc: `($(a)!t!(b)$)` is a fraction of the way, `!1cm!` a distance, `!(p)!` the foot of p on the line,
// `+` and `-` add the points, `k*(a)` scales one; a result is the point you would have written.
TEST(tikz_calc_points_are_the_points_they_say) {
  const std::string def = R"(\coordinate (a) at (0,0); \coordinate (b) at (4,2); \coordinate (c) at (3,0);)";
  CHECK_PAIRS((check::Pairs{
    {R"(\begin{tikzpicture})" + def + R"(\draw ($(a)!0.5!(b)$) -- (0,-1);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (2,1) -- (0,-1);\end{tikzpicture})"},
    {R"(\begin{tikzpicture})" + def + R"(\draw ($(a)+(1,2)$) -- ($(b)-(a)$);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (1,2) -- (4,2);\end{tikzpicture})"},
    {R"(\begin{tikzpicture})" + def + R"(\draw ($2*(a)+0.5*(b)$) -- ($-1*(c)$);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (2,1) -- (-3,0);\end{tikzpicture})"},
    // The foot of (3,0) on the line through (0,0) and (4,2): (2.4,1.2).
    {R"(\begin{tikzpicture})" + def + R"(\draw ($(a)!(c)!(b)$) -- (0,-1);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (2.4,1.2) -- (0,-1);\end{tikzpicture})"},
    // Turned about the first point: (b) at 90 degrees from (a) is (-2,4).
    {R"(\begin{tikzpicture})" + def + R"(\draw ($(a)!0.5!90:(b)$) -- (0,-1);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (-1,2) -- (0,-1);\end{tikzpicture})"},
    // Steps chain, and a node's anchor is a point.
    {R"(\begin{tikzpicture}\node[draw] (n) at (0,0) {x};\draw ($(n.east)!0!(n.west)$) -- (3,0);\end{tikzpicture})",
     R"(\begin{tikzpicture}\node[draw] (n) at (0,0) {x};\draw (n.east) -- (3,0);\end{tikzpicture})"},
    {R"(\begin{tikzpicture})" + def + R"(\draw ($(a)!0.5!(b)!0.5!(b)$) -- (0,-1);\end{tikzpicture})",
     R"(\begin{tikzpicture})" + def + R"(\draw (3,1.5) -- (0,-1);\end{tikzpicture})"},
  }));
  // A distance walks that far from the first point towards the second: 1cm along (3,4) from the origin is (0.6,0.8).
  CHECK_PAIRS((check::Pairs{
    {R"(\begin{tikzpicture}\draw ($(0,0)!1cm!(3,4)$) -- (0,-1);\end{tikzpicture})",
     R"(\begin{tikzpicture}\draw (0.6,0.8) -- (0,-1);\end{tikzpicture})"},
  }));
  // What cannot be read is said and left out.
  CHECK(check::warns(pic(R"(\draw ($(0,0)!x!(1,1)$) -- (1,0);)"), "calculation"));
}

// `fit=(a)(b)` puts a node round the points: the box of the nodes' west, east, north and south anchors, and
// `inner sep` more on each side (tikzlibraryfit makes the text box that big).
TEST(tikz_fit_puts_a_node_round_its_points) {
  const std::string two = R"(\node[draw] (a) at (0,0) {A}; \node[draw] (b) at (3,2) {B};)";
  const auto s = strokes(pic(two + R"(\node[draw, inner sep=0pt, fit=(a)(b)] {};)"));
  CHECK_EQ(s.size(), std::size_t(3));
  const Extent ea = extentOf(s[0]), eb = extentOf(s[1]), ef = extentOf(s[2]);
  // The outer separation of the two nodes (half the line width) is inside the box they are fitted by.
  const float lw = s[0].line_width;
  CHECK(near(ef.x0, ea.x0 - lw / 2, 0.6f));
  CHECK(near(ef.x1, eb.x1 + lw / 2, 0.6f));
  CHECK(near(ef.y0, eb.y0 - lw / 2, 0.6f));
  CHECK(near(ef.y1, ea.y1 + lw / 2, 0.6f));
  // The default inner sep (0.3333em) is outside that.
  const auto padded = strokes(pic(two + R"(\node[draw, fit=(a)(b)] {};)"));
  const float gap = static_cast<float>(plain(R"(\hspace{0.33333em})").width);
  CHECK(near(extentOf(padded[2]).width() - ef.width(), 2 * gap, 1.f));
  // Plain points fit too, and so does a `(1,2)` that holds a comma.
  const auto pts = strokes(pic(R"(\node[draw, inner sep=0pt, fit={(0,0) (2,1)}] {};)"));
  CHECK(near(extentOf(pts[0]).width(), cm(2), 1.f));
  CHECK(near(extentOf(pts[0]).height(), cm(1), 1.f));
  CHECK(near(extentOf(strokes(pic(R"(\node[draw, inner sep=0pt, fit=(0,0)(2,1)] {};)"))[0]).width(), cm(2), 1.f));
}

// A background layer is drawn before the main one, whatever the order they are written in; within a layer, in order.
TEST(tikz_a_background_layer_is_drawn_first) {
  const auto order = [](const std::string& body) {
    std::vector<unsigned> cols;
    for (const auto& r : pic(body).records) {
      if (r.type == DrawRecord::PATH) cols.push_back(r.col);
    }
    return cols;
  };
  const auto plainOrder = order(R"(\fill[red] (0,0) rectangle (1,1); \fill[blue] (0,0) rectangle (1,1);)");
  CHECK_EQ(plainOrder.size(), std::size_t(2));
  CHECK_EQ(plainOrder[0], 0xFFFF0000u);
  const auto layers = order(R"(\fill[red] (0,0) rectangle (1,1);
    \begin{scope}[on background layer]\fill[blue] (0,0) rectangle (1,1);\end{scope})");
  CHECK_EQ(layers.size(), std::size_t(2));
  CHECK_EQ(layers[0], 0xFF0000FFu);
  const auto env = order(R"(\fill[red] (0,0) rectangle (1,1);
    \begin{pgfonlayer}{background}\fill[blue] (0,0) rectangle (1,1);\fill[green] (0,0) rectangle (1,1);\end{pgfonlayer})");
  CHECK_EQ(env.size(), std::size_t(3));
  CHECK_EQ(env[0], 0xFF0000FFu);
  CHECK_EQ(env[2], 0xFFFF0000u);
}

namespace {

struct Centre {
  float x, y;
};

Centre centreOf(const DrawRecord& r) {
  const Extent e = extentOf(r);
  return {(e.x0 + e.x1) / 2, (e.y0 + e.y1) / 2};
}

const std::string kBoxes = R"([every node/.style={draw}])";

}  // namespace

// pgf's trees: a child is `level distance` (15mm) from its parent in the way the tree grows (down), the n children are
// `sibling distance` (15mm) apart, centred on the parent, and each has an edge, a line from border to border.
TEST(tikz_children_are_a_level_distance_below_their_parent_and_a_sibling_distance_apart) {
  const auto one = strokes(pic(kBoxes + R"(\node {r} child {node {a}};)"));
  CHECK_EQ(one.size(), std::size_t(3));  // the two nodes, and the edge
  const Centre parent = centreOf(one[0]), child = centreOf(one[1]);
  CHECK(near(child.x, parent.x));
  CHECK(near(child.y - parent.y, cm(1.5)));  // y grows downwards in the layout
  // (A level distance that keeps the boxes of the stub font, 121 x 166 px here, clear of each other.)
  const auto three = strokes(pic(R"([level distance=6cm, every node/.style={draw}]\node {r} child {node {a}} child {node {b}} child {node {c}};)"));
  CHECK_EQ(three.size(), std::size_t(7));
  const Centre p = centreOf(three[0]);
  CHECK(near(centreOf(three[1]).x, p.x - cm(1.5)));
  CHECK(near(centreOf(three[2]).x, p.x, 1.f));
  CHECK(near(centreOf(three[3]).x, p.x + cm(1.5)));
  // The edge to the middle child runs from the parent's bottom border to the child's top border.
  const Extent edge = extentOf(three[5]);
  CHECK(near(edge.y0, extentOf(three[0]).y1, 1.f));
  CHECK(near(edge.y1, extentOf(three[2]).y0, 1.f));
}

// `level 1/.style` and `level distance` are read at the level they are for; `grow=right` turns the tree; `child[missing]`
// leaves its place empty; children are named `parent-j`.
TEST(tikz_tree_options_are_read_at_their_level_and_the_tree_can_grow_any_way) {
  const auto wide = strokes(pic(R"([level 1/.style={sibling distance=3cm}, every node/.style={draw}]\node {r} child {node {a}} child {node {b}};)"));
  CHECK(near(centreOf(wide[2]).x - centreOf(wide[1]).x, cm(3)));
  const auto deep = strokes(pic(R"([level distance=2cm, every node/.style={draw}]\node {r} child {node {a}};)"));
  CHECK(near(centreOf(deep[1]).y - centreOf(deep[0]).y, cm(2)));
  const auto right = strokes(pic(R"([grow=right, every node/.style={draw}]\node {r} child {node {a}} child {node {b}};)"));
  CHECK(near(centreOf(right[1]).x - centreOf(right[0]).x, cm(1.5)));
  // Spread across the way it grows, the first child below (pgf's `left` of a tree growing right is down).
  CHECK(near(centreOf(right[1]).y - centreOf(right[2]).y, cm(1.5)));

  const auto miss = strokes(pic(kBoxes + R"(\node {r} child {node {a}} child[missing] child {node {c}};)"));
  CHECK_EQ(miss.size(), std::size_t(5));  // r, a, c and their two edges
  CHECK(near(centreOf(miss[2]).x - centreOf(miss[1]).x, cm(3)));
  // grow=up at its own level puts that child straight above, not among its siblings.
  const auto up = strokes(pic(kBoxes + R"(\node {r} child[grow=up] {node {u}} child {node {a}};)"));
  CHECK(near(centreOf(up[1]).x, centreOf(up[0]).x));
  CHECK(centreOf(up[1]).y < centreOf(up[0]).y);
  const auto names = strokes(pic(R"([sibling distance=6cm, every node/.style={draw}]\node (p) {r} child {node {a}} child {node {b}}; \draw (p-1) -- (p-2);)"));
  CHECK_EQ(names.size(), std::size_t(6));
  CHECK(near(extentOf(names[5]).x0, extentOf(names[1]).x1, 1.f));
  CHECK(near(extentOf(names[5]).x1, extentOf(names[2]).x0, 1.f));
}

// Nested children hang below their own parent, `child foreach` makes one per value, and `edge from parent path`
// replaces the straight edge (the org chart's `-|`).
TEST(tikz_children_have_children_and_their_edges_can_have_a_path_of_their_own) {
  const auto t = strokes(pic(kBoxes + R"(\node {r} child {node {a} child {node {a1}} child {node {a2}}} child {node {b}};)"));
  CHECK_EQ(t.size(), std::size_t(9));  // five nodes and four edges
  const std::string many = R"(\node {r} child foreach \x in {a,b,c} {node {\x}};)";
  CHECK_EQ(strokes(pic(kBoxes + many)).size(), std::size_t(7));
  const auto fork = strokes(pic(R"([every node/.style={draw}, edge from parent path={(\tikzparentnode.south) -- +(0,-8pt) -| (\tikzchildnode)}]
    \node {r} child {node {a}} child {node {b}};)"));
  CHECK_EQ(fork.size(), std::size_t(5));
  // The edge to a child that is not below its parent: down, across, down.
  CHECK(fork[3].path_segments.size() >= 3);
  // The label on an edge sits on it.
  const auto labelled = pic(kBoxes + R"(\node {r} child {node {a} edge from parent node[midway, left] {L}};)");
  CHECK_EQ(check::texts(labelled).size() >= 3, true);
}

// pgfmodulematrix: the cells' origins are on one vertical line in a column and one horizontal line in a row, a column is as
// wide as its widest cell, `column sep` and `row sep` go between the borders (or between the origins), the matrix is a node
// whose text is that box, and each cell is a node named `m-row-column`.
TEST(tikz_a_matrix_of_nodes_sets_its_cells_in_columns_and_rows) {
  const std::string head = R"([every node/.style={}]\matrix (m) [matrix of nodes, nodes={draw, inner sep=0pt}] )";
  const auto m = strokes(pic(head + R"({ a & bbbb \\ cc & d \\ };)"));
  CHECK_EQ(m.size(), std::size_t(4));  // four cells; the matrix itself is not drawn
  if (m.size() < 4) return;
  const Centre a = centreOf(m[0]), b = centreOf(m[1]), c = centreOf(m[2]), d = centreOf(m[3]);
  // The cells of a column stand on one line, those of a row on another.
  CHECK(near(a.x, c.x, 1.f));
  CHECK(near(b.x, d.x, 1.f));
  CHECK(near(a.y, b.y, 1.f));
  CHECK(near(c.y, d.y, 1.f));
  CHECK(c.y > a.y);
  // A column is as wide as its widest cell (cc, bbbb), nothing between them.
  CHECK(near(b.x - c.x, extentOf(m[2]).width() / 2 + extentOf(m[1]).width() / 2, 1.f));
  // Rows touch: the tall cells have the same height here.
  CHECK(near(c.y - a.y, extentOf(m[0]).height(), 1.f));
  const auto sep = strokes(pic(R"(\matrix (m) [matrix of nodes, nodes={draw, inner sep=0pt}, column sep=1cm, row sep=5mm] { a & bbbb \\ cc & d \\ };)"));
  CHECK_EQ(sep.size(), std::size_t(4));
  if (sep.size() < 4) return;
  CHECK(near(centreOf(sep[1]).x - centreOf(sep[0]).x - (b.x - a.x), cm(1), 1.5f));
  CHECK(near(centreOf(sep[2]).y - centreOf(sep[0]).y - (c.y - a.y), cm(0.5), 1.5f));
  // Between origins, the sep is the distance between the lines the cells stand on.
  const auto fixed = strokes(pic(R"(\matrix (m) [matrix of nodes, nodes={draw, inner sep=0pt}, column sep={2cm, between origins}] { a & bbbb \\ };)"));
  CHECK(near(centreOf(fixed[1]).x - centreOf(fixed[0]).x, cm(2), 1.f));
}

// The matrix node round its box has the node's inner sep, and is drawn behind its cells; cells are named and can be
// used, empty ones take no room and make no node unless asked.
TEST(tikz_a_matrix_is_a_node_and_its_cells_have_names) {
  const auto m = strokes(pic(R"(\matrix (m) [matrix of nodes, draw, inner sep=3pt, nodes={draw, inner sep=0pt}] { a & b \\ };)"));
  CHECK_EQ(m.size(), std::size_t(3));  // the matrix, then its cells
  if (m.size() < 3) return;
  const Extent outer = extentOf(m[0]), left = extentOf(m[1]), right = extentOf(m[2]);
  CHECK(near(left.x0 - outer.x0, static_cast<float>(plain(R"(\hspace{3pt})").width), 1.f));
  CHECK(near(outer.x1 - right.x1, static_cast<float>(plain(R"(\hspace{3pt})").width), 1.f));
  CHECK(near(outer.y1 - left.y1, static_cast<float>(plain(R"(\hspace{3pt})").width), 1.f));
  const auto used = strokes(pic(R"(\matrix (m) [matrix of nodes, row sep=2cm, nodes={draw, inner sep=0pt}] { a & b \\ c & d \\ }; \draw (m-1-2) -- (m-2-1);)"));
  CHECK_EQ(used.size(), std::size_t(5));
  CHECK(near(extentOf(used[4]).y0, extentOf(used[1]).y1, 1.f));  // out of the bottom of b ...
  CHECK(near(extentOf(used[4]).y1, extentOf(used[2]).y0, 1.f));  // ... into the top of c
  const auto gap = strokes(pic(R"(\matrix (m) [matrix of nodes, nodes={draw}] { a & \\ & d \\ };)"));
  CHECK_EQ(gap.size(), std::size_t(2));
  CHECK_EQ(strokes(pic(R"(\matrix (m) [matrix of nodes, nodes in empty cells, nodes={draw}] { a & \\ & d \\ };)")).size(), std::size_t(4));
  // `|[options]|` is a cell's own, and column and row styles come before it.
  const auto styled = pic(R"(\matrix (m) [matrix of nodes, column 1/.style={nodes={draw}}] { a & b \\ |[draw]| c & d \\ };)");
  CHECK_EQ(strokes(styled).size(), std::size_t(2));  // a and c, from column 1: c's own draw is the same one
  CHECK_EQ(strokes(pic(R"(\matrix (m) [matrix of nodes] { a & b \\ |[draw]| c & d \\ };)")).size(), std::size_t(1));
}

// `matrix of math nodes` sets each cell in math; a matrix of paths is said, once, and left out.
TEST(tikz_a_matrix_of_math_nodes_sets_its_cells_in_math) {
  const auto text = check::render(R"(\begin{tikzpicture}\matrix [matrix of nodes] { x \\ };\end{tikzpicture})", [] {
    Options o(InputMode::math);
    o.path = false;
    return o;
  }());
  const auto math = check::render(R"(\begin{tikzpicture}\matrix [matrix of math nodes] { x \\ };\end{tikzpicture})", [] {
    Options o(InputMode::math);
    o.path = false;
    return o;
  }());
  CHECK(!check::of(text, DrawRecord::TEXT).empty());
  CHECK(check::of(math, DrawRecord::TEXT).empty() && !check::of(math, DrawRecord::GLYPH).empty());
  CHECK(check::warns(pic(R"(\matrix { \node {a}; & \node {b}; \\ };)"), "matrix"));
}

// pgf's brace decoration on a straight line: a path of two bumps and a point `aspect` of the way along, `amplitude` high
// (the whole brace is that high), above the path to its left; `mirror` below it, `raise` away from it. The picture grows to
// hold it.
TEST(tikz_a_brace_decorates_a_straight_path) {
  const auto b = strokes(pic(R"(\draw[decorate, decoration={brace, amplitude=1cm}] (0,0) -- (4,0);)"));
  CHECK_EQ(b.size(), std::size_t(1));
  if (b.size() < 1) return;
  CHECK_EQ(b[0].path_segments.size(), std::size_t(7));  // move, curve, line, curve, curve, line, curve
  const Extent e = extentOf(b[0]);
  CHECK(near(e.width(), cm(4), 1.f));
  CHECK(near(e.height(), cm(1), 1.5f));  // the tip is the full amplitude; control points stay below it
  // The point is at half the way, on the side the path's left is: above, which is smaller y here.
  const auto& seg = b[0].path_segments;
  CHECK(near(seg[3].coords[4], e.x0 + e.width() / 2, 1.f));  // the end of the third segment is the tip
  CHECK(near(seg[3].coords[5], e.y0, 1.f));
  const auto mirrored = strokes(pic(R"(\draw[decorate, decoration={brace, amplitude=1cm, mirror}] (0,0) -- (4,0);)"));
  CHECK(near(mirrored[0].path_segments[3].coords[5], extentOf(mirrored[0]).y1, 1.f));
  // `raise` moves the brace along the same side the tip is on.
  const auto raised = strokes(pic(R"(\draw (0,0) -- (4,0); \draw[decorate, decoration={brace, amplitude=1cm, raise=2mm}] (0,0) -- (4,0);)"));
  CHECK_EQ(raised.size(), std::size_t(2));
  if (raised.size() < 2) return;
  CHECK(near(extentOf(raised[0]).y1 - extentOf(raised[1]).y1, cm(0.2), 1.f));  // its feet stand 2mm off the line
  // `aspect` moves the point along.
  const auto aspect = strokes(pic(R"(\draw[decorate, decoration={brace, amplitude=5mm, aspect=0.25}] (0,0) -- (4,0);)"));
  CHECK(near(aspect[0].path_segments[3].coords[4], extentOf(aspect[0]).x0 + cm(1), 1.f));
  // A decoration that is not drawn is said, once, and the path is drawn as it stands.
  const auto zig = pic(R"(\draw[decorate, decoration=zigzag] (0,0) -- (4,0);)");
  CHECK(check::warns(zig, "zigzag"));
  CHECK_EQ(strokes(zig)[0].path_segments.size(), std::size_t(2));
}

// A `\foreach` that is not `\x in {list}` is said, once, and left out.
TEST(tikz_a_foreach_that_is_not_read_warns) {
  const auto l = pic(R"(\foreach \x {1,2} \draw (\x,0) -- (\x,1);)");
  CHECK(check::warns(l, "\\foreach"));
}

// pgflibraryshapes.geometric: a regular polygon has a corner up when its sides are odd, and half a step round when they are
// even (a hexagon's are at 60, 120 ... 360 degrees), so a hexagon's east anchor is its sixth corner and a pentagon's north
// its first; `corner n` and `side n` are the anchors of the polygon the outer separation makes.
TEST(tikz_a_regular_polygon_has_its_corners_where_pgf_puts_them) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  const std::string hex = R"(\node[regular polygon, regular polygon sides=6, draw] (a) {x};)";
  const std::string pent = R"(\node[regular polygon, draw] (a) {x};)";
  CHECK_PAIRS((check::Pairs{
    {tikz(hex + R"(\draw (a.east) -- ++(1,0);)"), tikz(hex + R"(\draw (a.corner 6) -- ++(1,0);)")},
    {tikz(hex + R"(\draw (a.west) -- ++(1,0);)"), tikz(hex + R"(\draw (a.corner 3) -- ++(1,0);)")},
    {tikz(pent + R"(\draw (a.north) -- ++(1,0);)"), tikz(pent + R"(\draw (a.corner 1) -- ++(1,0);)")},
    // A side's anchor is half way between two corners.
    {tikz(pent + R"(\draw (a.side 1) -- ++(1,0);)"), tikz(pent + R"(\draw ($(a.corner 1)!0.5!(a.corner 2)$) -- ++(1,0);)")},
  }));
  // `shape border rotate` turns the corners: a quarter turn of a square is the same square, drawn from another corner.
  const Extent square = extentOf(strokes(pic(R"(\node[regular polygon, regular polygon sides=4, draw] {x};)"))[0]);
  const Extent turned = extentOf(strokes(pic(R"(\node[regular polygon, regular polygon sides=4, shape border rotate=90, draw] {x};)"))[0]);
  CHECK(near(square.width(), turned.width(), 0.5f) && near(square.height(), turned.height(), 0.5f));
  CHECK(near(square.width(), square.height(), 0.5f));
  const auto n = strokes(pic(hex));
  CHECK_EQ(n.size(), std::size_t(1));
  CHECK_EQ(n[0].path_segments.size(), std::size_t(7));  // a move, five lines, a close
  // The size follows the text: the polygon is round the text's incircle, so a longer text makes it wider, a minimum size
  // makes it at least that big.
  const auto width = [](const std::string& options, const std::string& text) {
    return extentOf(strokes(pic("\\node[" + options + ", draw] {" + text + "};"))[0]).width();
  };
  CHECK(width("regular polygon sides=6", "xxxxxxxx") > width("regular polygon sides=6", "x"));
  CHECK(width("regular polygon sides=6, minimum width=3cm", "x") >= 0.99f * static_cast<float>(plain("\\hspace{3cm}").width));
}

// star: `star points` outer corners at the incircle times `star point ratio` (1.5), the inner ones between them; the anchors
// are the outer and inner points of the border.
TEST(tikz_a_star_has_outer_and_inner_points) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  const std::string star = R"(\node[star, draw] (a) {x};)";
  CHECK_PAIRS((check::Pairs{
    {tikz(star + R"(\draw (a.north) -- ++(1,0);)"), tikz(star + R"(\draw (a.outer point 1) -- ++(1,0);)")},
    {tikz(star + R"(\draw (a.inner point 5) -- ++(1,0);)"), tikz(star + R"(\draw (a.inner point 5) -- ++(1,0);)")},
  }));
  CHECK_EQ(strokes(pic(star))[0].path_segments.size(), std::size_t(11));  // ten corners and the close
  CHECK_EQ(strokes(pic(R"(\node[star, star points=7, draw] {x};)"))[0].path_segments.size(), std::size_t(15));
  // A taller point (`star point height`) makes the star bigger than the default ratio's.
  const auto tall = [](const std::string& options) {
    return extentOf(strokes(pic("\\node[star, " + options + ", draw] {x};"))[0]).height();
  };
  CHECK(tall("star point height=1cm") > tall("star point ratio=1.2"));
}

// isosceles triangle: the apex points right, and `shape border rotate=90` turns it up; its apex, corners and sides are anchors.
TEST(tikz_an_isosceles_triangle_points_right_and_has_its_corners_as_anchors) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  const std::string tri = R"(\node[isosceles triangle, draw] (a) {x};)";
  const std::string up = R"(\node[isosceles triangle, shape border rotate=90, draw] (a) {x};)";
  CHECK_PAIRS((check::Pairs{
    {tikz(tri + R"(\draw (a.apex) -- ++(1,0);)"), tikz(tri + R"(\draw (a.east) -- ++(1,0);)")},
    {tikz(up + R"(\draw (a.apex) -- ++(1,0);)"), tikz(up + R"(\draw (a.north) -- ++(1,0);)")},
    {tikz(tri + R"(\draw (a.lower side) -- ++(1,0);)"), tikz(tri + R"(\draw ($(a.left corner)!0.5!(a.right corner)$) -- ++(1,0);)")},
  }));
  CHECK_EQ(strokes(pic(tri))[0].path_segments.size(), std::size_t(4));
  // A wider apex angle makes a shorter, broader triangle.
  const auto extent = [](const std::string& options, bool horizontal) {
    const Extent e = extentOf(strokes(pic("\\node[isosceles triangle, " + options + ", draw] {x};"))[0]);
    return horizontal ? e.width() : e.height();
  };
  CHECK(extent("isosceles triangle apex angle=90", true) < extent("isosceles triangle apex angle=30", true));
  CHECK(extent("isosceles triangle apex angle=90", false) > extent("isosceles triangle apex angle=30", false));
  // pgf's default is the smallest triangle round the text box; `shape border uses incircle` takes the circle round it, which
  // is bigger (pdfTeX, `h09-triangle`: 40pt long for `tri` against 57pt).
  CHECK(extent("", true) < extent("shape border uses incircle", true));
}

// `\clip`: what is drawn after it, in the same scope, stays inside its path -- pgf's own rule -- and the clip path's own box and
// what is drawn inside it make the picture's size (pdfTeX: a clip of 2cm x 1cm round a long line is 56.7 x 28.3pt; a clip of
// 4cm x 3cm round a short one 113.4 x 85.0pt).
TEST(tikz_a_clip_path_keeps_what_is_drawn_after_it_inside) {
  const auto clipped = pic(R"(\clip (0,0) rectangle (2,1); \draw (-3,-3) -- (6,4);)");
  CHECK(clipped.warnings.empty());
  CHECK(near(static_cast<float>(clipped.width), cm(2), 1.f));
  CHECK(near(static_cast<float>(clipped.height), cm(1), 1.f));
  const auto line = strokes(clipped);
  CHECK_EQ(line.size(), std::size_t(1));
  if (line.size() == 1) {
    // The line of slope 7/9 through (-3,-3) enters the box at (6/7, 0) and leaves it through the right side at (2, 8/9).
    const Extent e = extentOf(line[0]);
    CHECK(near(e.width(), cm(8.0 / 7.0), 1.5f));
    CHECK(near(e.height(), cm(8.0 / 9.0), 1.5f));
  }
  // The clip path draws nothing of its own, and one larger than what is drawn is the picture's size.
  const auto roomy = pic(R"(\clip (0,0) rectangle (4,3); \draw (0,0) -- (1,1);)");
  CHECK(near(static_cast<float>(roomy.width), cm(4), 1.f));
  CHECK(near(static_cast<float>(roomy.height), cm(3), 1.f));
  CHECK_EQ(strokes(roomy).size(), std::size_t(1));
  // A scope ends it.
  const auto scoped = pic(R"(\begin{scope}\clip (0,0) rectangle (1,1);\end{scope} \draw (-1,0) -- (2,0);)");
  CHECK(near(extentOf(strokes(scoped)[0]).width(), cm(3), 1.f));
  // Two clips are the intersection of their paths.
  const auto both = pic(R"(\begin{scope}\clip (0,0) rectangle (3,3); \clip (1,1) rectangle (4,4); \fill (-1,-1) rectangle (5,5);\end{scope})");
  const Extent f = extentOf(fills(both)[0]);
  CHECK(near(f.width(), cm(2), 1.f) && near(f.height(), cm(2), 1.f));
}

// A circle clips to the lens of the Venn diagram (two unit circles with their centres 1cm apart: 1cm wide, 1.732cm high), a
// filled shape is cut to the polygon inside and its own stroke is not drawn along the clip's edge; `\path[clip]` and `clip` as
// an option are `\clip`.
TEST(tikz_a_circle_clip_cuts_a_fill_to_a_lens_and_a_stroke_to_pieces) {
  const auto lens = pic(R"(\begin{scope}\clip (0,0) circle (1); \fill (1,0) circle (1);\end{scope})");
  const Extent e = extentOf(fills(lens)[0]);
  CHECK(near(e.width(), cm(1), 1.5f));
  CHECK(near(e.height(), cm(1.7320508), 1.5f));
  const auto option = pic(R"(\begin{scope}\path[clip] (0,0) circle (1); \fill (1,0) circle (1);\end{scope})");
  CHECK(near(extentOf(fills(option)[0]).width(), e.width(), 0.5f));
  // A filled and stroked rectangle that crosses the clip's edge: the fill stops at the edge, the stroke is open pieces there.
  const auto both = pic(R"(\begin{scope}\clip (0,0) rectangle (2,2); \filldraw[fill=red] (-1,0.5) rectangle (1,1.5);\end{scope})");
  CHECK_EQ(fills(both).size(), std::size_t(1));
  const auto outline = strokes(both);
  CHECK_EQ(outline.size(), std::size_t(1));
  if (outline.size() == 1) {
    // Three sides of the rectangle remain (the left one is outside), as one polyline: top, right, bottom.
    CHECK_EQ(outline[0].path_segments.size(), std::size_t(4));
    CHECK(near(extentOf(outline[0]).width(), cm(1), 1.f));
  }
  // `\draw[clip]` draws, and clips what follows.
  const auto drawn = pic(R"(\begin{scope}\draw[clip] (0,0) rectangle (1,1); \draw (-1,0.5) -- (3,0.5);\end{scope})");
  CHECK_EQ(strokes(drawn).size(), std::size_t(2));
  // A clip path whose literal sides are not convex is said and not used (a concave region cannot be cut to here): the line stays
  // whole; a convex polygon, whatever it is written with, is clipped to without a word.
  const auto concave = pic(R"(\clip (0,0) -- (4,0) -- (4,4) -- (2,1) -- (0,4) -- cycle; \draw (-1,2) -- (5,2);)");
  CHECK(check::warns(concave, "not convex"));
  CHECK(near(extentOf(strokes(concave)[0]).width(), cm(6), 1.f));
  const auto convex = pic(R"(\clip (0,0) -- (2,0) -- (3,1) -- (2,2) -- (0,2) -- cycle; \draw (-1,1) -- (5,1);)");
  CHECK(!check::warns(convex, "convex"));
  CHECK(near(extentOf(strokes(convex)[0]).width(), cm(3), 1.f));
  // Nothing outside the clip is left; a clip of curves is said and not used.
  CHECK(check::warns(pic(R"(\clip (0,0) .. controls (1,1) and (2,1) .. (3,0); \draw (0,0) -- (3,0);)"), "clip path of curves"));
}

// `plot (\x, {f(\x)})` is the path through `samples` points of `f` evenly over `domain`; at the start of a path it moves to the
// first one (pgf's `\pgfsetmovetofirstplotpoint`), after `--` it draws a line to it (`\pgfsetlinetofirstplotpoint`).
TEST(tikz_a_plot_is_the_polyline_through_its_points) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\draw plot[domain=0:2, samples=3] (\x, {\x*\x});)"), tikz(R"(\draw (0,0) -- (1,1) -- (2,4);)")},
    {tikz(R"(\draw[domain=0:2, samples=3] plot (\x, {\x*\x});)"), tikz(R"(\draw (0,0) -- (1,1) -- (2,4);)")},
    {tikz(R"(\draw plot coordinates {(0,0) (1,1) (2,4)};)"), tikz(R"(\draw (0,0) -- (1,1) -- (2,4);)")},
    {tikz(R"(\draw (0,0) -- plot coordinates {(1,1) (2,0)};)"), tikz(R"(\draw (0,0) -- (1,1) -- (2,0);)")},
    // Without `--` the first point is a move: two subpaths.
    {tikz(R"(\draw (0,0) plot coordinates {(1,1) (2,0)};)"), tikz(R"(\draw (0,0) (1,1) -- (2,0);)")},
    // `variable`, units, and the radians suffix: pgfmath's functions take degrees unless the value ends in `r`.
    {tikz(R"(\draw plot[variable=\t, domain=0:1, samples=2] ({\t*10mm}, {\t});)"), tikz(R"(\draw (0,0) -- (1,1);)")},
    {tikz(R"(\draw plot[domain=0:1.5, samples=4] (\x, {sin(\x r)});)"),
     tikz(R"(\draw plot[domain=0:1.5, samples=4] (\x, {sin(\x*180/pi)});)")},
    {tikz(R"(\draw plot[samples at={0,1,2}] (\x, {2*\x});)"), tikz(R"(\draw (0,0) -- (1,2) -- (2,4);)")},
    {tikz(R"(\draw plot[samples at={0,0.5,...,2}] (\x, {\x});)"), tikz(R"(\draw plot[samples=5, domain=0:2] (\x, {\x});)")},
    // A step is a horizontal run to the next x and then a vertical one; a comb stands on the axis.
    {tikz(R"(\draw plot[const plot] coordinates {(0,1) (1,2) (2,0)};)"),
     tikz(R"(\draw (0,1) -- (1,1) -- (1,2) -- (2,2) -- (2,0);)")},
    {tikz(R"(\draw plot[ycomb] coordinates {(0,1) (1,2)};)"), tikz(R"(\draw (0,0) -- (0,1) (1,0) -- (1,2);)")},
    {tikz(R"(\draw plot[xcomb] coordinates {(1,0) (2,1)};)"), tikz(R"(\draw (0,0) -- (1,0) (0,1) -- (2,1);)")},
    {tikz(R"(\draw plot[sharp cycle] coordinates {(0,0) (1,0) (1,1)};)"), tikz(R"(\draw (0,0) -- (1,0) -- (1,1) -- cycle;)")},
  }));
}

// pgf's smooth curve: the controls on either side of a point are along the line through its neighbours, 0.2775 * tension of
// the way (tension 0.5), and the ends have their own point as the outer control. Through (0,0) (1,1) (2,0): the controls
// at the middle are (1 -+ 0.2775, 1), the first curve starts with its own point as control, the last ends in its own.
TEST(tikz_a_smooth_plot_passes_through_its_points_with_pgfs_controls) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\draw plot[smooth] coordinates {(0,0) (1,1) (2,0)};)"),
     tikz(R"(\draw (0,0) .. controls (0,0) and (0.7225,1) .. (1,1) .. controls (1.2775,1) and (2,0) .. (2,0);)")},
    // A tension of 1 doubles the reach; two points are a straight curve.
    {tikz(R"(\draw plot[smooth, tension=1] coordinates {(0,0) (1,1) (2,0)};)"),
     tikz(R"(\draw (0,0) .. controls (0,0) and (0.445,1) .. (1,1) .. controls (1.555,1) and (2,0) .. (2,0);)")},
    {tikz(R"(\draw plot[smooth] coordinates {(0,0) (3,0)};)"), tikz(R"(\draw (0,0) .. controls (0,0) and (3,0) .. (3,0);)")},
  }));
  const auto l = pic(R"(\draw plot[smooth] coordinates {(0,0) (1,1) (2,0) (3,1)};)");
  CHECK(l.warnings.empty());
  CHECK_EQ(strokes(l)[0].path_segments.size(), std::size_t(4));  // a move and three curves
}

// A point that is not a number (1/0) is left out with one warning; the rest of the plot is drawn.
TEST(tikz_a_plot_leaves_out_a_point_that_is_not_a_number_and_says_so) {
  const auto l = pic(R"(\draw plot[domain=-1:1, samples=3] (\x, {1/\x});)");
  CHECK(check::warns(l, "a point of a plot is not a number"));
  CHECK_EQ(strokes(l)[0].path_segments.size(), std::size_t(2));
  // `samples=1` is two, a one-point domain is its two ends, no coordinates is no path, and gnuplot's plot is said.
  CHECK_EQ(strokes(pic(R"(\draw plot[samples=1, domain=0:1] (\x, \x);)"))[0].path_segments.size(), std::size_t(2));
  CHECK_EQ(strokes(pic(R"(\draw plot[domain=1:1] (\x, \x);)"))[0].path_segments.size(), std::size_t(2));
  CHECK(strokes(pic(R"(\draw plot coordinates {};)")).empty());
  CHECK(check::warns(pic(R"(\draw plot function{x*x};)"), "plot function"));
}

// A picture holds a node's outline, and half the line when it is drawn -- not its outer separation (pdfTeX, one `x`, 10pt:
// `\node {x}` 11.899bp wide, `[draw]` 12.298, `[draw, outer sep=5pt]` 12.298, `[fill=red]` 11.899, `[draw, line width=2pt]`
// 13.892; a star `[draw, outer sep=3pt]` 24.405 against 24.007 without the line).
TEST(tikz_a_picture_holds_the_outline_of_a_node_and_not_its_outer_sep) {
  const auto width = [](const std::string& options) {
    return static_cast<float>(pic("\\node[" + options + "] {x};").width);
  };
  // (A layout's width is whole pixels, so the lines are made wide enough to show.)
  const float plainWidth = width("");
  CHECK(near(width("draw, line width=10pt") - plainWidth, static_cast<float>(plain("\\hspace{10pt}").width), 1.5f));
  CHECK(near(width("draw, line width=10pt, outer sep=30pt"), width("draw, line width=10pt"), 1.5f));
  CHECK(near(width("fill=red, outer sep=30pt"), plainWidth, 1.5f));
  for (const char* shape : {"circle", "star", "regular polygon", "isosceles triangle"}) {
    const std::string s = shape;
    CHECK(near(width(s + ", draw, outer sep=30pt"), width(s + ", draw"), 1.5f));
  }
}

// Found probing item 16: a count that is a billion was a loop of a billion (a hang), and a size that is not a number or does
// not fit escaped to the host as an R error. Counts are capped and said; a picture of no size is no picture.
TEST(tikz_counts_are_capped_and_a_picture_always_has_a_size) {
  const auto cap = [](const std::string& body) { return pic(body); };
  CHECK_EQ(strokes(cap(R"(\node[regular polygon, regular polygon sides=1000000000, draw] {x};)"))[0].path_segments.size(),
           std::size_t(1001));   // a move, 999 lines, the close
  CHECK_EQ(strokes(cap(R"(\draw plot[samples=1000000000, domain=0:1000] (\x, 0);)"))[0].path_segments.size(), std::size_t(20000));
  const auto steps = cap(R"(\draw plot[samples at={0,0.000001,...,10}] (\x, \x);)");
  CHECK(check::warns(steps, "samples at has more than 20000 values"));
  CHECK_EQ(strokes(steps)[0].path_segments.size(), std::size_t(20000));
  // An arc of nothing or of a full circle has no size; a coordinate of a hundred metres has no use.
  CHECK(check::warns(cap(R"(\node[draw, rounded rectangle, rounded rectangle arc length=360] {T};)"), "is not between 0 and 360"));
  CHECK(check::warns(cap(R"(\node[draw, rounded rectangle, rounded rectangle arc length=0] {T};)"), "is not between 0 and 360"));
  const auto far = cap(R"(\draw (0,0) -- (1e10,1); \draw (0,0) -- (1,1);)");
  CHECK(check::warns(far, "more than 100 metres"));
  CHECK(far.width > 0 && far.width < 1000000);
  CHECK(check::warns(cap(R"(\draw plot[domain=0:1000, samples=5] (\x, {exp(\x)});)"), "not a number"));
}

// plot's expression: a unit after the variable's number, polar points, and the square root of a negative number (not a number,
// as 1/0 is) -- found probing item 16.
TEST(tikz_a_plot_point_may_have_units_and_polar_form) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\draw plot[domain=0:2, samples=3] (\x cm, 0);)"), tikz(R"(\draw (0,0) -- (1,0) -- (2,0);)")},
    {tikz(R"(\draw plot[domain=0:20, samples=3] (\x mm, {\x mm});)"), tikz(R"(\draw (0,0) -- (1,1) -- (2,2);)")},
    {tikz(R"(\draw plot[domain=0:2, samples=3] (\x:{1});)"), tikz(R"(\draw (0:1) -- (1:1) -- (2:1);)")},
    {tikz(R"(\draw plot[domain=0:90, samples=3] (\x:{\x/45});)"), tikz(R"(\draw (0:0) -- (45:1) -- (90:2);)")}}));
  const auto root = pic(R"(\draw plot[domain=-1:1, samples=3] (\x, {sqrt(\x)});)");
  CHECK(check::warns(root, "not a number"));
  CHECK_EQ(strokes(root)[0].path_segments.size(), std::size_t(2));
}

// A clip path the layout cannot clip to is said once, for the reason it is not: curves, a corner that turns the wrong way, or
// several parts (TikZ clips to their union; only the first is used here).
TEST(tikz_a_clip_path_that_is_not_one_region_is_said_for_its_own_reason) {
  const auto concave = pic(R"(\clip (0,0) -- (4,0) -- (4,4) -- (2,1) -- (0,4) -- cycle; \draw (-1,2) -- (5,2);)");
  CHECK(check::warns(concave, "not convex"));
  CHECK(!check::warns(concave, "curves"));
  CHECK(check::warns(pic(R"(\clip (0,0) .. controls (1,1) and (2,1) .. (3,0); \draw (0,0) -- (3,0);)"), "curves"));
  const auto parts = pic(R"(\clip (0,0) rectangle (1,1) (2,0) rectangle (3,1); \fill (-1,-1) rectangle (4,2);)");
  CHECK(check::warns(parts, "several parts"));
  CHECK(!check::warns(pic(R"(\clip (0,0) rectangle (1,1); \fill (-1,-1) rectangle (4,2);)"), "several parts"));
}

// ---- found by an independent probing pass of the TikZ support (2026-10-17), compared with pdfTeX ----

// `\tikzset` before a picture with options dropped the whole picture: the parser put the styles in front of the picture's text,
// before `[scale=2]`, which then was no option list ("is not a statement"). pdflatex draws the node.
TEST(tikz_a_tikzset_before_a_picture_with_options_does_not_drop_it) {
  const auto l = check::render(R"(\tikzset{mystyle/.style={draw,red}}\begin{tikzpicture}[scale=2]\node[mystyle] at (1,1) {x};\end{tikzpicture})");
  CHECK(l.warnings.empty());
  CHECK_EQ(strokes(l).size(), std::size_t(1));
  // The options may use the style, too.
  const auto used = check::render(R"(\tikzset{boxy/.style={draw}}\begin{tikzpicture}[boxy]\node {x};\end{tikzpicture})");
  CHECK(used.warnings.empty());
  CHECK(!check::render(R"(\tikzstyle{old}=[draw]\begin{tikzpicture}[node distance=2cm]\node[old] {x};\end{tikzpicture})").records.empty());
}

// A node's own `scale` scales the node and not where it is put (pdfTeX, one x: `scale=2` 24.197bp wide, the line once;
// `minimum width` is scaled too), and `at (2,1)` stays at (2cm,1cm). It was a step of the coordinates: the node moved to (4,2).
TEST(tikz_a_nodes_own_scale_scales_the_node_and_not_its_place) {
  const auto width = [](const std::string& options) {
    return extentOf(strokes(pic("\\node[draw" + options + "] {x};"))[0]).width();
  };
  const float one = width(""), twice = width(", scale=2");
  CHECK(near(twice, 2 * one, 2.f));
  CHECK(near(width(", xscale=2"), twice, 2.f));
  CHECK(near(width(", scale=2, minimum width=8cm"), 2 * width(", minimum width=8cm"), 3.f));
  // Where it stands: 2cm right of and 1cm above the origin, which a short line marks.
  const auto at = pic(R"(\node[draw, scale=2] at (2,1) {x}; \draw (0,0) -- (0.01,0);)");
  const auto marks = strokes(at);
  CHECK_EQ(marks.size(), std::size_t(2));
  const Extent node = extentOf(marks[0]), origin = extentOf(marks[1]);
  CHECK(near(0.5f * (node.x0 + node.x1) - origin.x0, cm(2), 2.f));
  CHECK(near(std::fabs(0.5f * (node.y0 + node.y1) - origin.y0), cm(1), 2.f));
  // Rotation is said, and the node stays where it is put.
  CHECK(check::warns(pic(R"(\node[draw, rotate=30] at (3,1) {rot};)"), "rotating a node is not supported"));
}

// `(a |- b)` is the x of a and the y of b; `(a -| b)` the x of b and the y of a (it was "not a node", and a path left out).
TEST(tikz_a_corner_coordinate_takes_the_x_of_one_and_the_y_of_the_other) {
  const auto tikz = [](const std::string& body) {
    return "\\begin{tikzpicture}\\node (a) at (1,2) {a}; \\node (b) at (3,0) {b};" + body + "\\end{tikzpicture}";
  };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\draw (a |- b) -- (b);)"), tikz(R"(\draw (1,0) -- (b);)")},
    {tikz(R"(\draw (a -| b) -- (b);)"), tikz(R"(\draw (3,2) -- (b);)")},
    // `|-` as a step of a path is vertical first: its corner is the x of a.south and the y of b.north.
    {tikz(R"(\draw (a.south) -- (a.south |- b.north) -- (b.north);)"), tikz(R"(\draw (a.south) |- (b.north);)")},
  }));
  CHECK(!check::warns(check::render(tikz(R"(\draw (a |- b) -- (b);)")), "not a node"));
}

// A node written on a rectangle is at its centre (`rectangle node{x} (3,1)`, `rectangle (3,1) node[midway]{x}`), and one written
// between `..` and the end of a curve is half way along it (pdfTeX); both were dropped or put at a corner.
TEST(tikz_a_node_on_a_rectangle_or_a_curve_is_half_way_along) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\draw (0,0) rectangle node{m} (3,1);)"), tikz(R"(\draw (0,0) rectangle (3,1); \node at (1.5,0.5) {m};)")},
    {tikz(R"(\draw (0,0) rectangle (3,1) node[midway]{m};)"), tikz(R"(\draw (0,0) rectangle (3,1); \node at (1.5,0.5) {m};)")},
    // The middle of a cubic from (0,0) to (4,0) with controls (1,1) and (3,1) is (2,0.75).
    {tikz(R"(\draw (0,0) .. controls (1,1) and (3,1) .. node[above]{z} (4,0);)"),
     tikz(R"(\draw (0,0) .. controls (1,1) and (3,1) .. (4,0); \node[above] at (2,0.75) {z};)")},
  }));
}

// A matrix in a shifted scope is shifted (it stood at the origin), and the names of its cells with it.
TEST(tikz_a_matrix_in_a_shifted_scope_is_shifted) {
  // The picture's box starts at the matrix either way, so the cells keep their place in it; what moves is the origin, which
  // a line from (0,0) marks: it is 2cm left of the middle of the two cells.
  const auto marked = pic(R"(\begin{scope}[xshift=2cm,yshift=1cm]\matrix (m) [matrix of nodes] {a & b \\};\end{scope}\draw (0,0) -- (0.01,0);)");
  const auto line = strokes(marked);
  CHECK_EQ(line.size(), std::size_t(1));
  const auto cells = check::of(marked, DrawRecord::TEXT);
  CHECK_EQ(cells.size(), std::size_t(2));
  const float centre = 0.5f * (cells[0].x + cells[1].x) + 0.5f * static_cast<float>(plain("a").width);
  CHECK(near(centre - extentOf(line[0]).x0, cm(2), 3.f));
}

// Styles a scope defines end with it: `every node/.style` in its options was still there after it.
TEST(tikz_the_styles_of_a_scope_end_with_it) {
  CHECK_EQ(strokes(pic(R"(\begin{scope}[every node/.style={draw}]\node {A};\end{scope}\node at (3,0) {B};)")).size(), std::size_t(1));
  // (A style defined outside the scope is in force inside it, and after it.)
  CHECK_EQ(strokes(pic(R"(\tikzset{boxed/.style={draw}}\begin{scope}\node[boxed] {A};\end{scope}\node[boxed] at (3,0) {B};)")).size(),
           std::size_t(2));
}

// `shift={(x,y)}` (the most used of the keys that were unsupported) moves what follows, in a picture, a scope, a path or a node.
TEST(tikz_shift_with_a_point_moves_what_follows) {
  const auto tikz = [](const std::string& body) { return "\\begin{tikzpicture}" + body + "\\end{tikzpicture}"; };
  CHECK_PAIRS((check::Pairs{
    {tikz(R"(\begin{scope}[shift={(2,1)}]\draw (0,0) -- (1,0);\end{scope})"), tikz(R"(\draw (2,1) -- (3,1);)")},
    {tikz(R"(\draw[shift={(1,2)}] (0,0) -- (1,0);)"), tikz(R"(\draw (1,2) -- (2,2);)")},
    {tikz(R"(\node[shift={(1,-1)}, draw] {x};)"), tikz(R"(\node[draw] at (1,-1) {x};)")},
  }));
  CHECK(!check::warns(check::render(tikz(R"(\draw[shift={(1,2)}] (0,0) -- (1,0);)")), "shift"));
}

// An edge from the parent has its nodes half way along it unless `pos` says otherwise (pdfTeX); any other path puts a node
// after its last point at that point.
TEST(tikz_a_node_on_an_edge_from_the_parent_is_half_way) {
  const auto tikz = [](const std::string& edge) {
    return "\\begin{tikzpicture}\\node {p} child {node {a} edge from parent " + edge + "};\\end{tikzpicture}";
  };
  CHECK_PAIRS((check::Pairs{{tikz("node[left]{x}"), tikz("node[left, pos=0.5]{x}")}}));
  CHECK(!check::sameInk(check::render(tikz("node[left]{x}")), check::render(tikz("node[left, pos=1]{x}"))));
}

// What a picture holds: a stroked path's line, half its width each side, and an arrow tip (pdfTeX: `line width=8pt` on a
// 1cm line is 36.34 x 7.77bp); a negative width is said, not an R error at draw time.
TEST(tikz_a_picture_holds_the_line_of_a_path_and_a_negative_width_is_said) {
  const auto l = pic(R"(\draw[line width=10pt] (0,0) -- (1,0);)");
  CHECK(near(static_cast<float>(l.width), cm(1) + static_cast<float>(plain("\\hspace{10pt}").width), 2.f));
  CHECK(near(static_cast<float>(l.height), static_cast<float>(plain("\\hspace{10pt}").width), 2.f));
  const auto neg = pic(R"(\draw[line width=-2pt] (0,1) -- (1,2);)");
  CHECK(check::warns(neg, "negative"));
  CHECK(strokes(neg)[0].line_width >= 0.f);
}

// An arc of a million degrees, a dash pattern of a thousandth of a point over ten centimetres and ten thousand circles are
// not a minute's work: they are bounded (found by probing: each was more than 40 seconds).
TEST(tikz_runaway_arcs_and_dash_patterns_are_bounded) {
  CHECK(!strokes(pic(R"(\draw (0,0) arc (0:36000000:1);)")).empty());
  const auto fine = pic(R"(\draw[dash pattern=on 0.001pt off 0.001pt] (0,0) -- (10,0);)");
  CHECK_EQ(strokes(fine).size(), std::size_t(1));
}

// Found by the fuzzer on the first CI run: an option bracket (or brace) left open at the end of a picture made the reader
// of trees read the same `[` for ever, because the helper that cuts a group out of the text said "none" and did not move
// on. A group left open now runs to the end of the text, as TeX reads one, and every one of these returns.
TEST(tikz_a_bracket_or_brace_left_open_at_the_end_of_a_picture_does_not_hang) {
  for (const char* tail : {"\\node[", "\\node[draw", "\\draw[", "\\path (0,0) node[", "\\node at (0,0) [", "\\matrix [",
                           "\\foreach \\i in {1,2} \\node[", "\\node {x} child[", "\\node {x} child {node[", "\\node{x",
                           "\\node {x} child {", "\\coordinate[", "\\draw (0,0) -- (1,1) node[", "\\scoped{",
                           "\\begin{scope}[", "\\draw plot coordinates {", "\\node {x} edge from parent[",
                           "\\draw[decorate, decoration={brace, amplitude=", "\\tikzset{a/.style={"}) {
    const auto l = check::render(std::string("\\begin{tikzpicture}") + tail);
    CHECK(l.width >= 0);
    const auto closed = check::render(std::string("\\begin{tikzpicture}") + tail + "\\end{tikzpicture}");
    CHECK(closed.width >= 0);
  }
}

TEST(tikz_a_rectangle_in_a_turned_scope_is_turned_and_scalebox_with_two_factors_draws_outlines) {
  // pdfTeX: `rotate=30` in a scope turns the square with it, the picture holds the turned square (38.3 by 38.3 for 1cm
  // at 28.45 pt); it was the box of the diagonal, 10 by 39.
  const auto turned = pic(R"(\begin{scope}[rotate=30]\draw (0,0) rectangle (1,1);\end{scope})");
  CHECK(near(static_cast<float>(turned.width), static_cast<float>(turned.height), 3.f));
  const auto straight = pic(R"(\draw (0,0) rectangle (1,1);)");
  CHECK(turned.width > straight.width + 5);
  // A scale that differs both ways cannot be a glyph's: it is an outline.
  Options opt(InputMode::math);
  opt.path = false;
  opt.textSize = 20;
  const auto both = check::render(R"(\scalebox{2}[1]{x})", opt);
  CHECK_EQ(check::of(both, DrawRecord::GLYPH).size(), std::size_t(0));
  CHECK(check::of(both, DrawRecord::PATH).size() > 0);
  CHECK_EQ(check::of(check::render(R"(\scalebox{2}{x})", opt), DrawRecord::GLYPH).size(), std::size_t(1));
}
