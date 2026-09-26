#ifndef GRIDMICROTEX_FRONT_LOWER_H
#define GRIDMICROTEX_FRONT_LOWER_H

#include "core/formula.h"
#include "front/ast.h"
#include "front/diagnostics.h"

namespace microtex::front {

/**
 * Builds the engine's atoms from a syntax tree, into `formula`.
 *
 * The tree is replayed onto the engine's own Formula in the order the old
 * parser added things to it, so what depended on that order -- the line
 * break marks Formula::add() inserts, the "previous atom" that scripts and
 * \limits take, the braces an environment was wrapped in -- comes out the
 * same. Structure the parser owns (groups, scripts, \over, declarations,
 * \left...\right, line breaks) is built here; every other command is built
 * by its existing handler, given the source text of its arguments exactly
 * as it used to receive it.
 *
 * An error a handler raises is recorded as a diagnostic at the command, and
 * the command is drawn as its name in red, as an unknown one is.
 *
 * `lines`: build the tree as the rows of a label (mixed and document mode)
 * rather than as one formula. `paragraphs`: those rows are paragraphs, so
 * the first one is indented as TeX indents it. A whole file's preamble
 * (Ast::preamble) is lowered for what it defines, quietly, and not drawn.
 */
void lowerInto(const Ast& ast, Formula& formula, Diagnostics& diagnostics, bool lines = false,
               bool paragraphs = false);

/**
 * `latex` read as a piece of input of its own, in math or text mode, and
 * its atom; nullptr when it is empty. For LaTeX the engine writes itself --
 * the digits of a long division, a command's name drawn in red -- which
 * reads cleanly, so its problems are not reported.
 */
sptr<Atom> buildFragment(const std::string& latex, bool math = true);

}  // namespace microtex::front

#endif
