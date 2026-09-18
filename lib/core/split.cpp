#include "core/split.h"

#include "utils/bidi.h"
#include "box/box_group.h"
// glue.h only forward-declares GlueBox; justifyLine() needs the
// definition to reach _stretch and _width.
#include "box/box_single.h"
#include "utils/log.h"

#include <algorithm>
#include <limits>
#include <vector>

using namespace std;
using namespace microtex;

#ifdef HAVE_LOG

static void printBox(const sptr<Box>& b, int dep, vector<bool>& lines, int max = 0) {
  logv("%-4d", dep);
  if (lines.size() < dep + 1) lines.resize(dep + 1, false);

  for (int i = 0; i < dep - 1; i++) {
    logv(lines[i] ? "    " : " │  ");
  }

  if (dep > 0) {
    logv(lines[dep - 1] ? " └──" : " ├──");
  }

  if (b == nullptr) {
    logv(ANSI_COLOR_RED " NULL\n" ANSI_RESET);
    return;
  }

  const vector<sptr<Box>>& children = b->descendants();
  const auto size = children.size();
  const auto& name = b->name();
  const char* fmt = (size > 0 ? ANSI_COLOR_CYAN " %-*s " ANSI_RESET : " %-*s ");
  logv(fmt, size > 0 ? name.size() : max, name.c_str());
  // show metrics and additional info
  logv(
    "[%g, (%g + %g) = %g, %g] %s\n",
    b->_width,
    b->_height,
    b->_depth,
    b->vlen(),
    b->_shift,
    b->toString().c_str()
  );
  if (size == 0) return;

  size_t limit = 0;
  for (const auto& x : children) {
    limit = std::max(limit, x->name().size());
  }

  for (size_t i = 0; i < size; i++) {
    lines[dep] = i == size - 1;
    printBox(children[i], dep + 1, lines, limit);
  }
}

void microtex::printBox(const sptr<Box>& box) {
  vector<bool> lines;
  ::printBox(box, 0, lines, box->name().size());
  logv("\n");
}

#endif  // HAVE_LOG

bool BoxSplitter::_justify = false;
bool BoxSplitter::_optimalBreak = false;

