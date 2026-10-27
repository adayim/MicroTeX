#pragma once

// TikzAtom: a tikzpicture -- paths, shapes and the nodes that hold text -- laid out as TikZ does and
// drawn with the recorder's stroked and filled paths. macro/macro_tikz.h reads the picture's source
// into this model; nothing here reads LaTeX. The model keeps lengths as the source gave them (a bare
// number is centimetres, as in TikZ) and names for nodes and anchors, because a coordinate such as
// `(a.north)` or `below=of a` is known only once the node it names has been given its size, which
// takes the font: createBox() resolves everything in the order the picture is written.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "atom/atom.h"
#include "box/box.h"
#include "env/units.h"
#include "graphic/graphic_basic.h"

namespace microtex {
namespace tikz {

struct CalcExpr;

/** A point of a picture, as written: `(1,2)`, `(30:2cm)`, `(a)`, `(a.north)`, `+(1,0)`. */
struct Coord {
  enum class Kind { xy, polar, node, calc, cross } kind = Kind::xy;
  Dimen x, y;                    // xy
  float angle = 0.f;             // polar: degrees
  Dimen r, r2;                   // polar: the radius, and the second one of an ellipse
  std::string name, anchor;      // node: its name, and the anchor after the dot
  std::shared_ptr<CalcExpr> calc;  // calc: `($ ... $)`
  std::shared_ptr<Coord> first, second;  // cross: `(a |- b)` is the x of a and the y of b, `(a -| b)` the x of b and the y of a
  bool xFromFirst = true;
  int relative = 0;              // 0 absolute, 1 `+` (the base stays), 2 `++` (the base moves)
};

/** One `!` of a calculation: `(a)!0.3!(b)` (a fraction of the way), `(a)!1cm!(b)` (a distance towards), `(a)!(p)!(b)`
 *  (the foot of p on the line), the target optionally turned about the point first (`!30:(b)`). */
struct CalcStep {
  enum class Kind { fraction, distance, project } kind = Kind::fraction;
  double fraction = 0;
  Dimen distance;
  Coord via;                     // project: the point that is projected
  Coord to;
  bool rotated = false;
  float angle = 0.f;
};

/** A signed, scaled coordinate with its steps; a calculation is a sum of them. */
struct CalcTerm {
  double factor = 1;
  Coord first;
  std::vector<CalcStep> steps;
};

struct CalcExpr {
  std::vector<CalcTerm> terms;
};

/** One of `draw`, `fill`: not said, off, in the current colour, or in a colour of its own. */
struct Paint {
  enum class Mode : char { unset, none, current, color } mode = Mode::unset;
  color c = 0;
};

/** A step of a coordinate transformation; each applies to the coordinates first, after those before it. */
struct XStep {
  enum class Kind { shift, scale, xscale, yscale, rotate, xshift, yshift } kind = Kind::shift;
  float v = 0.f;                 // scale factors, degrees
  Dimen dx, dy;                  // shifts
};

/** An arrow tip, `->` and kin, at one end of a path. `to` is TikZ's default (Computer Modern's arrow),
 *  `latex` and `stealth` the old tips, `newLatex` and `newStealth` arrows.meta's. */
struct Tip {
  enum class Kind { none, to, stealth, latex, newStealth, newLatex, triangleMeta, triangle45, bar, circle } kind = Kind::none;
  bool open = false, round = false;
  bool defaultTip = false;                       // `>` and `<`: whichever `>=` says, when the path is laid out
  std::optional<Dimen> length, width, inset, lineWidth;
  float scale = 1.f;
};

/** `2cm`, or `1cm and 2cm`: the first is vertical, as in `node distance`. */
struct Dist {
  Dimen y, x;
  bool pair = false;
};

/** How a `to` or an `edge` runs from one point to the other: straight, or a curve. */
struct ToSpec {
  std::optional<float> bend;                     // degrees, positive to the left
  std::optional<float> out, in;                  // absolute angles at the two ends
  float looseness = 1.f;
  bool loop = false;
  std::optional<Dimen> minDistance;
  bool curved() const { return bend || out || in || loop; }
};

/** The tip a name stands for (`to`, `stealth`, `Latex[length=3mm]` ...); false when there is none. */
bool tipByName(std::string name, Tip& tip);

struct Style {
  Paint draw, fill;
  std::optional<color> current;                  // `color=`, or a colour's name alone
  std::optional<Dimen> lineWidth;
  std::optional<std::vector<Dimen>> dash;        // on, off, on, off ...
  std::optional<Dimen> dashPhase;
  std::optional<int> cap, join;                  // microtex::Cap, Join
  std::optional<float> drawOpacity, fillOpacity;
  std::optional<Dimen> rounded;                  // rounded corners; zero is none
  std::vector<XStep> transform;
  std::optional<Tip> tipStart, tipEnd;
  std::optional<Dimen> shortenStart, shortenEnd;
  std::string defaultTip;                        // `>=stealth`
  std::optional<Dimen> step, xstep, ystep;       // grid
  ToSpec to;
  // Nodes.
  std::string shape;                             // rectangle, circle, ... ("" is the default)
  std::optional<float> aspect;                   // `aspect=`, `shape aspect=`: a diamond's width over its height
  std::optional<float> arcLength;                // `rounded rectangle arc length=`, degrees
  std::optional<int> polySides, starPoints;      // `regular polygon sides=`, `star points=`
  std::optional<double> starRatio;               // `star point ratio=`
  std::optional<Dimen> starHeight;               // `star point height=` (the last of the two to be given is used)
  bool starByHeight = false;
  std::optional<float> borderRotate;             // `shape border rotate=`, degrees
  std::optional<float> apexAngle;                // `isosceles triangle apex angle=`, degrees
  bool borderIncircle = false;                   // `shape border uses incircle` (pgf: false unless said)
  float nodeScaleX = 1.f, nodeScaleY = 1.f;      // a node's own `scale`, `xscale`, `yscale`: the node, not where it is put
  // `\clip` and `clip`: the clip paths in force, by id (a PathItem's `clipId`); a scope ends them with its group.
  std::vector<int> clips;
  bool clipPath = false;                         // the `clip` option: the path is also a clip path
  std::optional<Dimen> innerX, innerY, outerSep;
  std::optional<Dimen> minWidth, minHeight;
  std::optional<Dimen> textWidth;
  std::optional<char> align;                     // l, c, r (and j)
  std::optional<color> textColor;
  std::optional<float> textOpacity;
  std::string anchor;                            // anchor=: which point of the node is put at the position
  std::string placement;                         // above, below left, ...
  std::optional<Dist> placeDist;                 // `right=2cm of a`
  std::string placeOf;                           // the node `of` which it is put
  bool placeCentres = false;                     // `right of=a`: centre to centre
  std::optional<Dist> nodeDist;                  // `node distance=1cm and 2cm`
  std::string font;                              // `font=\small`
  std::optional<float> pos;                      // `pos=`, `midway` ...: along the segment before a node
  bool sloped = false, autoSide = false, swap = false;
  std::optional<float> rotate;                   // a node's own rotation
  // `label=` and `pin=` as written (pin: true), read into a Node's labels; the distances they use.
  std::vector<Coord> fit;                        // `fit=(a)(b)`: the points a node is put round
  // `matrix of nodes`: how the cells are set, and what the matrix says of them.
  int matrixKind = 0;                            // 1 `matrix of nodes`, 2 `matrix of math nodes`
  std::optional<Dimen> rowSep, columnSep;
  bool rowBetweenOrigins = false, columnBetweenOrigins = false;
  std::string nodesStyle, cellsStyle;            // `nodes=`, `cells=`
  bool emptyNodes = false;                       // `nodes in empty cells`
  // Trees (`child`): where a child is put, and how its edge runs.
  std::optional<Dimen> levelDistance, siblingDistance;
  std::optional<float> growAngle;                // `grow=`, degrees; down (-90) when not said
  bool growSwap = false;                         // `grow'`: the siblings run the other way
  int growLevel = -1;                            // the tree level `grow` was set at: its siblings are not spread there
  bool missing = false;                          // `child[missing]`: a place with no node
  std::string edgePath;                          // `edge from parent path`: a path naming the parent and the child nodes
  std::string parentAnchor, childAnchor;        // `parent anchor`, `child anchor`
  std::optional<int> layer;                      // `on background layer`: -1 is drawn before 0, the main one
  // `plot`: the range and number of samples, the handler (sharp, smooth, const, xcomb, ycomb, sharp cycle), the variable.
  std::optional<std::string> domain;             // `domain=a:b`, as written
  std::optional<int> samples;
  std::optional<std::string> samplesAt;          // `samples at={...}`
  std::string plotHandler;
  std::optional<double> tension;
  std::string plotVariable;                      // `variable=\t`; `\x` when empty
  // Decorations: `decorate` with `decoration={brace, amplitude=, aspect=, mirror, raise=}`.
  bool decorate = false;
  std::string decoration;
  std::optional<Dimen> amplitude, raise;
  std::optional<float> braceAspect;
  bool mirror = false;
  std::vector<std::pair<bool, std::string>> labelSpecs;
  std::optional<Dimen> labelDistance, pinDistance;
  std::string pinEdge;                           // `pin edge={...}`
};

/** A step of a path. */
struct PathOp {
  enum class Kind { move, line, hline, vline, curve, rect, circle, ellipse, arc, close, node, to, grid } kind = Kind::move;
  Coord p, c1, c2;
  Dimen r, r2;
  float a0 = 0.f, a1 = 0.f;
  int node = -1;                                 // index into PathItem::nodes
  bool edge = false;                             // `edge` rather than `to`: a path of its own, the current point stays
  std::optional<Style> own;                      // a `to` or `edge` with options of its own
};

/** A label or a pin: a node of its own, put beside the node it belongs to. */
struct Label {
  Style style;
  sptr<Atom> content;
  std::string angle;                             // `above`, `30`, `center` ...
  bool pin = false;
  Style edge;                                    // a pin's line to its node
};

struct Child;

struct Node {
  std::string name;
  Style style;
  sptr<Atom> content;
  Coord at;
  bool hasAt = false;
  float pos = 0.5f;
  bool posGiven = false;
  bool between = false;                          // written between an operation and its coordinate
  int alongOp = -1;                              // then: the operation it sits on
  std::vector<Label> labels;
  std::vector<Child> children;                   // `child {...}`: placed round it when it is
  std::size_t ownFrom = static_cast<std::size_t>(-1);  // the steps of the style's transform from here are the node's own shifts
};

/** A child of a node in a tree: its node (which has children of its own), the scope it grows in, and its place in the
 *  list (`missing` ones count and are not drawn). */
struct Child {
  Style style;
  Node node;
  int level = 1;
  bool missing = false;
};

/** One cell of a matrix of nodes: the node, and its place in the grid (from 1). */
struct MatrixCell {
  Node node;
  int row = 0, col = 0;
  bool empty = false;                            // nothing in it: it takes no room
};

/** A `\matrix`: its own node, and its cells. */
struct NodeMatrix {
  Node node;
  std::vector<MatrixCell> cells;
  int rows = 0, cols = 0;
  std::vector<Dimen> rowSkip;                    // `\\[2pt]`: more room after a row (index from 1)
};

struct PathItem {
  Style style;
  std::vector<PathOp> ops;
  std::vector<Node> nodes;
  bool isNode = false;                           // a \node or \coordinate: one node, no path
  bool clip = false;
  int clipId = -1;                               // >= 0: this path is a clip path, drawn as nothing
  std::shared_ptr<NodeMatrix> grid;              // a \matrix: one node and its cells, no path
};

}  // namespace tikz

class TikzAtom : public Atom {
public:
  std::vector<tikz::PathItem> paths;
  /** Says something about the picture, at the command that made it, when its box is made (set by the reader). */
  std::function<void(const std::string&)> report;

  TikzAtom();

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex
