#ifndef GRIDMICROTEX_FRONT_PARSER_H
#define GRIDMICROTEX_FRONT_PARSER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "front/ast.h"
#include "front/diagnostics.h"
#include "front/expander.h"
#include "front/spec.h"

namespace microtex::front {

struct ParserOptions {
  Mode startMode = Mode::math;
  /** Names the engine knows that take no arguments and have no spec:
   *  symbols (\alpha) and predefined formulas (\sin). Anything else not in
   *  the spec is reported as unknown. */
  std::function<bool(const std::string&)> isKnownName;
  /** Mixed mode: a line end in the prose is a line break, a `\\` of its
   *  own -- the one place a label departs from TeX, for R's "\n". Prose is
   *  text reached from the top without passing through math or an
   *  environment, but for one whose body is text (minipage, an unknown
   *  one), which is prose as its surroundings are. */
  bool lineEndsBreak = false;
  /** Document mode: a blank line (the lexer's `par` token) in the prose
   *  starts a paragraph. A line end on its own is only a space, as TeX
   *  reads it. */
  bool parBreaks = false;
  /** A whole file: where its `\begin{document}` starts (0: no preamble)
   *  and where its `\end{document}` ends. The preamble is read as if the
   *  input ended at the body (Ast::preamble counts its items), and nothing
   *  after the body is read at all. */
  std::uint32_t bodyStart = 0;
  std::uint32_t bodyEnd = UINT32_MAX;
};

/**
 * Builds the syntax tree of an input from the expander's tokens.
 *
 * Recursive descent, driven by the spec table (front/spec.h), tracking math
 * and text mode. It never stops at a mistake: each one is recorded in the
 * diagnostics with its position, and the tree gets what TeX's own recovery
 * would give -- a missing `}` is inserted, `a^b^c` reads as `{a^b}^c`, an
 * unknown command is kept (and drawn red).
 *
 * The structure follows the old parser where the two differ in shape but
 * not in meaning, so that each command can still be built by its old
 * handler: an infix command's denominator is the rest of the list; a
 * declaration like \bf runs to the end of the list or the next `&` / `\\`;
 * a rule (\hline) ends its row.
 */
class Parser {
public:
  Parser(Expander& input, Ast& ast, Diagnostics& diagnostics, ParserOptions options);

  /** Parse the whole input. Returns (and sets) the root list. */
  NodeId parse();

private:
  /** Where a list ends, besides the end of the input. */
  struct Stop {
    /** At `}`, which is left for the caller. */
    bool group = false;
    /** At `&`, `\\`, `\cr` and `\end`: a cell of an alignment. */
    bool cell = false;
    /** This list is the cell itself, not something nested in it. */
    bool cellTop = false;
    /** At `\end` only: the body of an unknown environment met in text. */
    bool end = false;
    /** At `\\` and `\cr` even outside an alignment, and at `&`: the reach
     *  of a declaration such as \bf, and of \over's denominator. */
    bool overArg = false;
    /** At a line end: \over's denominator met in a label's prose, whose
     *  line end is a `\\`, while the denominator itself is math. */
    bool lineEnd = false;
    /** At `\middle` and `\right`. */
    bool right = false;
    /** At `$` (inline) or `$$` (display) closing math. */
    bool dollar = false;
    bool displayDollar = false;
    /** At this control symbol: ")" for \), "]" for \]. */
    std::string closeSymbol;
    /** Reading a one-token argument: a command is only itself (no body
     *  for a declaration, no denominator for \over). */
    bool argument = false;
  };

  /** A token put back. `replay` marks one read a second time (an optional
   *  argument's content), which is not recorded again. */
  /** A token's `origin` when it is read for the first time. */
  static constexpr std::size_t kFresh = static_cast<std::size_t>(-1);

  struct Pending {
    ExpandedToken tok;
    bool replay;
    /** For a token read again, where the tokens put back with it were first
     *  read: a recording begun before that has them already. */
    std::size_t origin;
  };

  struct Logged {
    std::size_t index;
    ExpandedToken tok;
    std::size_t origin;
  };

  Expander& _in;
  Ast& _ast;
  Diagnostics& _diags;
  ParserOptions _opts;

