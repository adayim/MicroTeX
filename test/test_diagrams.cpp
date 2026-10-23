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

// --- what a tikz-cd diagram can say that was once read as text or dropped, one key at a time ----------------------

// Cell A of a diagram: its leftmost glyph. (Not the first by height: a label can be above it.)
static DrawRecord cellA(const Records& r) {
  const auto g = glyphs(r);
  DrawRecord a = g[0];
  for (const auto& x : g) {
    if (x.x < a.x) a = x;
  }
  return a;
}

// The label of a diagram of two cells in a row and one label: the glyph that is neither the leftmost nor the rightmost.
static DrawRecord labelOf(const Records& r) {
  const auto g = glyphs(r);
  float lo = 1e9f, hi = -1e9f;
  for (const auto& x : g) {
    lo = std::min(lo, x.x);
    hi = std::max(hi, x.x);
  }
  for (const auto& x : g) {
    if (x.x > lo && x.x < hi) return x;
  }
  return g[0];
}

TEST(diagrams_the_older_syntax_reads_every_label_group_and_a_short_command_takes_its_labels) {
  // Two labels, each with its own options; before, the second was set as text in the cell.
  const auto two = tikz("A \\arrow{r}[near start]{\\phi}[near end]{\\psi} & B");
  CHECK(quiet(two));
  CHECK_EQ(glyphs(rec(two)).size(), std::size_t(4));  // A, B and the two labels
  CHECK(key(two) == key(tikz("A \\arrow[r, \"\\phi\" near start, \"\\psi\" near end] & B")));
  // `\rar[options]{label}`: the group after the options is a label, not a target.
  CHECK(key(tikz("A \\rar[red]{\\phi} & B")) == key(tikz("A \\arrow[red]{r}{\\phi} & B")));
  CHECK(key(tikz("A \\rar[red]{\\phi} & B")) == key(tikz("A \\arrow[r, red, \"\\phi\"] & B")));
  // The target of the older syntax is a cell or a node as well as a direction.
  CHECK(key(tikz("A \\arrow{2-2} & B \\\\ C & D")) == key(tikz("A \\arrow[dr] & B \\\\ C & D")));
}

TEST(diagrams_extra_space_after_an_ampersand_or_a_row_break_is_added_to_the_separation) {
  const auto plain = glyphs(rec(tikz("A & B \\\\ C & D")));
  const auto wide = glyphs(rec(tikz("A &[2em] B \\\\ C & D")));
  const auto tall = glyphs(rec(tikz("A & B \\\\[2em] C & D")));
  CHECK(quiet(tikz("A &[2em] B \\\\[2em] C & D")));
  CHECK_EQ(wide.size(), std::size_t(4));
  // Glyphs are sorted by row then column: A, B, C, D. At 10pt, 2em is 20.
  CHECK(near(wide[1].x - plain[1].x, 20));
  CHECK(near(wide[0].x, plain[0].x));
  CHECK(near(tall[2].y - plain[2].y, 20));
  CHECK(near(tall[1].y, plain[1].y));
  // `[` that is not a length is text, as before.
  CHECK(glyphs(rec(tikz("A & [a,b]"))).size() > glyphs(rec(tikz("A & B"))).size());
}

TEST(diagrams_a_cell_has_node_options_a_name_and_the_name_ends_an_arrow) {
  const auto tex = tikz("A \\arrow[r] & |[blue, name=Z]| B \\\\ C \\arrow[to=Z] & D");
  CHECK(quiet(tex));
  CHECK(key(tex) == key(tikz("A \\arrow[r] & B \\\\ C \\arrow[ur] & D")));
  CHECK(check::drawsColour(check::render(tex), 0xFF0000FFu));
  // The options are not text.
  CHECK_EQ(glyphs(rec(tex)).size(), std::size_t(4));
  CHECK(key(tikz("|[alias=Z]| A & B \\arrow[to=Z]")) == key(tikz("A & B \\arrow[l]")));
  CHECK(key(tikz("A & B \\\\ |[name=Z]| C & D \\arrow[from=Z, to=1-2]")) ==
        key(tikz("A & B \\\\ C \\arrow[ur] & D")));
  // `draw` outlines the cell.
  CHECK_EQ(onlyLines(rec(tikz("|[draw]| A", ""))).size(), std::size_t(4));
  CHECK(warnsAbout(tikz("|[wibble]| A"), "wibble"));
}

TEST(diagrams_a_label_that_says_above_below_left_or_right_stands_on_that_side_of_its_point) {
  const auto at = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r, \"f\"" + opt + "] & B"));
    return std::make_pair(middle(glyphs(r))[0], shafts(r)[0]);
  };
  const auto base = at("");
  CHECK(quiet(tikz("A \\arrow[r, \"f\" below] & B")));
  CHECK(at(" below").first.y > at(" below").second.y);
  CHECK(at(" above").first.y < at(" above").second.y);
  CHECK(near(at(" above").first.x, base.first.x));
  // Beside it, level with the point, and not centred on it.
  CHECK(at(" left").first.x < base.first.x);
  CHECK(at(" right").first.x > base.first.x);
  CHECK(near(at(" left").first.y, at(" left").second.y, 0.5f));
  // The same said of the arrow.
  const auto r = rec(tikz("A \\arrow[r, below, \"f\"] & B"));
  CHECK(near(middle(glyphs(r))[0].y, at(" below").first.y));
  CHECK(at(" anchor=north").first.y > at(" anchor=north").second.y);
}

TEST(diagrams_a_label_can_have_its_own_size_shift_colour_and_a_name) {
  // The label is the smallest glyph (a script); its place is taken from A's, since the picture grows with it.
  const auto label = [](const std::string& opts) {
    const auto r = rec(tikz("A \\arrow[r, \"f\"" + opts + "] & B"));
    return std::make_pair(labelOf(r), cellA(r));
  };
  const auto base = label("");
  const auto roomy = label("{inner sep=3ex}");
  CHECK((roomy.second.y - roomy.first.y) > (base.second.y - base.first.y) + 5);
  const auto moved = label("{xshift=0.5em}");
  CHECK(near(moved.first.x - moved.second.x - (base.first.x - base.second.x), 5));
  CHECK_EQ(label("{red}").first.col, check::kRed);
  // font= puts the label back to full size (a script is smaller).
  CHECK(label("{font=}").first.font_size > base.first.font_size);
  // A named label is something an arrow can start from.
  const auto two = tikz("A \\arrow[r, \"\"{name=U}] & B \\arrow[from=U, to=1-1]");
  CHECK(quiet(two));
  CHECK(shafts(rec(two), 3).size() >= 2);
  CHECK(warnsAbout(tikz("A \\arrow[r, \"f\"{wibble}] & B"), "wibble"));
}

TEST(diagrams_description_and_marking_put_the_label_on_the_arrow_description_cuts_it_marking_does_not) {
  const auto cut = horizontal(shafts(rec(tikz("A \\arrow[r, \"f\" description] & B", "[column sep=large]")), 3));
  CHECK_EQ(cut.size(), std::size_t(2));
  const auto over = horizontal(shafts(rec(tikz("A \\arrow[r, \"f\" marking] & B", "[column sep=large]")), 3));
  CHECK_EQ(over.size(), std::size_t(1));
  // A background colour is what it is filled with.
  const auto r = check::render(tikz("A \\arrow[r, \"f\" description] & B", "[background color=yellow]"));
  CHECK(check::drawsColour(r, 0xFFFFFF00u));
}

TEST(diagrams_a_sloped_label_is_turned_along_the_arrow_and_drawn_as_an_outline) {
  CHECK(quiet(tikz("A \\arrow[dr, \"f\", sloped] & B \\\\ C & D")));
  const auto count = [](const std::string& tex, DrawRecord::Type type) {
    int n = 0;
    for (const auto& x : rec(tex)) n += x.type == type ? 1 : 0;
    return n;
  };
  // A glyph of the font cannot be drawn turned, so the turned label is its outline: no glyph of its own.
  const std::string turned = tikz("A \\arrow[dr, \"f\", sloped] & B \\\\ C & D");
  const std::string upright = tikz("A \\arrow[dr, \"f\"] & B \\\\ C & D");
  CHECK_EQ(count(upright, DrawRecord::GLYPH), 5);
  CHECK_EQ(count(turned, DrawRecord::GLYPH), 4);
  CHECK_EQ(count(turned, DrawRecord::PATH), 1);
  // Along a level arrow it is not turned at all.
  CHECK_EQ(count(tikz("A \\arrow[r, \"f\", sloped] & B"), DrawRecord::PATH), 0);
  CHECK_EQ(count(tikz("A \\arrow[r, \"f\"{rotate=20}] & B"), DrawRecord::PATH), 1);
}

