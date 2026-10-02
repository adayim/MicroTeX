#ifndef GRIDMICROTEX_FRONT_FRONT_H
#define GRIDMICROTEX_FRONT_FRONT_H

#include <cstdint>
#include <map>
#include <string>

#include "front/ast.h"
#include "front/diagnostics.h"
#include "front/input_mode.h"

namespace microtex {
class Formula;
}

namespace microtex::front {

/**
 * The syntax tree of `latex` read by the new front end: the prelude, user
 * macros and define_macro() macros expanded, then parsed from `mode`.
 * `lineBreaks`: a line end in text is a line break (mixed mode).
 * `paragraphs`: a blank line is a `\par`, which starts a paragraph
 * (document mode). `bodyStart`, `bodyEnd`: a whole file's body, from its
 * `\begin{document}` to the end of its `\end{document}`; the preamble
 * before it is read for what it defines (ParserOptions), and nothing after
 * it is read. `userMacros`: define_macro()'s macros apply (not to the
 * engine's own definitions). Problems go to `diagnostics`; errors the
 * expander still raises (a redefinition, runaway recursion) are thrown as
 * ex_parse.
 */
Ast parseLatex(const std::string& latex, Mode mode, Diagnostics& diagnostics,
               bool lineBreaks = false, bool paragraphs = false, std::uint32_t bodyStart = 0,
               std::uint32_t bodyEnd = UINT32_MAX, bool userMacros = true);

/**
 * The atoms of `latex`, read by the new front end as `mode` says, into
 * `formula`. The problems found replace lastDiagnostics().
 */
void buildModern(const std::string& latex, InputMode mode, Formula& formula);

/** The diagnostics of the last buildModern(), for the host to report. */
const Diagnostics& lastDiagnostics();

/** The equation count and the labels of an input: where one starts, and
 *  where it ended. A host that lays a document out in pieces, each its own
 *  input, passes the count and the labels on from one to the next. */
struct NumberingState {
  int equation = 0;
  /** label -> what a reference to it draws, as LaTeX source. */
  std::map<std::string, std::string> labels;
};

/** Where the next buildModern() starts counting, and the labels it already
 *  knows. Used once: the input after that starts afresh. */
void setStartNumbering(NumberingState start);

/** Where the last buildModern() left them. */
const NumberingState& lastNumbering();

}  // namespace microtex::front

#endif