  /** Tokens put back, the next one last. */
  std::vector<Pending> _ahead;
  bool _lastReplay = false;
  std::size_t _lastOrigin = kFresh;
  /** Tokens consumed while an argument's source text is being recorded,
   *  each with its position in the whole stream of consumed tokens. */
  std::vector<Logged> _log;
  std::size_t _consumed = 0;
  int _recording = 0;
  int _depth = 0;
  /** For each \ensuremath in math whose argument is being read again, the
   *  size `_ahead` had before it was put back: while tokens stand above a
   *  mark, that argument is still being read, so the marks are how deep
   *  \ensuremath nests. */
  std::vector<std::size_t> _ensureMarks;
  /** Inside an argument read as text (\text{}, \mbox{}): TeX's restricted
   *  horizontal mode, where `$$` is an empty formula, not display math. */
  int _restricted = 0;
  /** Set when a rule or \intertext ended the row of the cell being read. */
  bool _rowEnded = false;
  /** In the prose of a mixed-mode input, where a line end breaks the line. */
  bool _prose = false;
  /** Inside a typewriter argument (\texttt), whose font has no ligatures. */
  bool _monospace = false;

  ExpandedToken next();
  void unread(ExpandedToken t);
  const ExpandedToken& peek();
  ExpandedToken nextNonSpace();
  /** The next token that is not a space; the spaces before it are read. */
  const ExpandedToken& peekNonSpace();

  std::size_t startRecording();
  /** The source of the tokens consumed since `mark`, less `dropLast`
   *  tokens at the end (a closing brace) whose lead is kept. */
  std::string stopRecording(std::size_t mark, std::size_t dropLast = 0, bool firstLead = true);

  bool atStop(const ExpandedToken& t, const Stop& stop) const;

  // A span is taken by value throughout: `peek()` hands back a reference into
  // `_ahead`, which the next `next()` pops and a later `unread()` refills, so a
  // `const SourceSpan&` taken from a peeked token would name another token's
  // position by the time it is read.
  NodeId parseList(Mode mode, const Stop& stop, SourceSpan at);
  /** The items of a list up to its stop, appended to `items`. */
  void readItems(Mode mode, const Stop& stop, std::vector<NodeId>& items);
  /** One item, appended to `items`. False at a stop token. */
  bool parseItem(Mode mode, const Stop& stop, std::vector<NodeId>& items);
  NodeId parseCommand(ExpandedToken t, Mode mode, const Stop& stop,
                      std::vector<NodeId>& items, bool& consumedRest);
  NodeId parseArgument(const ArgSpec& spec, Mode mode, const std::string& who);
  NodeId parseRawArgument(const ArgSpec& spec, const std::string& who);
  NodeId parseTokens(std::vector<ExpandedToken> tokens, Mode mode, SourceSpan at, std::size_t origin);
  NodeId parseGroupAfterOpen(const ExpandedToken& open, Mode mode);
  NodeId parseScripts(NodeId base, Mode mode, SourceSpan at);
  NodeId parseScriptArgument(Mode mode, SourceSpan at);
  NodeId parseLeftRight(const ExpandedToken& left, Mode mode);
  NodeId parseDelimiter(const std::string& who);
  NodeId parseEnvironment(const ExpandedToken& begin, Mode mode);
  /** An environment's body read as text up to its `\end{name}`, which is
   *  read too (or warned about). */
  NodeId parseTextBody(const std::string& name, SourceSpan at);
  /** `inGroup`: the math sits in a group, whose `}` ends it too. */
  NodeId parseMath(const ExpandedToken& open, bool display, const std::string& closeSymbol,
                   bool inGroup);
  NodeId parseBare(Bare bare, const std::string& who);
  std::vector<ExpandedToken> collectBracketed(const std::string& who);
  std::string readGroupName();

  NodeId emptyList(SourceSpan at, Mode mode);
  /** A `\\` with no gap, for a line end in the prose. `paragraph` marks the
   *  break that starts a paragraph, which the lowering sets apart. */
  NodeId lineBreak(SourceSpan at, Mode mode, bool paragraph = false);
  NodeId character(const ExpandedToken& t, Mode mode);
  /** A text ligature (`--`, `---`, ``` `` ```, `''`) starting at `first`,
   *  or kNoNode when it is not one. */
  NodeId ligature(const ExpandedToken& first, Mode mode);
  NodeId absentArgument(SourceSpan at, Mode mode);
};

}  // namespace microtex::front

#endif
