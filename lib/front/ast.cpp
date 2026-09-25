#include "front/ast.h"

#include <utility>

namespace microtex::front {

NodeId Ast::add(Node node, const std::vector<NodeId>& children) {
  node.firstChild = static_cast<std::uint32_t>(_children.size());
  node.childCount = static_cast<std::uint32_t>(children.size());
  _children.insert(_children.end(), children.begin(), children.end());
  _nodes.push_back(std::move(node));
  return static_cast<NodeId>(_nodes.size() - 1);
}

const char* nodeKindName(NodeKind kind) {
  switch (kind) {
    case NodeKind::list: return "list";
    case NodeKind::group: return "group";
    case NodeKind::character: return "char";
    case NodeKind::space: return "space";
    case NodeKind::command: return "command";
    case NodeKind::argument: return "argument";
    case NodeKind::scripts: return "scripts";
    case NodeKind::infix: return "infix";
    case NodeKind::declaration: return "declaration";
    case NodeKind::leftRight: return "leftright";
    case NodeKind::environment: return "environment";
    case NodeKind::row: return "row";
    case NodeKind::cell: return "cell";
    case NodeKind::math: return "math";
    case NodeKind::error: return "error";
  }
  return "?";
}

}  // namespace microtex::front