TEST(diagrams_shift_left_and_right_take_a_length_or_a_number_of_the_default) {
  // Heights above A's baseline: the picture grows with an arrow that moves out of it.
  const auto up = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r" + opt + "] & B"));
    return glyphs(r)[0].y - shafts(r)[0].y;
  };
  const float y0 = up("");
  CHECK(quiet(tikz("A \\arrow[r, shift left=2em] & B")));
  CHECK(near(up(", shift left=2em") - y0, 20));
  CHECK(near(y0 - up(", shift right=2em"), 20));
  // A bare number is a multiple of 0.56ex, which `shift left` alone is one of.
  CHECK(near(up(", shift left=0.56ex") - y0, up(", shift left") - y0));
  CHECK(near(up(", shift left=3") - y0, 3 * (up(", shift left") - y0)));
  // xshift, yshift and shift move the arrow, and its label with it.
  const auto at = [](const std::string& opts) {
    const auto r = rec(tikz("A \\arrow[r, \"f\"" + opts + "] & B"));
    const auto a = cellA(r), small = labelOf(r);
    return std::make_tuple(shafts(r)[0].x - a.x, a.y - shafts(r)[0].y, small.x - a.x, a.y - small.y);
  };
  const auto base = at(""), moved = at(", yshift=1em, xshift=-0.5em");
  CHECK(near(std::get<0>(moved) - std::get<0>(base), -5));
  CHECK(near(std::get<1>(moved) - std::get<1>(base), 10));
  CHECK(near(std::get<2>(moved) - std::get<2>(base), -5));
  CHECK(near(std::get<3>(moved) - std::get<3>(base), 10));
  CHECK(key(tikz("A \\arrow[r, shift={(0.5em,1em)}] & B")) == key(tikz("A \\arrow[r, xshift=0.5em, yshift=1em] & B")));
}

TEST(diagrams_a_negative_shorten_makes_an_arrow_longer) {
  // The shaft is in pieces where the path was lengthened, so measure what it spans.
  const auto span = [](const std::string& opt) {
    const auto s = horizontal(shafts(rec(tikz("A \\arrow[r" + opt + "] & B")), 3));
    return maxX(s) - minX(s);
  };
  const float full = span("");
  CHECK(quiet(tikz("A \\arrow[r, shorten=-0.5em] & B")));
  CHECK(near(span(", shorten=-0.5em") - full, 10));
  CHECK(near(span(", shorten >=-1em") - full, 10));
  CHECK(near(span(", shorten <=-1em") - full, 10));
}

TEST(diagrams_math_mode_false_sets_cells_and_labels_as_text) {
  const auto count = [](const std::string& tex, DrawRecord::Type type) {
    int n = 0;
    for (const auto& x : rec(tex)) n += x.type == type ? 1 : 0;
    return n;
  };
  const auto all = tikz("A \\arrow[r, \"f\"] & B", "[math mode=false]");
  CHECK(quiet(all));
  CHECK_EQ(count(all, DrawRecord::GLYPH), 0);
  CHECK_EQ(count(all, DrawRecord::TEXT), 3);
  // On an arrow, only its labels.
  const auto one = tikz("A \\arrow[r, math mode=false, \"f\"] & B");
  CHECK_EQ(count(one, DrawRecord::GLYPH), 2);
  CHECK_EQ(count(one, DrawRecord::TEXT), 1);
}

TEST(diagrams_a_loop_in_and_out_looseness_and_controls_curve_the_arrow) {
  const auto tex = tikz("A \\arrow[loop above] & B");
  CHECK(quiet(tex));
  const auto l = onlyLines(rec(tex));
  const auto g = glyphs(rec(tex));
  // A curve above the cell, which starts and ends at it.
  CHECK(minY(l) < g[0].y - 8);
  CHECK(l.size() > 20);
  // Left and right of it, and below.
  CHECK(minX(onlyLines(rec(tikz("A \\arrow[loop left] & B")))) < g[0].x - 3);
  CHECK(maxY(onlyLines(rec(tikz("A \\arrow[loop below] & B")))) > g[0].y + 3);
  // How high a curve gets above A's baseline.
  const auto height = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r" + opt + "] & B"));
    return glyphs(r)[0].y - minY(onlyLines(r));
  };
  const float level = height("");
  // out and in, the angles from the east: up and to the right, then down onto B.
  CHECK(height(", out=60, in=120") > level + 3);
  // Looseness stretches the curve, and the controls are given outright.
  CHECK(height(", out=60, in=120, looseness=0.5") < height(", out=60, in=120"));
  CHECK(height(", out=60, in=120, looseness=2") > height(", out=60, in=120"));
  CHECK(quiet(tikz("A \\arrow[r, controls={+(0.5,1) and +(-0.5,1)}] & B")));
  CHECK(height(", controls={+(0.5,1) and +(-0.5,1)}") > level + 10);
}

