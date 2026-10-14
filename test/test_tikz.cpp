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

// At a size of 100 a centimetre is some 28 px: lengths compare to a pixel.
check::Layout pic(const std::string& body, InputMode mode = InputMode::math) {
  Options opt(mode);
  opt.path = false;
  opt.textSize = 100;
  return check::render("\\begin{tikzpicture}" + body + "\\end{tikzpicture}", opt);
}

check::Layout plain(const std::string& tex) {
  Options opt(InputMode::math);
  opt.path = false;
  opt.textSize = 100;
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

// A `\foreach` that is not `\x in {list}` is said, once, and left out.
TEST(tikz_a_foreach_that_is_not_read_warns) {
  const auto l = pic(R"(\foreach \x {1,2} \draw (\x,0) -- (\x,1);)");
  CHECK(check::warns(l, "\\foreach"));
}
