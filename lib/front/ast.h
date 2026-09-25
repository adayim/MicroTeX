#ifndef GRIDMICROTEX_FRONT_AST_H
#define GRIDMICROTEX_FRONT_AST_H

#include <cstdint>
#include <string>
#include <vector>

#include "front/diagnostics.h"
#include "utils/types.h"

namespace microtex::front {

using NodeId = std::uint32_t;
constexpr NodeId kNoNode = 0xFFFFFFFFu;

enum class Mode : std::uint8_t { math, text };

enum class NodeKind : std::uint8_t {
  /** Items in order. The children are the items. */
  list,
  /** `{...}`. One child: the list inside. */
  group,
  /** One character (with any variation selector or joined code points). */
  character,
  /** Space in text. `raw` holds the source whitespace it stands for. */
  space,
  /** A command: `text` is its name. Children are its arguments, in the
   *  order the spec lists them. `flag` marks a command the engine does
   *  not define (drawn red). */
  command,
  /** One argument. `flag` says whether it was given (an optional one may
   *  not be); `raw` is its source text after macro expansion, exactly as
   *  the old parser's command handlers received it. One child, the parsed
   *  list, unless the argument is kept as text. */
  argument,
  /** Sub- and superscripts. Children: base (a list, empty for none), sub,
   *  sup (each a list, or an empty list when absent); `aux` counts the
   *  primes, `flag` says the sub is present and `star` the sup. */
  scripts,
  /** `\over` and kin: `text` is the command. Children: numerator list,
   *  denominator list, then its own arguments (delimiters, a dimension). */
  infix,
  /** `\bf`, `\color`: `text` is the command. Children: its arguments, then
   *  the list it applies to. */
  declaration,
  /** `\left ... \middle ... \right`. Children: the left delimiter
   *  argument, then list, middle delimiter argument, list, ..., and the
   *  right delimiter argument. */
  leftRight,
  /** `\begin{name}...\end{name}`: `text` is the name, `raw` the body's
   *  source text. Children: the environment's arguments, then its rows.
   *  `flag` says its cells or items are text (a tabular or a list met in
   *  text). */
  environment,
  /** One row of an alignment. Children: its cells. `text` says what ended
   *  it: "\\", "cr", a rule's name, "intertext", or "" at the end;
   *  `raw` holds the dimension of `\\[...]`, `star` a `\\*`. */
  row,
  /** One cell. One child: its list. */
  cell,
  /** `$...$`, `\(...\)` (inline) or `$$...$$`, `\[...\]` (display, `flag`)
   *  inside text. One child: the list. */
  math,
  /** A place the input could not be read. `text` is the problem; the
   *  children are whatever was read around it. */
  error,
};

struct Node {
  NodeKind kind = NodeKind::list;
  Mode mode = Mode::math;
  bool flag = false;
  bool star = false;
  std::uint16_t aux = 0;
  c32 cp = 0;
  SourceSpan span;
  std::string text;
  std::string raw;
  std::uint32_t firstChild = 0;
  std::uint32_t childCount = 0;
};

/** The parse of one input: nodes in one array, children as index ranges
 *  into a second, so a tree of any size is two allocations that grow. */
class Ast {
public:
  NodeId add(Node node, const std::vector<NodeId>& children);

  const Node& node(NodeId id) const { return _nodes[id]; }
  Node& node(NodeId id) { return _nodes[id]; }

  NodeId child(NodeId id, std::uint32_t i) const {
    return _children[_nodes[id].firstChild + i];
  }

  std::uint32_t childCount(NodeId id) const { return _nodes[id].childCount; }

  std::size_t size() const { return _nodes.size(); }

  NodeId root = kNoNode;

private:
  std::vector<Node> _nodes;
  std::vector<NodeId> _children;
};

const char* nodeKindName(NodeKind kind);

}  // namespace microtex::front

#endif