std::vector<float> BoxSplitter::enumerateBreakWidths(const sptr<HBox>& hb) {
  std::vector<float> out;
  // canBreak() reports a break only where the content overruns the
  // ceiling it is given, so start just inside the natural width to see
  // the final break, then step the ceiling down past each one in turn.
  float ceiling = hb->_width - PREC;
  // Bounded: a malformed or pathological box must not spin here.
  for (int guard = 0; guard < 4096 && ceiling > 0; guard++) {
    std::stack<Position> positions;
    const float w = canBreak(positions, hb, ceiling);
    if (w >= hb->_width) break;                   // nothing breakable left
    if (!out.empty() && w >= out.back()) break;   // not advancing; stop
    out.push_back(w);
    ceiling = w - PREC;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

std::vector<float> BoxSplitter::optimalLineTargets(const sptr<HBox>& hb, float width) {
  if (width <= 0) return {};
  const std::vector<float> breaks = enumerateBreakWidths(hb);
  if (breaks.empty()) return {};

  // Node 0 is the start of the paragraph, 1..m the breaks, m+1 the end.
  std::vector<float> pos;
  pos.reserve(breaks.size() + 2);
  pos.push_back(0.f);
  for (const float b : breaks) pos.push_back(b);
  pos.push_back(hb->_width);
  const int n = static_cast<int>(pos.size());

  const float INF = std::numeric_limits<float>::max();
  std::vector<float> best(n, INF);
  std::vector<int> prev(n, -1);
  best[0] = 0.f;

  for (int i = 1; i < n; i++) {
    for (int j = 0; j < i; j++) {
      if (best[j] == INF) continue;
      const float w = pos[i] - pos[j];
      if (w > width + PREC) continue;  // that line would not fit
      // TeX's demerits, in miniature. Badness grows with the cube of how
      // far short the line falls, so one very loose line costs more than
      // several slightly loose ones -- which is what breaks up greedy's
      // ragged shape. Squaring (linePenalty + badness) then makes each
      // extra line carry a cost of its own, so the paragraph is not
      // allowed to grow a line merely to even out the others.
      //
      // The last line is free: TeX does not penalise a short final line.
      float cost = 0.f;
      if (i != n - 1) {
        const float slack = (width - w) / width;
        const float badness = 100.f * slack * slack * slack;
        const float demerits = 10.f + badness;
        cost = demerits * demerits;
      }
      if (best[j] + cost < best[i]) {
        best[i] = best[j] + cost;
        prev[i] = j;
      }
    }
  }
  if (best[n - 1] == INF) return {};  // nothing feasible; caller falls back

  std::vector<float> targets;
  for (int i = n - 1; i > 0; i = prev[i]) {
    if (prev[i] < 0) return {};
    targets.push_back(pos[i] - pos[prev[i]]);
  }
  std::reverse(targets.begin(), targets.end());
  // The final line takes whatever is left, so it needs no target.
  if (!targets.empty()) targets.pop_back();
  return targets;
}

// Flatten a line into its boxes, left to right, descending only through
// HBoxes. Stopping at any other box type keeps justification to the
// spaces between words on the line: the spaces inside a fraction or a
// matrix cell belong to that construct's own layout, and stretching them
// would pull it apart.
static void collectLineLeaves(const sptr<Box>& b, std::vector<sptr<Box>>& out) {
  auto h = std::dynamic_pointer_cast<HBox>(b);
  if (h == nullptr) {
    out.push_back(b);
    return;
  }
  for (const auto& child : h->_children) collectLineLeaves(child, out);
}

// Re-sum HBox widths after their glue has been widened. HBox::_width is
// maintained as the sum of its children (see HBox::recalculate), so the
// same restricted descent that found the glue can put the widths right.
static float recomputeHBoxWidth(const sptr<Box>& b) {
  auto h = std::dynamic_pointer_cast<HBox>(b);
  if (h == nullptr) return b->_width;
  float w = 0;
  for (const auto& child : h->_children) w += recomputeHBoxWidth(child);
  h->_width = w;
  return w;
}

bool BoxSplitter::justifyLine(const sptr<Box>& line, float width) {
  if (width <= 0 || line == nullptr) return false;

  std::vector<sptr<Box>> leaves;
  collectLineLeaves(line, leaves);

  // A line ends at its last piece of ink. The break leaves the space it
  // broke at sitting on the end of the line, and TeX drops that space
  // rather than stretching it -- keeping it would push the visible text
  // short of the margin by the width of a stretched space, so the line
  // would measure right but still look ragged.
  int last = -1;
  for (int i = static_cast<int>(leaves.size()) - 1; i >= 0; i--) {
    if (!leaves[i]->isSpace()) {
      last = i;
      break;
    }
  }
  if (last < 0) return false;  // nothing but spaces

  float trailing = 0;
  std::vector<sptr<GlueBox>> glue;
  for (int i = 0; i < static_cast<int>(leaves.size()); i++) {
    auto g = std::dynamic_pointer_cast<GlueBox>(leaves[i]);
    if (g == nullptr) continue;
    if (i > last) {
      trailing += g->_width;
      g->_width = 0;
    } else {
      glue.push_back(g);
    }
  }

  // Never squeeze: shrinking is what makes justified text look cramped,
  // and a line already at or past the measure has nothing to give.
  const float slack = width - (line->_width - trailing);
  if (slack <= PREC || glue.empty()) {
    if (trailing > 0) recomputeHBoxWidth(line);
    return trailing > 0;
  }

  float total = 0;
  for (const auto& g : glue) total += g->_stretch;
  if (total <= PREC) {
    if (trailing > 0) recomputeHBoxWidth(line);
    return trailing > 0;
  }

  for (const auto& g : glue) {
    g->_width += slack * (g->_stretch / total);
  }
  recomputeHBoxWidth(line);
  return true;
}

std::pair<bool, sptr<Box>> BoxSplitter::splitDispatch(
  const sptr<Box>& b,
  float width,
  float lineSpace,
  int depth
) {
  if (depth > MAX_SPLIT_DEPTH) return {false, b};
  auto h = dynamic_pointer_cast<HBox>(b);
  if (h != nullptr) return split(h, width, lineSpace);
  auto v = dynamic_pointer_cast<VBox>(b);
  if (v != nullptr) return split(v, width, lineSpace, depth);
  return {false, b};
}

std::pair<bool, sptr<Box>> BoxSplitter::split(
  const sptr<VBox>& vb,
  float width,
  float lineSpace,
  int depth
) {
  if (width <= 0) return {false, vb};

  // Cheap rejection: if every row already fits there is nothing to do,
  // and the box is returned untouched so unsplit content keeps its
  // original object identity and metrics exactly.
  bool needsSplit = false;
  for (const auto& child : vb->_children) {
    if (child->_width > width) {
      needsSplit = true;
      break;
    }
  }
  if (!needsSplit) return {false, vb};

  // A VBox is positioned by its height/depth split, not just its total
  // extent, and callers such as MatrixAtom::createBox overwrite both to
  // centre the box on the math axis after building it. Recover that
  // offset now so it can be reapplied to the taller box below --
  // rebuilding without it silently shifts the baseline of every
  // multi-row formula.
  const float oldTotal = vb->_height + vb->_depth;
  const float axis = vb->_height - oldTotal / 2;

  // NOTE ON REACH: this splits rows that are plain HBoxes, which covers
  // content separated by `\\` at the top level. It deliberately does NOT
  // reach inside matrix/array cells, and so does not wrap long items in
  // itemize/enumerate/align/gather/tabular.
  //
  // Those rows hold WrapperBoxes (see MatrixAtom::createBox), whose
  // _height/_depth are the *row's* metrics, precomputed by
  // recalculateLine() before the VBox exists; MatrixAtom then overwrites
  // the row HBox metrics too. Breaking a cell would therefore have to
  // re-derive every row height and push the new metrics back up through
  // the WrapperBox and row box -- a change inside the matrix layout
  // itself, affecting every table, matrix, cases and align in the
  // package. Callers that need prose to wrap inside list items should
  // stack the items themselves and give each one its own max_width.
  auto out = sptrOf<VBox>();
  bool splitted = false;
  for (const auto& child : vb->_children) {
    if (child->_width > width) {
      auto [childSplit, newChild] = splitDispatch(child, width, lineSpace, depth + 1);
      if (childSplit) splitted = true;
      out->add(newChild);
    } else {
      // Interline struts and rows that already fit pass through as-is.
      out->add(child);
    }
  }

  // Nothing actually broke -- a row can be wider than the limit yet have
  // no legal break position. Keep the original box rather than an
  // equivalent copy.
  if (!splitted) return {false, vb};

  const float newTotal = out->_height + out->_depth;
  out->_height = newTotal / 2 + axis;
  out->_depth = newTotal / 2 - axis;

  return {true, std::static_pointer_cast<Box>(out)};
}

std::pair<bool, sptr<Box>> BoxSplitter::split(const sptr<Box>& b, float width, float lineSpace) {
  auto [splitted, box] = splitDispatch(b, width, lineSpace, 0);
#ifdef HAVE_LOG
  if (box != b) {
    logv("[BEFORE SPLIT]:\n");
    printBox(b);
    logv("[AFTER SPLIT]:\n");
    printBox(box);
  } else {
    logv("[BOX TREE]:\n");
    printBox(box);
  }
#endif
  return {splitted, box};
}

// Put one finished line into visual order.
//
// Lines are built in logical order -- that is the order the breaks have
// to be chosen in -- and only then reordered, which is what the Unicode
// algorithm prescribes. Does nothing unless the row carried levels, i.e.
// unless something in it was right-to-left.
void BoxSplitter::reorderLine(const sptr<Box>& line) {
  auto h = std::dynamic_pointer_cast<HBox>(line);
  if (h == nullptr) return;
  if (!h->_childLevels.empty()) {
    microtex::bidi_reorder(h->_children, h->_childLevels);
    // Reordering is a permutation, not a normalisation: applying it twice
    // would put the line back the wrong way round. Dropping the levels once
    // they have been used makes a second call a no-op rather than a bug.
    h->_childLevels.clear();
  }
  // A child may be a whole group -- `\textbf{...}` builds its own row --
  // which the step above moves as one unit. Its contents still have to be
  // ordered among themselves, and they carry the levels their own row
  // resolved. Reversing the groups and then reversing within each group is
  // rule L2 applied at two embedding levels.
  //
  // Descending through a *vertical* box as well would not help today: no
  // structural atom (VRowAtom, FracAtom, MatrixAtom, ...) forwards
  // collectBidiText/assignBidiLevels, so rows inside one never carry
  // levels to act on. Widening the walk was tried and changed nothing.
  for (const auto& child : h->_children) {
    if (child != line) reorderLine(child);
  }
}

std::pair<bool, sptr<Box>> BoxSplitter::split(const sptr<HBox>& hb, float width, float lineSpace) {
  // A single line that already fits still has to be ordered.
  if (width == 0 || hb->_width <= width) {
    reorderLine(hb);
    return {false, hb};
  }

  // RAII: everything between here and the return below can throw, and a
  // raw owner leaked the VBox when it did (valgrind: 6 blocks from
  // BoxSplitter::split over one test run).
  auto vbox = sptrOf<VBox>();
  sptr<HBox> first, second;
  sptr<HBox> hbox = hb;
  bool splitted = false;

  // Per-line target widths when breaking by total fit. Empty means
  // greedy, in which case every line simply targets the full measure and
  // the behaviour is exactly what it was.
  const std::vector<float> targets =
    _optimalBreak ? optimalLineTargets(hb, width) : std::vector<float>();
  size_t line = 0;

  while (hbox->_width > width) {
    stack<Position> positions;
    // A target is only ever <= width, so the line this produces cannot
    // overrun the measure regardless of how the target was chosen.
    const float target = line < targets.size() ? std::min(targets[line], width)
                                               : width;
    if (canBreak(positions, hbox, target) == hbox->_width) {
      // Nothing breakable within the target. Retry at the full measure
      // before giving up, so a bad plan degrades to greedy rather than
      // leaving the rest of the paragraph unbroken.
      if (target >= width) break;
      positions = stack<Position>();
      if (canBreak(positions, hbox, width) == hbox->_width) break;
    }
    line++;
    Position pos = positions.top();
    positions.pop();
    const auto brk = pos._box->breakBoxAt(pos._index);
    auto hboxes = pos._box->split(pos._index - 1);
    first = hboxes.first;
    second = hboxes.second;
    // A break that draws something puts it on the line it ends -- the
    // hyphen of a word broken across lines. getBreakPosition() has
    // already reserved room for it, and this has to happen before
    // justifyLine() below, which stretches the line's glue to fill the
    // measure exactly and would otherwise be pushed past it.
    if (brk != nullptr) {
      // The hyphen belongs to the word it follows, so it takes that word's
      // embedding level; that is what puts it at the visual left of a
      // right-to-left line. It also has to be pushed at all: bidi_reorder
      // requires one level per child and silently does nothing when the
      // two disagree, so appending the box alone left the whole line in
      // logical order.
      if (!first->_childLevels.empty() &&
          first->_childLevels.size() == first->_children.size()) {
        first->_childLevels.push_back(first->_childLevels.back());
      }
      first->add(brk);
    }
    // A nested box joins its parent as one more child, so it needs one
    // more level or the vectors no longer line up and bidi_reorder gives
    // up on the whole line. It takes the level of its own first child --
    // the direction of the text inside it.
    auto addLevelled = [](const sptr<HBox>& parent, const sptr<HBox>& child,
                          bool atFront) {
      if (parent->_childLevels.size() != parent->_children.size()) return;
      if (parent->_childLevels.empty() && child->_childLevels.empty()) return;
      // The box's own first level when it has one. Levels are resolved
      // for the whole formula now, so a box built from a row always has
      // them; a box built any other way may not, and it still has to
      // travel with the text around it. It then takes the level of the
      // neighbour it is placed against, because level 0 would strand it:
      // rule L2 reverses runs at or above the lowest odd level, so a 0
      // sitting among 1s splits the reversal in two.
      std::uint8_t lv = 0;
      if (!child->_childLevels.empty()) {
        lv = child->_childLevels.front();
      } else if (!parent->_childLevels.empty()) {
        lv = atFront ? parent->_childLevels.front() : parent->_childLevels.back();
      }
      if (atFront) {
        parent->_childLevels.insert(parent->_childLevels.begin(), lv);
      } else {
        parent->_childLevels.push_back(lv);
      }
    };
    while (!positions.empty()) {
      pos = positions.top();
      positions.pop();
      hboxes = pos._box->splitRemove(pos._index);
      addLevelled(hboxes.first, first, false);
      hboxes.first->add(first);
      addLevelled(hboxes.second, second, true);
      hboxes.second->add(0, second);
      first = hboxes.first;
      second = hboxes.second;
    }
    // Visual order before justification: justifyLine() drops the spaces
    // that end the line and stretches the rest, and which spaces those
    // are depends on the direction.
    reorderLine(first);
    // `first` is a completed line and something follows it, so it is
    // never the last line of the paragraph -- the one case TeX leaves
    // ragged. The trailing `second` added after this loop is that line,
    // and is deliberately left alone.
    if (_justify) justifyLine(first, width);
    vbox->add(first, lineSpace);
    splitted = true;
    hbox = second;
  }

  if (second != nullptr) {
    // The last line, left ragged but still ordered.
    reorderLine(second);
    vbox->add(second, lineSpace);
    return {splitted, vbox};
  }

  return {splitted, hbox};
}

float BoxSplitter::canBreak(stack<Position>& s, const sptr<HBox>& hbox, const float width) {
  const vector<sptr<Box>>& children = hbox->_children;
  const int count = children.size();
  // Cumulative width
  auto* cumWidth = new float[count + 1]();
  cumWidth[0] = 0;
  for (int i = 0; i < count; i++) {
    auto box = children[i];
    cumWidth[i + 1] = cumWidth[i] + box->_width;
    if (cumWidth[i + 1] <= width) continue;
    int pos = getBreakPosition(hbox, i, cumWidth, width);
    auto h = dynamic_pointer_cast<HBox>(box);
    if (h != nullptr) {
      stack<Position> sub;
      float w = canBreak(sub, h, width - cumWidth[i]);
      if (w != box->_width && (cumWidth[i] + w <= width || pos == -1)) {
        s.push(Position(i - 1, hbox));
        // add to stack
        vector<Position> p;
        while (!sub.empty()) {
          p.push_back(sub.top());
          sub.pop();
        }
        for (auto it = p.rbegin(); it != p.rend(); it++) s.push(*it);
        // release cum-width
        float x = cumWidth[i] + w;
        delete[] cumWidth;
        return x;
      }
    }

    if (pos != -1) {
      s.push(Position(pos, hbox));
      float x = cumWidth[pos];
      delete[] cumWidth;
      return x;
    }
  }

  delete[] cumWidth;
  return hbox->_width;
}

int BoxSplitter::getBreakPosition(
  const sptr<HBox>& hb,
  int i,
  const float* cumWidth,
  float width
) {
  const auto& bp = hb->_breakPositions;
  if (bp.empty()) return -1;

  // The last recorded break at or before i. Positions are appended in
  // increasing order as the row is built, and split() preserves that.
  int k = -1;
  for (size_t j = 0; j < bp.size(); j++) {
    if (bp[j] > i) break;
    k = static_cast<int>(j);
  }
  if (k < 0) return -1;

  // A break that draws something -- a hyphen -- makes the line it ends
  // wider than the content preceding it, so it has to be paid for *here*,
  // where the break is chosen, not where the line is built. Otherwise
  // every hyphenated line overruns the measure by the width of its own
  // hyphen. Back off to an earlier break when it will not fit.
  //
  // A plain break records no box, takes the first branch, and keeps
  // exactly the position it has always had.
  for (int j = k; j >= 0; j--) {
    const auto extra = hb->breakBoxAt(bp[j]);
    if (extra == nullptr) return bp[j];
    if (cumWidth[bp[j]] + extra->_width <= width) return bp[j];
  }
  // Every candidate is a hyphen and none of them fits, which happens when
  // the measure is narrower than the shortest fragment plus its hyphen.
  // Take the last one regardless: an overfull line is bad, but refusing
  // to break leaves the whole unbroken word sticking out, which is worse.
  return bp[k];
}
