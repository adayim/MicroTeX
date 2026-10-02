#pragma once

// DiagramAtom: a commutative diagram -- objects in a grid, joined by arrows
// with labels -- as amscd's CD and tikz-cd's tikzcd draw one. Both readers
// (macro/macro_diagram.h) fill the same model, which is laid out as tikz-cd
// does: columns and rows sized by their cells plus a separation, every arrow
// running between two cells' anchors (their centre, at the math axis) and
// clipped at the cells' boxes. The arrows are drawn as line segments, heads
// and tails included, so every device draws them with no more than lines,
// and a dashed one is made of short solid ones.

#include <functional>
#include <string>
#include <vector>

#include "atom/atom.h"
#include "box/box.h"
#include "env/units.h"
#include "graphic/graphic_basic.h"

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
  /** At its end: a head, two, the double arrow's, a harpoon's one barb (on
   *  the left of the way it goes, or the right), or a bar. */
  enum class Head { none, one, two, implies, harpoonLeft, harpoonRight, bar } head = Head::one;
  /** At its start: a hook (on the left of the way it goes, or the right), a
   *  tail, a bar, or a head, which an arrow that goes both ways has. */
  enum class Tail { none, hook, hookBack, tail, bar, head, implies } tail = Tail::none;
  /** The path is shortened by this much at its start and its end. */
  Dimen shortenStart, shortenEnd;
  /** Two parallel lines: Rightarrow, equal. */
  bool doubled = false;
  /** Draws no shaft, but its labels. */
  bool phantom = false;
  /** A zigzag shaft. */
  bool squiggly = false;
  /** Drawn over the arrows before it, which are cut where it crosses them. */
  bool crossing = false;
  /** Its own colour, for the lines and the labels. */
  bool hasColor = false;
  color ink = 0;
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
  /** tikz-cd's `cramped`: less room in the cells and none around the diagram. */
  bool cramped = false;
  /** amscd's way of setting rows: object rows and rows of arrows alternate, TeX's
   *  baselineskip and lineskip apart, with labels in the rows' heights and depths. */
  bool texRows = false;

  DiagramAtom();

  sptr<Box> createBox(Env& env) override;
};

/** An arrow KaTeX has that the font has no stretching glyph for (\xtwoheadrightarrow,
 *  \xtwoheadleftarrow, \xlongequal, \xtofrom), drawn the way a diagram's are, as long
 *  as `length` says when it is set. It stands where \xrightarrow's glyph does. */
class StretchArrowAtom : public Atom {
public:
  enum class Kind { twoHeadRight, twoHeadLeft, longEqual, toFrom };

  StretchArrowAtom(Kind kind, std::function<float(const Env&)>&& length)
      : _kind(kind), _length(std::move(length)) {
    _type = AtomType::relation;
  }

  sptr<Box> createBox(Env& env) override;

private:
  Kind _kind;
  std::function<float(const Env&)> _length;
};

/** KaTeX's drawn enclosures: `\overlinesegment`, `\underlinesegment`, `\angl`,
 *  `\phase` and `\textcircled`, strokes around the box of their content. */
class EncloseAtom : public Atom {
public:
  enum class Kind { overSegment, underSegment, angle, phase, circle, harpoonLeft, harpoonRight, doubleArrow };

  EncloseAtom(Kind kind, const sptr<Atom>& base) : _kind(kind), _base(base) {}

  sptr<Box> createBox(Env& env) override;

private:
  Kind _kind;
  sptr<Atom> _base;
};

}  // namespace microtex