TEST(diagrams_to_path_with_a_right_angle_goes_round_it_and_rounded_corners_round_it) {
  const auto r = onlyLines(rec(tikz("A \\arrow[dr, to path=|- (\\tikztotarget)] & B \\\\ C & D")));
  CHECK(quiet(tikz("A \\arrow[dr, to path=|- (\\tikztotarget)] & B \\\\ C & D")));
  CHECK(!shafts(r).empty());
  // Vertical first, then horizontal; -| the other way.
  const auto a = shafts(rec(tikz("A \\arrow[dr, to path=|- (\\tikztotarget)] & B \\\\ C & D")));
  const auto b = shafts(rec(tikz("A \\arrow[dr, to path=-| (\\tikztotarget)] & B \\\\ C & D")));
  CHECK(!vertical(a).empty() && !horizontal(a).empty());
  CHECK(!vertical(b).empty() && !horizontal(b).empty());
  CHECK(minY(vertical(a)) < minY(horizontal(a)) + 1);
  CHECK(minX(horizontal(b)) < minX(vertical(b)) + 1);
  // Rounded, there are more pieces.
  CHECK(onlyLines(rec(tikz("A \\arrow[dr, rounded corners, to path=|- (\\tikztotarget)] & B \\\\ C & D"))).size() > r.size());
  CHECK(warnsAbout(tikz("A \\arrow[r, to path={-- (1,1)}] & B"), "to path"));
}

TEST(diagrams_start_and_end_anchors_put_the_ends_of_an_arrow_on_a_point_of_the_cell) {
  const auto start = [](const std::string& anchor) {
    const auto r = rec(tikz("A \\arrow[r, start anchor=" + anchor + "] & B", "[column sep=large]"));
    return onlyLines(r);
  };
  const auto plain = shafts(rec(tikz("A \\arrow[r] & B", "[column sep=large]")))[0];
  CHECK(quiet(tikz("A \\arrow[r, start anchor=north] & B")));
  // From the top of the cell, not its middle.
  CHECK(minY(start("north")) < plain.y - 1);
  CHECK(maxY(start("south")) > plain.y + 1);
  CHECK(near(shafts(rec(tikz("A \\arrow[r, start anchor=north] & B", "[column sep=large]")))[0].x,
             shafts(rec(tikz("A \\arrow[r, start anchor=south] & B", "[column sep=large]")))[0].x));
  // Moved from where it was, as `[xshift=..]` has it.
  const auto moved = shafts(rec(tikz("A \\arrow[r, start anchor={[xshift=-1em]}] & B", "[column sep=large]")))[0];
  CHECK(near(plain.x - moved.x, 10));
  CHECK(warnsAbout(tikz("A \\arrow[r, start anchor=wibble] & B"), "wibble"));
  // The cell's own `real east` is the middle of its box, `east` the axis.
  CHECK(!onlyLines(rec(tikz("A \\arrow[r, end anchor=real west] & B"))).empty());
}

TEST(diagrams_line_width_and_the_thickness_names_set_the_strokes_and_scale_the_tips) {
  const auto width = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r" + opt + "] & B"));
    float w = 0.f;
    for (const auto& l : onlyLines(r)) w = std::max(w, l.line_width);
    return w;
  };
  CHECK(quiet(tikz("A \\arrow[r, thick] & B")));
  const float w0 = width("");
  CHECK(width(", line width=2pt") > w0 * 2);
  CHECK(width(", thick") > width(", thin"));
  CHECK(width(", very thick") > width(", thick"));
  CHECK(width(", ultra thick") > width(", very thick"));
  // A heavier line has a bigger head, the tips being measured in line widths.
  const auto head = [](const std::string& opt) {
    return maxX(onlyLines(rec(tikz("A \\arrow[r" + opt + "] & B")))) - maxX(shafts(rec(tikz("A \\arrow[r" + opt + "] & B"))));
  };
  CHECK(head("") < 1);
  float reach = 0.f;
  const auto strokes = onlyLines(rec(tikz("A \\arrow[r, line width=2pt] & B")));
  const auto longest = shafts(rec(tikz("A \\arrow[r, line width=2pt] & B")))[0];
  for (const auto& l : strokes) {
    if (len(l) < 12) reach = std::max(reach, std::fabs(l.y - longest.y));
  }
  float reach0 = 0.f;
  const auto strokes0 = onlyLines(rec(tikz("A \\arrow[r] & B")));
  const auto longest0 = shafts(rec(tikz("A \\arrow[r] & B")))[0];
  for (const auto& l : strokes0) {
    if (len(l) < 12) reach0 = std::max(reach0, std::fabs(l.y - longest0.y));
  }
  CHECK(reach > reach0 * 2);
}

TEST(diagrams_dotted_and_dashed_have_the_dash_patterns_tikz_gives_them) {
  const auto pieces = [](const std::string& opt) {
    return horizontal(onlyLines(rec(tikz("A \\arrow[r" + opt + "] & B", "[column sep=large]")))).size();
  };
  CHECK(quiet(tikz("A \\arrow[r, densely dotted] & B")));
  // Closer dots are more of them; looser dashes fewer.
  CHECK(pieces(", densely dotted") > pieces(", dotted"));
  CHECK(pieces(", loosely dotted") < pieces(", dotted"));
  CHECK(pieces(", densely dashed") > pieces(", loosely dashed"));
  CHECK(pieces(", dash pattern=on 2pt off 3pt") > 3);
  CHECK(pieces(", dash pattern=on 2pt off 1pt") > pieces(", dash pattern=on 2pt off 3pt"));
  CHECK(warnsAbout(tikz("A \\arrow[r, dash pattern=wibble] & B"), "dash pattern"));
}

TEST(diagrams_double_draws_two_lines_apart_by_the_double_distance) {
  CHECK(quiet(tikz("A \\arrow[r, double] & B")));
  const auto d = shafts(rec(tikz("A \\arrow[r, double] & B")));
  CHECK_EQ(d.size(), std::size_t(2));
  // The lines' own width and the double distance (0.6pt unless said) apart; 2pt is 1.4pt more.
  const auto wide = shafts(rec(tikz("A \\arrow[r, double, double distance=2pt] & B")));
  CHECK_EQ(wide.size(), std::size_t(2));
  CHECK(near(std::fabs(wide[0].y - wide[1].y) - std::fabs(d[0].y - d[1].y), 1.4f * 72.f / 72.27f, 0.02f));
  CHECK(std::fabs(d[0].y - d[1].y) > 0.6f);
}

TEST(diagrams_the_arrows_that_are_the_other_way_round_end_in_what_the_right_hand_ones_start_with) {
  // twoheadleftarrow is two heads at the start, leftharpoon a barb there, hookleftarrow a hook at the end.
  for (const char* name : {"twoheadleftarrow", "leftharpoonup", "leftharpoondown", "hookleftarrow", "leftarrowtail"}) {
    CHECK(quiet(tikz(std::string("A \\arrow[r, ") + name + "] & B")));
  }
  const auto ends = [](const std::string& opt) {
    const auto r = rec(tikz("A \\arrow[r, " + opt + "] & B"));
    const auto s = shafts(r)[0];
    const float mid = (std::min(s.x, s.x2) + std::max(s.x, s.x2)) / 2;
    int left = 0, right = 0;
    for (const auto& l : onlyLines(r)) {
      if (len(l) >= 12) continue;
      (std::max(l.x, l.x2) < mid ? left : right)++;
    }
    return std::make_pair(left, right);
  };
  CHECK(ends("twoheadleftarrow").first > ends("leftarrow").first);
  CHECK_EQ(ends("twoheadleftarrow").second, 0);
  CHECK(ends("leftharpoonup").first > 0);
  CHECK_EQ(ends("leftharpoonup").second, 0);
  CHECK(ends("hookleftarrow").first > 0);
  CHECK(ends("hookleftarrow").second > 0);  // the head is at the start, the hook at the end
  CHECK(ends("leftarrowtail").second > 0);
  // The left harpoons are the right ones turned about.
  CHECK(ends("leftharpoonup").first == ends("rightharpoonup").second);
}

TEST(diagrams_tikzs_arrow_tip_keys_name_the_tips) {
  CHECK(key(tikz("A \\arrow[r, <-] & B")) == key(tikz("A \\arrow[r, leftarrow] & B")));
  CHECK(key(tikz("A \\arrow[r, <->] & B")) == key(tikz("A \\arrow[r, leftrightarrow] & B")));
  CHECK(key(tikz("A \\arrow[r, ->] & B")) == key(tikz("A \\arrow[r] & B")));
  CHECK(key(tikz("A \\arrow[r, -] & B")) == key(tikz("A \\arrow[r, dash] & B")));
  CHECK(key(tikz("A \\arrow[r, -{cm double to}] & B")) == key(tikz("A \\arrow[r, two heads] & B")));
  CHECK(key(tikz("A \\arrow[r, {cm bar}-{cm to}] & B")) == key(tikz("A \\arrow[r, mapsto] & B")));
}

TEST(diagrams_tikzcdset_sets_what_every_diagram_after_it_starts_with) {
  const std::string body = "A \\arrow[r] & B";
  const auto set = [](const std::string& keys) { return "\\tikzcdset{" + keys + "}"; };
  CHECK(quiet(set("arrows=dashed") + tikz(body)));
  CHECK(key(set("arrows=dashed") + tikz(body)) == key(tikz(body, "[arrows=dashed]")));
  // A separation set that way is set again by every diagram (`normal`), as in tikz-cd: it has no effect. Through
  // `diagrams` or `every diagram` it has.
  CHECK(key(set("sep=large") + tikz(body)) == key(tikz(body)));
  CHECK(key(set("column sep=large") + tikz(body)) == key(tikz(body)));
  CHECK(key(set("diagrams={sep=large}") + tikz(body)) == key(tikz(body, "[sep=large]")));
  CHECK(key(set("every diagram/.append style={sep=large}") + tikz(body)) == key(tikz(body, "[sep=large]")));
  CHECK(key(set("cramped") + tikz(body)) == key(tikz(body, "[cramped]")));
  // The sizes of row sep and column sep can be changed, and made.
  CHECK(key(set("row sep/normal=3em") + tikz("A \\arrow[d] \\\\ B")) == key(tikz("A \\arrow[d] \\\\ B", "[row sep=3em]")));
  CHECK(key(set("column sep/mine/.initial=4em") + tikz(body, "[column sep=mine]")) == key(tikz(body, "[column sep=4em]")));
  // Styles of its own making, for arrows.
  CHECK(key(set("my arrow/.style={dashed}") + tikz("A \\arrow[r, my arrow] & B")) == key(tikz("A \\arrow[r, dashed] & B")));
  // `every arrow`, `every label` and `every cell` are the ones the diagram's `arrows=` and kin append to.
  CHECK(key(set("every arrow/.append style={dashed}") + tikz(body)) == key(tikz(body, "[arrows=dashed]")));
  CHECK(key(set("every label/.append style={swap}") + tikz("A \\arrow[r, \"f\"] & B")) == key(tikz("A \\arrow[r, \"f\"'] & B")));
  // A diagram before it is what it was, one after it has it, and the diagram's own options come last.
  CHECK(key(tikz(body) + set("arrows=dashed")) == key(tikz(body)));
  CHECK(key(set("arrows=dashed") + tikz(body, "[arrows=dotted]")) == key(tikz(body, "[arrows=dotted]")) ||
        key(set("arrows=dashed") + tikz(body, "[arrows=dotted]")).size() > 8);
}

TEST(diagrams_cells_give_every_cell_its_node_options_but_draw_fill_and_rotate_only_through_nodes) {
  const auto lines = [](const std::string& opts) { return onlyLines(rec(tikz("A & B", opts))).size(); };
  CHECK(quiet(tikz("A & B", "[cells={nodes={draw=gray}}]")));
  CHECK_EQ(lines("[cells={nodes={draw=gray}}]"), std::size_t(8));
  CHECK_EQ(lines("[cells={nodes={draw}}]"), std::size_t(8));
  CHECK_EQ(lines(""), std::size_t(0));
  // As in tikz-cd, `cells={draw}` is not read: the outline is a node option the cell has by `nodes` only.
  CHECK_EQ(lines("[cells={draw}]"), std::size_t(0));
  CHECK_EQ(lines("[cells={fill=yellow}]"), std::size_t(0));
  CHECK(check::drawsColour(check::render(tikz("A & B", "[cells={nodes={text=red}}]")), check::kRed));
  CHECK(check::drawsColour(check::render(tikz("A & B", "[cells={text=red}]")), check::kRed));
  CHECK(check::drawsColour(check::render(tikz("A & B", "[cells={red}]")), check::kRed));
  CHECK(key("\\tikzcdset{every cell/.append style={nodes={draw}}}" + tikz("A & B")) == key(tikz("A & B", "[cells={nodes={draw}}]")));
}

TEST(diagrams_center_yshift_and_crossing_over_clearance_set_where_arrows_start_and_how_wide_their_gaps_are) {
  const auto y = [](const std::string& opts) {
    const auto r = rec(tikz("A \\arrow[r] & B", opts));
    return glyphs(r)[0].y - shafts(r)[0].y;
  };
  // The anchors are that far above the baseline: the axis height unless said.
  CHECK(near(y("[center yshift=2em]"), 20));
  CHECK(near(y("[center yshift=0em]"), 0, 0.01f) || std::fabs(y("[center yshift=0em]")) < 0.01f);
  CHECK(y("[center yshift=0em]") < y(""));
  const auto gap = [](const std::string& opts) {
    int n = 0;
    for (const auto& l : onlyLines(rec(tikz("A \\arrow[dr] & B \\\\ C & D \\arrow[ul, crossing over]", opts)))) n += len(l) > 6 ? 1 : 0;
    return n;
  };
  CHECK(quiet(tikz("A \\arrow[dr] & B \\\\ C & D", "[crossing over clearance=3ex]")));
  CHECK(gap("[crossing over clearance=5ex]") <= gap(""));
}

TEST(diagrams_the_keys_it_does_not_have_are_still_warned_of_once) {
  for (const char* opt : {"opacity=0.4", "line cap=butt", "text width=1cm"}) {
    CHECK(warnsAbout(tikz(std::string("A \\arrow[r, \"f\", ") + opt + "] & B"), opt));
  }
  CHECK(warnsAbout(tikz("A \\arrow[r] & B", "[execute at end picture={}]"), "execute at end picture"));
  CHECK(warnsAbout(tikz("A \\arrow[r] & B", "[arrow style=math font]"), "arrow style"));
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

TEST(diagrams_a_cell_can_be_a_circle_or_an_ellipse_at_least_as_big_as_asked) {
  // pdflatex: `|[draw, circle, minimum size=3cm]| e` is a circle 85pt across, its arrows meeting its border.
  const auto plain = layout(tikz("|[draw]| e"));
  const auto round = layout(tikz("|[draw, circle, minimum size=3cm]| e"));
  CHECK(round.width > 4 * plain.width);
  CHECK(round.height > 4 * plain.height);
  CHECK(std::fabs(round.width - round.height) < 3);
  // An outline of many short lines, where a box has four.
  CHECK(check::of(round, DrawRecord::LINE).size() >= 64);
  CHECK_EQ(check::of(plain, DrawRecord::LINE).size(), std::size_t(4));
  // Of the text and its inner sep when no size is asked: a circle is wider than the box round the letter.
  CHECK(layout(tikz("|[draw, circle]| e")).width >= plain.width);
  CHECK(quiet(tikz("|[draw, ellipse, minimum width=2cm, minimum height=1cm]| e")));
}

TEST(diagrams_a_cells_font_sets_its_text_in_that_size) {
  CHECK(layout(tikz("|[font=\\Huge]| e")).height > layout(tikz("e")).height + 3);
}

TEST(diagrams_an_empty_group_after_an_arrows_options_is_nothing) {
  CHECK(quiet(tikz("e \\arrow[loop, out=60, in=120]{}")));
}

TEST(diagrams_distance_is_how_far_the_controls_of_a_curve_stand_from_its_ends) {
  // `distance` is both `min distance` and `max distance` in TikZ: a loop of looseness 8 over a short chord is
  // held to it, so a smaller distance is a smaller loop.
  const auto height = [](const std::string& distance) {
    return layout(tikz("e \\arrow[loop, out=60, in=120" + distance + "]")).height;
  };
  CHECK(height(", distance=3em") < height(", distance=8em"));
}

// arrows.meta's Latex and Stealth on a tikz-cd arrow: a polygon, filled (or outlined when open), of pgf's size
// ((3 + 4.5 lw) pt long: 4.8pt for 0.4pt lines) or the one asked for, with the shaft stopping at it.
TEST(diagrams_arrows_meta_tips_are_polygons_the_shaft_stops_at) {
  const auto count = [&](const std::string& tex, DrawRecord::Type type) {
    std::size_t n = 0;
    for (const auto& r : rec(tex)) n += r.type == type ? 1 : 0;
    return n;
  };
  const auto width = [&](const std::string& tex) {
    float lo = 1e9f, hi = -1e9f;
    for (const auto& r : rec(tex)) {
      if (r.type != DrawRecord::PATH && r.type != DrawRecord::STROKE_PATH) continue;
      for (const auto& s : r.path_segments) {
        if (s.cmd == DrawRecord::PathSegment::CLOSE) continue;
        lo = std::min(lo, s.coords[0]);
        hi = std::max(hi, s.coords[0]);
      }
    }
    return hi - lo;
  };
  const std::string plain = tikz("A \\arrow[r] & B");
  const std::string latex = tikz("A \\arrow[r, -Latex] & B");
  CHECK(quiet(latex));
  CHECK(quiet(tikz("A \\arrow[r, arrows={-Stealth}] & B")));
  CHECK_EQ(count(plain, DrawRecord::PATH), std::size_t(0));
  CHECK_EQ(count(latex, DrawRecord::PATH), std::size_t(1));
  CHECK_EQ(count(tikz("A \\arrow[r, Stealth-Stealth] & B"), DrawRecord::PATH), std::size_t(2));
  CHECK_EQ(count(tikz("A \\arrow[r, -{Latex[open]}] & B"), DrawRecord::PATH), std::size_t(0));
  CHECK(count(tikz("A \\arrow[r, -{Latex[open]}] & B"), DrawRecord::STROKE_PATH) >= 1);
  // the length asked for
  const float shortTip = width(latex), longTip = width(tikz("A \\arrow[r, -{Latex[length=10pt]}] & B"));
  CHECK(shortTip > 3.5f && shortTip < 6.5f);
  CHECK(longTip > 8.f && longTip < 11.f);  // (the polygon is a little shorter than the length: the outline is half a line width outside it)
  CHECK(warnsAbout(tikz("A \\arrow[r, -{Latex[nosuchkey]}] & B"), "not supported"));
}

// A cell that is a circle or an ellipse is filled as one: a path, not the rectangle round it.
TEST(diagrams_a_round_cell_is_filled_round) {
  const auto round = layout(tikz("|[circle, fill=yellow]| A"));
  CHECK_EQ(check::of(round, DrawRecord::FILL_RECT).size(), std::size_t(0));
  CHECK(!check::of(round, DrawRecord::PATH).empty());
  CHECK(!check::of(layout(tikz("|[fill=yellow]| A")), DrawRecord::FILL_RECT).empty());
}

// A `\\` at the end of the last row starts a row of its own, as pgf's matrix has it (LuaLaTeX: 14.5pt without, 26.8 with),
// and a body with nothing in it is no row at all.
TEST(diagrams_a_final_row_break_opens_a_row_and_an_empty_body_none) {
  const auto with = layout(tikz("A \\arrow[r] & B \\\\"));
  const auto without = layout(tikz("A \\arrow[r] & B"));
  CHECK(with.height + with.depth > without.height + without.depth + 8);
  CHECK_EQ(layout(tikz("")).width, 0);
}

// `[len]` after a `\\` is extra space there; the text of the first row, which no `\\` precedes, is not.
TEST(diagrams_a_bracket_at_the_start_of_the_first_row_is_text) {
  const auto bracket = layout(tikz("{[1]} \\arrow[r] & B"));
  const auto plain = layout(tikz("{} \\arrow[r] & B"));
  CHECK(bracket.height <= plain.height + 12);
  CHECK(quiet(tikz("A \\arrow[r] & B \\\\[1cm] C \\arrow[r] & D")));
  CHECK(layout(tikz("A \\arrow[r] & B \\\\[1cm] C \\arrow[r] & D")).height >
        layout(tikz("A \\arrow[r] & B \\\\ C \\arrow[r] & D")).height + 20);
}

// A bare number is points for `line width`, a length with its unit is that length: `1cm` is not `1pt`.
TEST(diagrams_a_line_width_with_a_unit_keeps_it) {
  const auto thickest = [](const std::string& value) {
    float w = 0.f;
    for (const auto& l : onlyLines(rec(tikz("A \\arrow[r, line width=" + value + "] & B")))) w = std::max(w, l.line_width);
    return w;
  };
  CHECK(thickest("1cm") > 20 * thickest("1"));
  CHECK(std::fabs(thickest("1cm") - thickest("28.4528pt")) < 0.5f);
  CHECK(std::fabs(thickest("2") - thickest("2pt")) < 0.01f);
}
