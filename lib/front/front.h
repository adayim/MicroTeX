#ifndef GRIDMICROTEX_FRONT_FRONT_H
#define GRIDMICROTEX_FRONT_FRONT_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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
 * ex_parse. `packages`: the packages it starts from (setStartPackages()).
 */
Ast parseLatex(const std::string& latex, Mode mode, Diagnostics& diagnostics,
               bool lineBreaks = false, bool paragraphs = false, std::uint32_t bodyStart = 0,
               std::uint32_t bodyEnd = UINT32_MAX, bool userMacros = true,
               const std::vector<std::string>& packages = {},
               std::shared_ptr<const UserScope> inherit = nullptr);

/**
 * The atoms of `latex`, read by the new front end as `mode` says, into
 * `formula`. The problems found replace lastDiagnostics().
 */
void buildModern(const std::string& latex, InputMode mode, Formula& formula);

/** The diagnostics of the last buildModern(), for the host to report. */
const Diagnostics& lastDiagnostics();

/** Say something about the input at `at` while its boxes are made, which is after the input is read: a layout that
 *  finds what a reader could not (an anchor a node has not). Added to lastDiagnostics(), once. */
void warnLater(const SourceSpan& at, const std::string& message);

/** The packages (front/packages.h) the next buildModern() starts from, after
 *  the format: the input need not ask for them, and what they do besides
 *  define is done first. Used once: the input after that starts with none. */
void setStartPackages(std::vector<std::string> packages);

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
