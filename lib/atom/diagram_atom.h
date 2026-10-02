#pragma once

// DiagramAtom: a commutative diagram -- objects in a grid, joined by arrows
// with labels -- as amscd's CD and tikz-cd's tikzcd draw one. Both readers
// (macro/macro_diagram.h) fill the same model, which is laid out as tikz-cd
// does: columns and rows sized by their cells plus a separation, every arrow
// running between two cells' anchors (their centre, at the math axis) and
// clipped at the cells' boxes. The arrows are drawn as line segments, heads
// and tails included, so every device draws them with no more than lines,
// and a dashed one is made of short solid ones.

#include <string>
#include <vector>

#include "atom/atom.h"
#include "box/box.h"
#include "env/units.h"

namespace microtex {

struct DiagramLabel {
  sptr<Atom> body;
  /** Which side of the arrow's direction of travel the label sits on, or
   *  centred on it, which interrupts the arrow. */
  enum class Side { left, right, center } side = Side::left;
  /** Where along the arrow, from 0 (its start) to 1. */
  float pos = 0.5f;
};

struct DiagramArrow {
  int row = 0, col = 0;
  int toRow = 0, toCol = 0;
  enum class Head { none, one, two, implies } head = Head::one;
  /** At its start: a hook (on the left of the way it goes, or the right), a
   *  tail, a bar, or a head, which an arrow that goes both ways has. */
  enum class Tail { none, hook, hookBack, tail, bar, head, implies } tail = Tail::none;
  /** The path is shortened by this much at its start and its end. */
  Dimen shortenStart, shortenEnd;
  /** Two parallel lines: Rightarrow, equal. */
  bool doubled = false;
  /** Draws no shaft, but its labels. */
  bool phantom = false;
  enum class Dash { solid, dashed, dotted } dash = Dash::solid;
  /** `bend left` is positive, in degrees. */
  float bend = 0.f;
  /** `shift left`, in units of tikz-cd's default 0.56ex; negative is right. */
  float shift = 0.f;
  /** The arrow is at least this long between the two cells' boxes, which
   *  widens the gap between the columns it crosses (amscd's arrows). */
  Dimen minLength;
  /** With `minLength`: longer if a label needs it. */
  bool fitLabels = false;
  /** The arrow is this long, centred between its cells (amscd's vertical
   *  arrows); none when invalid. */
  Dimen fixedLength;
  std::vector<DiagramLabel> labels;
};

class DiagramAtom : public Atom {
public:
  /** The cells, row by row; an empty cell is nullptr. */
  std::vector<std::vector<sptr<Atom>>> cells;
  std::vector<DiagramArrow> arrows;
  /** The least distance between the boxes of two rows, and of two columns. */
  Dimen rowSep, colSep;
  /** The least distance between the baselines of two rows. */
  Dimen minRowPitch;
  /** The space inside a cell's box around its content. */
  Dimen innerX, innerY;

  DiagramAtom();

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex
