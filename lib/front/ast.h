#ifndef GRIDMICROTEX_FRONT_AST_H
#define GRIDMICROTEX_FRONT_AST_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "front/diagnostics.h"
#include "utils/types.h"

namespace microtex::front {

struct UserScope;  // expander.h

/** What a package says of an environment it defines (`\gmenvironmentkind{kind}{names}`), which the engine cannot
 *  read off its code. Bits, so one environment can have several. */
namespace EnvironmentKind {
/** Its `[outer]{inner}` is key=value data the package reads itself: left as written (tabularray's). */
constexpr std::uint8_t keyValue = 1;
/** Written in LaTeX, and a block of the document in its own right (`proof`, `framed`), not a group on a line of the
 *  paragraph. */
constexpr std::uint8_t block = 2;
/** A float (`table`, `figure`): set where it is written, apart from the paragraphs around it. A block too. */
constexpr std::uint8_t floating = 4;
/** A display (`equation`, `align`): set on a line of its own. */
constexpr std::uint8_t display = 8;
/** Its rows are numbered (`align`, `gather`). */
constexpr std::uint8_t numbered = 16;
/** The kernel's `center`, `flushleft` and `flushright`. */
constexpr std::uint8_t center = 32;
constexpr std::uint8_t flushLeft = 64;
constexpr std::uint8_t flushRight = 128;
}  // namespace EnvironmentKind

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

  /** What the input had defined where environment `id` began, kept for an environment whose arguments a package reads
   *  later (tabularray's `cmd=\mycmd`): LaTeX it holds is read with those definitions in force. Null if none. */
  void setScope(NodeId id, std::shared_ptr<const UserScope> scope) { _scopes[id] = std::move(scope); }
  std::shared_ptr<const UserScope> scopeOf(NodeId id) const {
    const auto it = _scopes.find(id);
    return it == _scopes.end() ? nullptr : it->second;
  }

  /** What the format and the packages declared of their environments (`EnvironmentKind` bits), as the parse found them. */
  std::unordered_map<std::string, std::uint8_t> environmentKinds;
  bool environmentIs(const std::string& name, std::uint8_t kind) const {
    const auto it = environmentKinds.find(name);
    return it != environmentKinds.end() && (it->second & kind) != 0;
  }

  /** What the format and the packages gave the lowering as data (`\gmdeclare{table}{key}{value}`), by "table\x1fkey". */
  std::map<std::string, std::string> declared;

  NodeId root = kNoNode;
  /** How many of the root's first items are a whole file's preamble: read
   *  for what they define, not drawn. */
  std::uint32_t preamble = 0;

private:
  std::vector<Node> _nodes;
  std::vector<NodeId> _children;
  std::unordered_map<NodeId, std::shared_ptr<const UserScope>> _scopes;
};

const char* nodeKindName(NodeKind kind);

}  // namespace microtex::front

#endif
