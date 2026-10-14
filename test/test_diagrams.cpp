// Commutative diagrams: amscd's CD and tikz-cd's tikzcd, drawn as cells in a
// grid with arrows between them. The records say where everything is: the
// cells are glyphs, an arrow is line records (its heads, tails and dashes
// included), and a label is a glyph too (engine.h). Sizes are those of a
// ten-point text.

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "engine.h"

using check::Options;
using check::Records;
using check::Strings;
using microtex::DrawRecord;
using microtex::InputMode;

namespace {

check::Layout layout(const std::string& tex, InputMode mode = InputMode::math) {
  Options opt(mode);
  opt.path = false;
  opt.textSize = 10;
  return check::render(tex, opt);
}

Records rec(const std::string& tex, InputMode mode = InputMode::math) { return check::ink(layout(tex, mode)); }

bool quiet(const std::string& tex, InputMode mode = InputMode::math) { return layout(tex, mode).warnings.empty(); }

bool warnsAbout(const std::string& tex, const std::string& part, InputMode mode = InputMode::math) {
  return check::warns(layout(tex, mode), part);
}

std::string tikz(const std::string& body, const std::string& opts = "") {
  return "\\begin{tikzcd}" + opts + "\n" + body + "\n\\end{tikzcd}";
}

std::string cd(const std::string& body) { return "\\begin{CD}\n" + body + "\n\\end{CD}"; }

float len(const DrawRecord& l) { return std::hypot(l.x2 - l.x, l.y2 - l.y); }

std::vector<float> lens(const Records& rs) {
  std::vector<float> out;
  for (const auto& r : rs) out.push_back(len(r));
  return out;
}

Records onlyLines(const Records& r) {
  Records out;
  for (const auto& x : r) {
    if (x.type == DrawRecord::LINE) out.push_back(x);
  }
  return out;
}

// The line records long enough to be a shaft, not a head or a hook.
Records shafts(const Records& r, float min = 12) {
  Records out;
  for (const auto& l : onlyLines(r)) {
    if (len(l) >= min) out.push_back(l);
  }
  return out;
}

Records horizontal(const Records& lines) {
  Records out;
  for (const auto& l : lines) {
    if (std::fabs(l.y - l.y2) < 1e-3f) out.push_back(l);
  }
  return out;
}

Records vertical(const Records& lines) {
  Records out;
  for (const auto& l : lines) {
    if (std::fabs(l.x - l.x2) < 1e-3f) out.push_back(l);
  }
  return out;
}

// The glyphs, by their baseline origins: top to bottom, then left to right.
Records glyphs(const Records& r) {
  Records g;
  for (const auto& x : r) {
    if (x.type == DrawRecord::GLYPH) g.push_back(x);
  }
  std::stable_sort(g.begin(), g.end(), [](const auto& a, const auto& b) {
    const float ra = std::round(a.y), rb = std::round(b.y);
    return ra != rb ? ra < rb : a.x < b.x;
  });
  return g;
}

// The glyphs between the first and the last in x: a label.
Records middle(const Records& g) {
  float lo = 1e9f, hi = -1e9f;
  for (const auto& x : g) {
    lo = std::min(lo, x.x);
    hi = std::max(hi, x.x);
  }
  Records out;
  for (const auto& x : g) {
    if (x.x > lo + 5 && x.x < hi - 5) out.push_back(x);
  }
  return out;
}

bool near(float a, float b, float relative = 1e-3f) { return std::fabs(a - b) <= relative * std::max(std::fabs(b), 1e-6f) + 1e-6f; }

float minX(const Records& ls) {
  float m = 1e9f;
  for (const auto& l : ls) m = std::min({m, l.x, l.x2});
  return m;
}
float maxX(const Records& ls) {
  float m = -1e9f;
  for (const auto& l : ls) m = std::max({m, l.x, l.x2});
  return m;
}
float minY(const Records& ls) {
  float m = 1e9f;
  for (const auto& l : ls) m = std::min({m, l.y, l.y2});
  return m;
}
float maxY(const Records& ls) {
  float m = -1e9f;
  for (const auto& l : ls) m = std::max({m, l.y, l.y2});
  return m;
}

using Key = std::vector<std::tuple<long, long, long, long>>;

// x, y, x2, y2 of every line, rounded and sorted.
Key key(const std::string& tex) {
  Key out;
  for (const auto& l : onlyLines(rec(tex))) {
    out.emplace_back(std::lround(l.x * 1000), std::lround(l.y * 1000), std::lround(l.x2 * 1000), std::lround(l.y2 * 1000));
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

TEST(diagrams_an_arrow_runs_between_two_cells_clear_of_both) {
  const auto tex = tikz("A \\arrow[r] & B");
  CHECK(quiet(tex));
  const auto r = rec(tex);
  const auto g = glyphs(r);
  CHECK_EQ(g.size(), std::size_t(2));
  const auto s = shafts(r);
  CHECK_EQ(s.size(), std::size_t(1));
  // Level, to the right, and with space left either side of it.
  CHECK(near(s[0].y, s[0].y2));
  CHECK(s[0].x > g[0].x);
  CHECK(s[0].x2 < g[1].x);
  CHECK(s[0].x - g[0].x > 5);
  CHECK(g[1].x - s[0].x2 > 3);
  // At the height of the math axis above the baseline, not the baseline.
  CHECK(s[0].y < g[0].y);
  CHECK(g[0].y - s[0].y > 1);
}

TEST(diagrams_an_arrow_has_a_head_which_is_more_than_a_shaft) {
  const auto r = rec(tikz("A \\arrow[r] & B"));
  const auto l = onlyLines(r);
  CHECK(l.size() > 8);
  const auto s = shafts(r);
  CHECK_EQ(s.size(), std::size_t(1));
  // The head points the way the arrow goes: its tip is the farthest right of
  // anything drawn, half a line width past the shaft's end.
  CHECK(maxX(l) > s[0].x2);
  CHECK(maxX(l) - s[0].x2 < 1);
  // A reverse arrow has it at the other end.
  const auto r2 = rec(tikz("A & B \\arrow[l]"));
  const auto l2 = onlyLines(r2);
  const auto s2 = shafts(r2);
  CHECK_EQ(s2.size(), std::size_t(1));
  CHECK(minX(l2) < std::min(s2[0].x, s2[0].x2));
  CHECK(std::min(s2[0].x, s2[0].x2) - minX(l2) < 1);
}

TEST(diagrams_arrows_in_a_row_stay_level_when_their_cells_differ_in_height) {
  const auto axisAbove = [](const std::string& tex) {
    const auto r = rec(tex);
    const auto g = glyphs(r);
    float leftmost = 1e9f, y = 0;
    for (const auto& x : g) {
      if (x.x < leftmost) {
        leftmost = x.x;
        y = x.y;
      }
    }
    return shafts(r)[0].y - y;
  };
  CHECK(near(axisAbove(tikz("A \\arrow[r] & B")), axisAbove(tikz("A \\arrow[r] & B^{2}_{p}"))));
}

TEST(diagrams_a_label_sits_on_the_left_of_the_way_an_arrow_goes_or_on_the_right_with_a_prime) {
  const auto above = rec(tikz("A \\arrow[r, \"f\"] & B"));
  const auto below = rec(tikz("A \\arrow[r, \"f\"'] & B"));
  CHECK_EQ(glyphs(above).size(), std::size_t(3));  // A, B and the label
  CHECK_EQ(glyphs(below).size(), std::size_t(3));
  const auto sa = shafts(above)[0], sb = shafts(below)[0];
  // The label is the glyph between the cells in x, and above or below the shaft.
  const auto la = middle(glyphs(above)), lb = middle(glyphs(below));
  CHECK_EQ(la.size(), std::size_t(1));
  CHECK_EQ(lb.size(), std::size_t(1));
  CHECK(la[0].y < sa.y);
  CHECK(lb[0].y > sb.y);
  // Centred along the arrow.
  CHECK(std::fabs(la[0].x - (sa.x + sa.x2) / 2) <= 0.1f * (sa.x + sa.x2) / 2);
  // Going down, left of the way is the right of the page.
  const auto down = rec(tikz("A \\arrow[d, \"g\"] \\\\ C"));
  const auto gd = glyphs(down);
  const auto sd = shafts(down)[0];
  const auto inner = [](const Records& g) {
    float lo = 1e9f, hi = -1e9f;
    for (const auto& x : g) {
      lo = std::min(lo, x.y);
      hi = std::max(hi, x.y);
    }
    Records out;
    for (const auto& x : g) {
      if (x.y > lo && x.y < hi) out.push_back(x);
    }
    return out;
  };
  CHECK_EQ(inner(gd).size(), std::size_t(1));
  CHECK(inner(gd)[0].x > sd.x);
  const auto down2 = rec(tikz("A \\arrow[d, \"g\"'] \\\\ C"));
  CHECK_EQ(inner(glyphs(down2)).size(), std::size_t(1));
  CHECK(inner(glyphs(down2))[0].x < shafts(down2)[0].x);
}

TEST(diagrams_a_label_option_moves_it_along_the_arrow) {
  const auto where = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r, \"f\"" + opt + "] & B"));
    const auto m = middle(glyphs(r));
    return m[0].x - shafts(r)[0].x;
  };
  const float mid = where("");
  CHECK(where(" near start") < mid);
  CHECK(where(" near end") > mid);
  CHECK(where("{pos=0.1}") < where(" near start"));
  CHECK(near(where("{near start, swap}"), where(" near start")));
}

