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
  /** TikZ's own placement, when the label says `above`, `left`, `anchor=north`: the label's compass point
   *  that stands on the path's point. `none` is tikz-cd's `auto`, `side` above. */
  enum class Anchor { none, center, north, south, east, west, northEast, northWest, southEast, southWest } anchor = Anchor::none;
  /** The space inside the label's node (0.5ex when not valid). */
  Dimen innerSep;
  /** `xshift`, `yshift` of the label's node. */
  Dimen xshift, yshift;
  /** Turned along the path (`sloped`), and by `rotate` more. */
  bool sloped = false;
  float rotate = 0.f;
  /** Set at the size of the text around, not as a script: `phantom` and `marking` say `font=`. */
  bool fullSize = false;
  /** Cuts the arrow it is on (`description`), which `hasFill` also does, as a box of the background colour. */
  bool interrupts = false;
  /** Its text at this multiple of the script size (`font=\Large`). */
  float fontScale = 1.f;
  /** A node that is a point: no text box, no inner sep (`coordinate`). */
  bool point = false;
  bool hasInk = false;
  color ink = 0;
  bool hasFill = false;
  color fill = 0;
  /** `draw` (in `drawInk` when it names a colour). */
  bool hasDraw = false;
  bool hasDrawInk = false;
  color drawInk = 0;
  /** Another arrow's `from=` or `to=` may name it. */
  std::string name;
};

/** An arrows.meta tip (`Latex`, `Stealth`) at an end of an arrow, as tikz-cd takes `-Latex`, `Stealth-Stealth`,
 *  `-{Latex[length=3mm, open]}`: a polygon, filled, outlined or both, the sizes pgf's own unless given. */
struct MetaTip {
  enum class Kind { none, latex, stealth } kind = Kind::none;
  bool open = false, round = false;
  /** Not valid: pgf's. */
  Dimen length, width, inset, lineWidth;
  float scale = 1.f;
};

struct DiagramArrow {
  int row = 0, col = 0;
  int toRow = 0, toCol = 0;
  /** At its end: a head, two, the double arrow's, a harpoon's one barb (on
   *  the left of the way it goes, or the right), a bar, or what tikz-cd's
   *  `hookleftarrow` and `leftarrowtail` end in: a hook (left, right) or a tail. */
  enum class Head { none, one, two, implies, harpoonLeft, harpoonRight, bar, hook, hookBack, tail } head = Head::one;
  /** At its start: a hook (on the left of the way it goes, or the right), a
   *  tail, a bar, or a head, two heads or a harpoon, which an arrow that goes
   *  both ways, `twoheadleftarrow` or `leftharpoonup` has. */
  enum class Tail { none, hook, hookBack, tail, bar, head, implies, two, harpoonLeft, harpoonRight } tail = Tail::none;
  /** An arrows.meta tip instead of `head` or `tail` (which are then none). */
  MetaTip headTip, tailTip;
  /** The path is shortened by this much at its start and its end (a negative one lengthens it). */
  Dimen shortenStart, shortenEnd;
  /** Two parallel lines: Rightarrow, equal. */
  bool doubled = false;
  /** TikZ's `double`: the space between the two lines (not valid: 0.0969em, that of `Rightarrow`). */
  Dimen doubleInner;
  /** Draws no shaft, but its labels. */
  bool phantom = false;
  /** A zigzag shaft. */
  bool squiggly = false;
  /** Drawn over the arrows before it, which are cut where it crosses them. */
  bool crossing = false;
  /** Its own colour, for the lines and the labels. */
  bool hasColor = false;
  color ink = 0;
  enum class Dash { solid, dashed, dotted, custom } dash = Dash::solid;
  /** `custom`: TikZ's `dash pattern`, on and off, in this order, repeating. */
  std::vector<Dimen> pattern;
  /** `line width`; not valid: the math font's rule thickness. */
  Dimen lineWidth;
  /** `bend left` is positive, in degrees. */
  float bend = 0.f;
  /** `in` and `out`, in degrees from the east, anticlockwise; `looseness` multiplies how far the curve's
   *  controls stand from its ends; `min distance` is the least. */
  bool hasOut = false, hasIn = false;
  float outAngle = 0.f, inAngle = 0.f;
  float looseness = 1.f;
  Dimen minDistance;
  /** `max distance`: the most (`distance` sets both). */
  Dimen maxDistance;
  /** `controls={+(x,y) and +(x,y)}`: the controls, each from its own end. */
  bool hasControls = false;
  Dimen control1x, control1y, control2x, control2y;
  /** `to path=|- (\tikztotarget)` and `-|`: a right angle, the first leg vertical or horizontal;
   *  `rounded corners` rounds it. */
  enum class Route { direct, verticalFirst, horizontalFirst } route = Route::direct;
  bool rounded = false;
  Dimen cornerRadius;
  /** `shift left`, in units of tikz-cd's default 0.56ex; negative is right. A dimension (`shift left=1.5ex`) is
   *  `shiftLength`, to the left of the way it goes. */
  float shift = 0.f;
  Dimen shiftLength;
  /** `xshift`, `yshift`, `shift={(x,y)}`: the arrow and its labels moved. */
  Dimen xshift, yshift;
  /** `start anchor`, `end anchor`: a point of the cell (empty: the cell's border, toward the other end). Then
   *  the `[xshift=..]` that came with it. */
  std::string startAnchor, endAnchor;
  Dimen startDx, startDy, endDx, endDy;
  /** The arrow starts or ends at a node of that name: a label another arrow made, if no cell has the name. */
  std::string fromName, toName;
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

/** The node options of a cell: `|[draw, blue, name=Z]|`, and `cells=`. */
struct DiagramCell {
  std::string name;
  bool hasDraw = false;
  bool hasDrawInk = false;
  color drawInk = 0;
  bool hasFill = false;
  color fill = 0;
  bool hasInk = false;
  color ink = 0;
  float rotate = 0.f;
  Dimen innerX, innerY;
  /** `circle` and `ellipse`: the node's outline is round, round the box its text and `inner sep` make, and at least
   *  `minimum width` and `minimum height` (`minimum size` is both) across. */
  enum class Shape { rectangle, circle, ellipse } shape = Shape::rectangle;
  Dimen minWidth, minHeight;
  /** `font=`: a size or style declaration the cell's text is set in. */
  std::string font;
};

class DiagramAtom : public Atom {
public:
  /** The cells, row by row; an empty cell is nullptr. */
  std::vector<std::vector<sptr<Atom>>> cells;
  /** Their node options, where a cell has any, in the same shape as `cells`. */
  std::vector<std::vector<DiagramCell>> cellOptions;
  /** The extra space before a column and before a row (`&[1.5em]`, `\\[1cm]`); not valid: none. */
  std::vector<Dimen> columnExtra, rowExtra;
  /** `center yshift`: how far the arrows' anchors are above a cell's baseline (not valid: the axis height). */
  Dimen centerShift;
  /** `crossing over clearance`: the width of the gap a crossing arrow cuts (not valid: 1.5ex). */
  Dimen crossingClearance;
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
  enum class Kind { twoHeadRight, twoHeadLeft, longEqual, toFrom, doubleLeft, doubleBoth };

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
