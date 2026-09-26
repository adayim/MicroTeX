// libFuzzer harness for the LaTeX front end (lib/front/).
//
// Font-free, so it needs no MicroTeX init: it runs the lexer, the expander
// and the parser over arbitrary bytes. Beyond "no crash, no hang, no
// sanitizer report" it checks their own promises: token spans move forward
// and stay inside the input, every token but `end` consumes input (which is
// what makes the lexer linear), text with no macro expands to itself, and
// the parser builds a tree.
//
//     test/run.sh fuzz 300

#include <cstddef>
#include <cstdint>
#include <string>

#include "front/ast.h"
#include "front/expander.h"
#include "front/lexer.h"
#include "front/parser.h"
#include "front/spec.h"
#include "utils/exceptions.h"

using namespace microtex::front;

namespace {

void lex_all(const std::string& src, bool par, bool atLetter) {
  Diagnostics diags;
  LexOptions opts;
  opts.blankLineIsPar = par;
  CatcodeTable catcodes;
  Lexer lexer(src, opts, diags, catcodes);
  if (atLetter) lexer.setCatcode('@', Cat::letter);

  std::size_t last = 0;
  std::size_t count = 0;
  while (true) {
    const Token t = lexer.next();
    if (t.span.offset < last) __builtin_trap();
    if (t.span.offset + t.span.length > src.size()) __builtin_trap();
    last = t.span.offset;
    if (t.kind == TokKind::end) break;
    if (t.span.length == 0) __builtin_trap();
    if (++count > src.size()) __builtin_trap();
  }
  // `end` is sticky.
  if (lexer.next().kind != TokKind::end) __builtin_trap();
}

// The expander must end on any input (its limits throw ex_parse), and text
// with no backslash in it has no macro to expand, so it must come back as it
// went in, less a leading byte-order mark.
void expand_all(const std::string& src) {
  Diagnostics diags;
  ExpanderOptions opts;
  opts.isBuiltinCommand = [](const std::string& name) { return name == "frac"; };
  opts.isBuiltinEnvironment = [](const std::string& name) { return name == "matrix"; };
  opts.maxExpandedBytes = std::size_t{1} << 20;
  std::string out;
  try {
    out = Expander(src, std::move(opts), diags).expandToText();
  } catch (const microtex::ex_parse&) {
    return;
  }
  if (src.find('\\') == std::string::npos) {
    const bool bom = src.compare(0, 3, "\xEF\xBB\xBF") == 0;
    if (out != (bom ? src.substr(3) : src)) __builtin_trap();
  }
}

// The parser must end on any input, and what it builds must be a tree:
// every node reachable from the root exactly once.
void parse_all(const std::string& src, Mode mode, bool lineBreaks, std::uint32_t bodyStart,
               std::uint32_t bodyEnd) {
  Diagnostics diags;
  ExpanderOptions eo;
  eo.prelude = true;
  eo.persistent = false;
  eo.maxExpandedBytes = std::size_t{1} << 20;
  eo.isBuiltinCommand = [](const std::string& name) { return findCommand(name) != nullptr; };
  eo.isBuiltinEnvironment = [](const std::string& name) { return findEnvironment(name) != nullptr; };
  Ast ast;
  try {
    Expander expander(src, std::move(eo), diags);
    ParserOptions po;
    po.startMode = mode;
    po.lineEndsBreak = lineBreaks;
    po.bodyStart = bodyStart;
    po.bodyEnd = bodyEnd;
    po.isKnownName = [](const std::string& name) { return name == "alpha"; };
    Parser(expander, ast, diags, std::move(po)).parse();
  } catch (const microtex::ex_parse&) {
    return;
  }
  std::vector<unsigned char> seen(ast.size(), 0);
  std::vector<NodeId> stack{ast.root};
  while (!stack.empty()) {
    const NodeId id = stack.back();
    stack.pop_back();
    if (id >= ast.size() || seen[id]) __builtin_trap();
    seen[id] = 1;
    for (std::uint32_t i = 0; i < ast.childCount(id); i++) stack.push_back(ast.child(id, i));
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string src(reinterpret_cast<const char*>(data), size);
  lex_all(src, false, false);
  lex_all(src, true, size > 0 && (data[0] & 1));
  expand_all(src);
  // With bit 3 set, a whole file's body starts where the second byte says
  // and ends the third byte's count further on (anywhere: both ends are
  // positions, not tokens).
  const bool file = size > 2 && (data[0] & 8);
  const std::uint32_t bodyStart = file ? data[1] : 0;
  const std::uint32_t bodyEnd = file ? bodyStart + data[2] : UINT32_MAX;
  parse_all(src, size > 0 && (data[0] & 2) ? Mode::text : Mode::math, size > 0 && (data[0] & 4),
            bodyStart, bodyEnd);
  return 0;
}