TEST(diagrams_a_description_label_breaks_the_arrow_around_it) {
  const auto r = rec(tikz("A \\arrow[r, \"f\"{description}] & B", "[column sep=large]"));
  auto s = horizontal(shafts(r, 3));
  std::sort(s.begin(), s.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
  // Two pieces with a gap, and the label in the gap.
  CHECK_EQ(s.size(), std::size_t(2));
  const auto lab = middle(glyphs(r));
  CHECK_EQ(lab.size(), std::size_t(1));
  CHECK(lab[0].x > s[0].x2);
  CHECK(lab[0].x < s[1].x);
}

TEST(diagrams_a_dashed_arrow_is_made_of_short_pieces_with_gaps_a_dotted_one_of_dots) {
  auto d = horizontal(onlyLines(rec(tikz("A \\arrow[r, dashed] & B"))));
  std::sort(d.begin(), d.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
  CHECK(d.size() >= 3);
  // Equal pieces, and a gap after each.
  CHECK(near(len(d[1]), len(d[2])));
  for (std::size_t i = 0; i + 2 < d.size(); i++) CHECK(d[i + 1].x - d[i].x2 > 0.5f);
  const auto o = horizontal(onlyLines(rec(tikz("A \\arrow[r, dotted] & B"))));
  CHECK(o.size() > d.size());
  CHECK(check::maximum(lens(o)) < check::minimum(lens(d)));
}

TEST(diagrams_a_diagonal_arrow_leaves_and_enters_the_cells_along_the_line_between_them) {
  const auto tex = tikz("A \\arrow[dr] & B \\\\ C & D");
  CHECK(quiet(tex));
  const auto r = rec(tex);
  const auto g = glyphs(r);
  const auto s = shafts(r);
  CHECK_EQ(s.size(), std::size_t(1));
  CHECK_EQ(g.size(), std::size_t(4));
  // Down and to the right, between A and D.
  CHECK(s[0].x2 > s[0].x);
  CHECK(s[0].y2 > s[0].y);
  const auto &a = g[0], &d = g[3];
  CHECK(s[0].x > a.x);
  CHECK(s[0].x2 < d.x + 12);
  // On the line from A's anchor to D's: the two slopes agree.
  const float arrow = (s[0].y2 - s[0].y) / (s[0].x2 - s[0].x), cells = (d.y - a.y) / (d.x - a.x);
  CHECK(std::fabs(arrow - cells) <= 0.1f * std::fabs(cells));
}

TEST(diagrams_rightarrow_and_equal_are_two_parallel_lines_equal_with_no_head) {
  const auto eq = shafts(rec(tikz("A \\arrow[r, equal] & B")));
  CHECK_EQ(eq.size(), std::size_t(2));
  CHECK(near(eq[0].x, eq[1].x));
  CHECK(near(eq[0].x2, eq[1].x2));
  CHECK(std::fabs(eq[0].y - eq[1].y) > 1);
  // No head: nothing but the two lines.
  CHECK_EQ(onlyLines(rec(tikz("A \\arrow[r, equal] & B"))).size(), std::size_t(2));
  CHECK(onlyLines(rec(tikz("A \\arrow[r, Rightarrow] & B"))).size() > 10);
}

TEST(diagrams_hook_tail_and_mapsto_add_to_the_start_of_an_arrow) {
  struct Base {
    std::size_t n;
    float left, shaft;
  };
  const auto base = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r" + opt + "] & B"));
    const auto l = onlyLines(r);
    return Base{l.size(), minX(l), shafts(r)[0].x};
  };
  const auto plain = base("");
  for (const char* opt : {", hook", ", hook'", ", tail", ", mapsto"}) CHECK(base(opt).n > plain.n);
  // A hook reaches back past the shaft's start; a bar is across it.
  CHECK(base(", hook").left < base(", hook").shaft);
  const auto m = rec(tikz("A \\arrow[r, mapsto] & B"));
  const auto vert = vertical(onlyLines(m));
  CHECK_EQ(vert.size(), std::size_t(1));
  CHECK(std::fabs(vert[0].x - plain.shaft) < 1);
  // And two heads put a second head behind the first.
  CHECK(base(", two heads").n > plain.n);
}

TEST(diagrams_bend_and_shift_change_the_path_of_an_arrow) {
  const float y0 = shafts(rec(tikz("A \\arrow[r] & B")))[0].y;
  // A bend left goes above the level of the straight arrow, a bend right below.
  const auto bent = onlyLines(rec(tikz("A \\arrow[r, bend left=40] & B")));
  const auto right = onlyLines(rec(tikz("A \\arrow[r, bend right=40] & B")));
  CHECK(minY(bent) < y0 - 3);
  CHECK(maxY(right) > y0 + 3);
  // Shifted left, an arrow is above where it was; right, below.
  const float up = shafts(rec(tikz("A \\arrow[r, shift left] & B")))[0].y;
  const float down = shafts(rec(tikz("A \\arrow[r, shift right] & B")))[0].y;
  CHECK(up < y0);
  CHECK(down > y0);
  CHECK(near(y0 - up, down - y0));
}

TEST(diagrams_separations_set_the_distance_between_cells) {
  const auto d = [](const std::string& opts) {
    const auto g = glyphs(rec(tikz("A \\arrow[r] \\arrow[d] & B \\\\ C & D", opts)));
    return std::make_pair(g[1].x - g[0].x, g[2].y - g[0].y);  // column, row
  };
  const auto normal = d("");
  CHECK(d("[column sep=large]").first > normal.first);
  CHECK(near(d("[column sep=large]").second, normal.second));
  CHECK(d("[row sep=huge]").second > normal.second);
  CHECK(d("[row sep=small, column sep=tiny]").first < normal.first);
  // sep sets both, and a size can be a length.
  CHECK(near(d("[sep=3em]").first - d("[sep=2em]").first, 10));
  CHECK(near(d("[sep=3em]").second - d("[sep=2em]").second, 10));
}

TEST(diagrams_what_tikz_cd_does_that_is_not_supported_warns_once_and_the_arrow_is_still_drawn) {
  const auto tex = tikz("A \\arrow[r, wibble, wobble] & B \\arrow[r, wibble] & C");
  const auto w = layout(tex).warnings;
  int wibble = 0, wobble = 0;
  for (const auto& m : w) {
    wibble += m.find("wibble") != std::string::npos ? 1 : 0;
    wobble += m.find("wobble") != std::string::npos ? 1 : 0;
  }
  CHECK_EQ(wibble, 1);
  CHECK_EQ(wobble, 1);
  CHECK_EQ(shafts(rec(tex)).size(), std::size_t(2));
  CHECK(warnsAbout(tikz("A \\arrow[r] & B", "[sep=wide]"), "sep=wide"));
}

TEST(diagrams_an_arrow_that_goes_nowhere_or_out_of_the_diagram_warns_and_is_not_drawn) {
  const auto out = tikz("A \\arrow[r] & B \\arrow[r]");
  CHECK(warnsAbout(out, "outside the diagram"));
  CHECK_EQ(shafts(rec(out)).size(), std::size_t(1));
  CHECK(warnsAbout(tikz("A \\arrow[] & B"), "no direction"));
}

TEST(diagrams_the_older_syntax_and_the_shortcuts_draw_the_same_arrows) {
  CHECK(key(tikz("A \\rar & B \\dar \\\\ C & D")) == key(tikz("A \\arrow[r] & B \\arrow[d] \\\\ C & D")));
  CHECK(key(tikz("A \\arrow{r}{f} & B")) == key(tikz("A \\arrow[r, \"f\"] & B")));
  CHECK(key(tikz("A \\ar[rr] & B & C")) == key(tikz("A \\arrow[rr] & B & C")));
}

TEST(diagrams_a_diagram_can_be_set_in_a_display_in_a_document_and_on_a_cell_with_math_in_it) {
  const std::string tex =
    "Here is a square:\n\\[ \\begin{tikzcd} A \\arrow[r, \"\\alpha\"] & B^2 \\end{tikzcd} \\]\nand more.";
  CHECK(quiet(tex, InputMode::document));
  CHECK(onlyLines(rec(tex, InputMode::document)).size() > 8);
  // Problems inside a cell are the diagram's own.
  CHECK(warnsAbout(tikz("\\wibble \\arrow[r] & B"), "wibble"));
}

TEST(diagrams_a_reversed_arrow_has_its_head_at_the_start_a_both_way_one_at_both) {
  // The short pieces at each end of the shaft: the strokes of its heads.
  const auto heads = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r, " + opt + "] & B"));
    const auto s = shafts(r)[0];
    const float mid = (std::min(s.x, s.x2) + std::max(s.x, s.x2)) / 2;
    int left = 0, right = 0;
    for (const auto& l : onlyLines(r)) {
      if (len(l) >= 4) continue;
      left += std::max(l.x, l.x2) < mid ? 1 : 0;
      right += std::min(l.x, l.x2) > mid ? 1 : 0;
    }
    return std::make_pair(left, right);
  };
  CHECK(heads("leftarrow").first > 0);
  CHECK_EQ(heads("leftarrow").second, 0);
  CHECK(heads("leftrightarrow").first > 0);
  CHECK(heads("leftrightarrow").second > 0);
  CHECK(heads("Leftarrow").first > 0);
  CHECK_EQ(heads("Leftarrow").second, 0);
  CHECK(heads("Leftrightarrow").second > 0);
}

TEST(diagrams_shorten_takes_a_length_off_the_start_or_the_end_of_an_arrow) {
  const float full = len(shafts(rec(tikz("A \\arrow[r] & B")))[0]);
  const float shorter = len(shafts(rec(tikz("A \\arrow[r, shorten <=0.5em, shorten >=0.2em] & B")))[0]);
  CHECK(near(full - shorter, 7));
  const float both = len(shafts(rec(tikz("A \\arrow[r, shorten=0.5em] & B")))[0]);
  CHECK(near(full - both, 10));
  CHECK(warnsAbout(tikz("A \\arrow[r, shorten <=wide] & B"), "not a size"));
}

TEST(diagrams_an_arrow_can_have_its_own_colour_its_labels_too) {
  for (const char* opt : {"red", "color=red", "draw=red"}) {
    const auto r = rec(tikz(std::string("A \\arrow[r, \"f\", ") + opt + "] \\arrow[d] & B \\\\ C & D"));
    Records red, black;
    for (const auto& l : onlyLines(r)) {
      if (l.col == check::kRed) red.push_back(l);
      if (l.col == 0xFF000000u) black.push_back(l);
    }
    CHECK(red.size() > 8);
    CHECK(black.size() > 8);
    // The red ones are the horizontal arrow's: none of them goes downward.
    for (const auto& l : red) CHECK(std::fabs(l.y - l.y2) < 4);
    bool redGlyph = false;
    for (const auto& g : glyphs(r)) redGlyph = redGlyph || g.col == check::kRed;
    CHECK(redGlyph);
  }
  CHECK(warnsAbout(tikz("A \\arrow[r, color=wibble] & B"), "colour"));
}

TEST(diagrams_from_and_to_name_cells_outright_or_by_a_direction_from_the_cell) {
  CHECK(key(tikz("A & B \\arrow[dl] \\\\ C & D")) == key(tikz("A & B \\\\ C & D \\arrow[from=1-2, to=2-1]")));
  CHECK(key(tikz("A \\arrow[from=r, to=d] & B \\\\ C & D")) == key(tikz("A & B \\arrow[dl] \\\\ C & D")));
  CHECK(warnsAbout(tikz("A \\arrow[to=elsewhere] & B"), "row-column"));
}

TEST(diagrams_cramped_takes_room_out_of_a_diagram) {
  const auto size = [](const std::string& opts) {
    const auto l = layout(tikz("A \\arrow[r] \\arrow[d] & B \\\\ C & D", opts));
    return std::make_pair(l.width, l.height);
  };
  CHECK(size("[cramped]").first < size("").first);
  CHECK(size("[cramped]").second < size("").second);
}

TEST(diagrams_an_arrow_that_crosses_over_cuts_a_gap_in_the_ones_before_it) {
  const auto pieces = [](const std::string& second) {
    const auto r = rec(tikz("A \\arrow[dr] & B \\\\ C " + second + " & D", "[row sep=large, column sep=large]"));
    // The first arrow's shaft: the longest pieces going down and to the right.
    int n = 0;
    for (const auto& l : onlyLines(r)) n += (l.x2 > l.x && l.y2 > l.y && len(l) > 6) ? 1 : 0;
    return n;
  };
  CHECK_EQ(pieces(""), 1);
  CHECK_EQ(pieces("\\arrow[ur]"), 1);
  CHECK_EQ(pieces("\\arrow[ur, crossing over]"), 2);
}

TEST(diagrams_squiggly_makes_a_zigzag_shaft_a_harpoon_has_one_barb_mapsfrom_a_bar_at_the_end) {
  const auto z = onlyLines(rec(tikz("A \\arrow[r, squiggly] & B", "[column sep=large]")));
  std::vector<float> ys;
  for (const auto& l : z) {
    ys.push_back(l.y);
  }
  CHECK(check::distinct(ys, 2) > 5);
  const auto small = [](const std::string& opt) {
    int n = 0;
    for (const auto& l : onlyLines(rec(tikz("A \\arrow[r, " + opt + "] & B")))) n += len(l) < 4 ? 1 : 0;
    return n;
  };
  // One barb of a head's two, and the short stroke that is the harpoon's stem.
  CHECK_EQ(small("harpoon"), small("") / 2 + 1);
  CHECK_EQ(small("harpoon'"), small("") / 2 + 1);
  const auto m = rec(tikz("A \\arrow[r, mapsfrom] & B"));
  const auto vert = vertical(onlyLines(m));
  CHECK_EQ(vert.size(), std::size_t(1));
  CHECK(std::fabs(vert[0].x - maxX(shafts(m))) < 1);
}

TEST(diagrams_a_phantom_arrows_label_is_centred_on_where_its_path_would_be) {
  const auto r = rec(tikz("A \\arrow[r, phantom, \"\\lrcorner\"] & B"));
  CHECK_EQ(onlyLines(r).size(), std::size_t(0));
  const auto g = glyphs(r);
  CHECK_EQ(g.size(), std::size_t(3));
  const auto mid = middle(g);
  CHECK_EQ(mid.size(), std::size_t(1));
  const auto s = shafts(rec(tikz("A \\arrow[r] & B")))[0];
  // Half the label's width left of the middle, give or take.
  CHECK(std::fabs(mid[0].x - (s.x + s.x2) / 2) < 6);
}

TEST(diagrams_arrows_and_labels_are_options_of_every_arrow_and_label) {
  const auto dashes = [](const std::string& opts) {
    int n = 0;
    for (const auto& l : onlyLines(rec(tikz("A \\arrow[r] & B", opts)))) {
      n += (std::fabs(l.y - l.y2) < 1e-3f && len(l) > 2 && len(l) < 8) ? 1 : 0;
    }
    return n;
  };
  CHECK(dashes("[arrows=dashed]") > dashes(""));
  CHECK_EQ(dashes("[arrows={dashed}]"), dashes("[arrows=dashed]"));
  const auto below = rec(tikz("A \\arrow[r, \"f\"] & B", "[labels=swap]"));
  CHECK(middle(glyphs(below))[0].y > shafts(below)[0].y);
  const auto both = rec(tikz("A \\arrow[r, \"f\"] & B", "[diagrams={labels=swap}]"));
  CHECK(middle(glyphs(both))[0].y > shafts(both)[0].y);
}

// --- amscd's CD ----------------------------------------------------------------

TEST(diagrams_cd_sets_objects_in_rows_with_arrows_of_at_least_2_5pc_between_them) {
  const auto tex = cd("A @>>> B @>>> C");
  CHECK(quiet(tex));
  const auto r = rec(tex);
  CHECK_EQ(glyphs(r).size(), std::size_t(3));
  const auto s = shafts(r);
  CHECK_EQ(s.size(), std::size_t(2));
  // 2.5pc is 30 points, 29.9 big points, at a ten-point size.
  CHECK(std::min(len(s[0]), len(s[1])) >= 29.5f);
  CHECK(near(len(s[0]), len(s[1])));
}

TEST(diagrams_a_cd_arrow_grows_to_its_label) {
  const float shorter = len(shafts(rec(cd("A @>f>> B")))[0]);
  const float longer = len(shafts(rec(cd("A @>{\\text{a rather long label}}>> B")))[0]);
  CHECK(longer > shorter);
}

TEST(diagrams_cd_rows_are_texs_a_baselineskip_each_side_of_a_row_of_arrows_more_for_labels) {
  const auto rows = [](const std::string& tex) {
    const auto g = glyphs(rec(cd(tex)));
    const float left = check::minimum(check::column(g, &DrawRecord::x));
    std::vector<float> ys;
    for (const auto& x : g) {
      if (x.x < left + 1) ys.push_back(x.y);
    }
    return check::uniqueSorted(ys, 2);
  };
  // Object rows 2em + 2em apart, the vertical arrows 1.8em, centred in between.
  const auto plain = rows("A @>>> B \\\\ @VVV @VVV \\\\ C @>>> D");
  CHECK_EQ(plain.size(), std::size_t(2));
  CHECK(near(static_cast<float>(plain[1] - plain[0]), 40));
  // A label under an arrow in the first row, and one over an arrow in the
  // last, take the room a lineskip (0.4em) leaves them.
  const auto roomy = rows("A @>>a> B \\\\ @VVV @VVV \\\\ C @>b>> D");
  CHECK_EQ(roomy.size(), std::size_t(2));
  CHECK(roomy[1] - roomy[0] > plain[1] - plain[0]);
  // And an arrow stays the same length whatever the other cells of its column.
  const auto h = horizontal(shafts(rec(cd("A @>>> B \\\\ @VVV @VVV \\\\ \\mathrm{CCCCCC} @>>> D"))));
  CHECK_EQ(h.size(), std::size_t(2));
  CHECK(near(len(h[0]), len(h[1])));
}

TEST(diagrams_cd_labels_are_above_and_below_left_and_right_as_the_at_sign_says) {
  const auto r = rec(cd("A @>a>b> B @<c<d< C \\\\ @VeVfV @. @AgAhA \\\\ D @= E @>>> F"));
  const auto g = glyphs(r);
  const auto s = shafts(r);
  auto top = horizontal(s);
  const float highest = minY(top);
  Records topRow;
  for (const auto& l : top) {
    if (l.y == highest) topRow.push_back(l);
  }
  std::sort(topRow.begin(), topRow.end(), [](const auto& a, const auto& b) { return std::min(a.x, a.x2) < std::min(b.x, b.x2); });
  CHECK_EQ(topRow.size(), std::size_t(2));
  const auto between = [&](const DrawRecord& row, bool above) {
    int n = 0;
    for (const auto& x : g) {
      if (((x.y < row.y) == above) && x.x > std::min(row.x, row.x2) && x.x < std::max(row.x, row.x2) &&
          std::fabs(x.y - row.y) < 20) {
        n++;
      }
    }
    return n;
  };
  // Right arrow: a above, b below. Left arrow: c above and d below as well,
  // which is the right of the way it goes.
  for (const auto& row : topRow) {
    CHECK_EQ(between(row, true), 1);
    CHECK_EQ(between(row, false), 1);
  }
  // Down, in the first column: e on its left, f on its right; up, in the
  // third: g on its left, h on its right.
  Records vert;
  for (const auto& l : s) {
    if (std::fabs(l.x - l.x2) < 1e-3f && l.y != l.y2) vert.push_back(l);
  }
  std::sort(vert.begin(), vert.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
  CHECK_EQ(vert.size(), std::size_t(2));
  const auto beside = [&](const DrawRecord& v, bool left) {
    const float mid = (v.y + v.y2) / 2;
    int n = 0;
    for (const auto& x : g) {
      if (((x.x < v.x) == left) && std::fabs(x.y - mid) < 12 && std::fabs(x.x - v.x) < 15) n++;
    }
    return n;
  };
  for (const auto& v : vert) {
    CHECK_EQ(beside(v, true), 1);
    CHECK_EQ(beside(v, false), 1);
  }
}

TEST(diagrams_cd_equals_and_bar_are_two_plain_lines_and_dot_leaves_a_place_empty) {
  const auto l = onlyLines(rec(cd("A @= B \\\\ @| @. \\\\ C")));
  // Two horizontal and two vertical lines, nothing else: no heads.
  CHECK_EQ(l.size(), std::size_t(4));
  CHECK_EQ(horizontal(l).size(), std::size_t(2));
  CHECK_EQ(vertical(l).size(), std::size_t(2));
}

TEST(diagrams_a_cd_with_something_it_cannot_read_warns_and_goes_on) {
  CHECK(warnsAbout(cd("A @X B @>>> C"), "@X"));
  CHECK(warnsAbout(cd("@VVV \\\\ A"), "above"));
  CHECK_EQ(shafts(rec(cd("A @X B @>>> C"))).size(), std::size_t(1));
}

// A mix names an arrow's colour as a name does (tikz-cd reads xcolor's syntax).
TEST(diagrams_an_arrow_takes_a_mixed_colour) {
  CHECK(check::drawsColour(check::render("\\begin{tikzcd} A \\arrow[r, red!50] & B \\end{tikzcd}"), 0xFFFF8080u));
  CHECK(check::drawsColour(check::render("\\begin{tikzcd} A \\arrow[r, draw=red!50] & B \\end{tikzcd}"), 0xFFFF8080u));
}
