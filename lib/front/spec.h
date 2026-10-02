#ifndef GRIDMICROTEX_FRONT_SPEC_H
#define GRIDMICROTEX_FRONT_SPEC_H

#include <cstdint>
#include <string>
#include <vector>

namespace microtex::front {

/** How one argument of a command is read. */
enum class ArgKind : std::uint8_t {
  /** Parsed in math mode, wherever the command is used. */
  math,
  /** Parsed in text mode. */
  text,
  /** Parsed in the mode the command is used in. */
  current,
  /** Kept as text: a colour name, key=value options, a column spec. */
  raw,
  /** A TeX dimension, kept as text. */
  dimen,
  /** A delimiter: one token (`(`, `\langle`, `.`) or a group. */
  delim,
  /** Kept as text, read with `%`, `#`, `_`, `^`, `~`, `&` and `$` as plain
   *  characters, as a URL or file name needs. */
  url,
};

struct ArgSpec {
  ArgKind kind;
  /** A `[...]` argument that may be left out. */
  bool optional;
};

/** Where a command's operands are. */
enum class Shape : std::uint8_t {
  /** Its arguments follow it. */
  prefix,
  /** `\over`: the numerator is everything before it in the list, the
   *  denominator everything after. */
  infix,
  /** `\limits`: modifies the item before it. */
  postfix,
  /** `\bf`, `\displaystyle`, `\large`: applies to the rest of the list,
   *  which ends at the group's end, at `&`, and at `\\` / `\cr`. */
  declaration,
  /** `\color`: applies to the rest of the group, across `\\`. */
  groupDeclaration,
};

/** A value TeX reads without braces after the command. */
enum class Bare : std::uint8_t { none, dimen, number };

struct CommandSpec {
  Shape shape = Shape::prefix;
  std::vector<ArgSpec> args;
  Bare bare = Bare::none;
  /** For a declaration, the mode its body is read in: \displaystyle's is
   *  math even in text, as the engine has always read it. */
  ArgKind body = ArgKind::current;
  /** Handled by the parser itself rather than by its arguments alone:
   *  \left, \right, \middle, \begin, \end, \\, \cr, \(, \[, \hline. */
  bool special = false;
};

/** The spec of a command the engine defines, or nullptr. User macros and
 *  the prelude are expanded before the parser sees them. */
const CommandSpec* findCommand(const std::string& name);

/** How an environment's body is read. */
enum class EnvBody : std::uint8_t {
  /** Rows split at `\\` and `\cr`, cells at `&`. */
  alignment,
  /** Kept as text for its builder (itemize, enumerate). */
  raw,
  /** Text up to its \end, paragraphs and all: a minipage's. */
  text,
};

struct EnvSpec {
  std::vector<ArgSpec> args;
  EnvBody body = EnvBody::alignment;
  /** Its cells or items are text when it is met in text, as in LaTeX
   *  (tabular, itemize); met in math, they are math, as they always were. */
  bool textInText = false;
};

/** The spec of an environment the engine builds, or nullptr. Environments
 *  written in LaTeX (pmatrix, cases, ...) are expanded by the prelude
 *  into these. */
const EnvSpec* findEnvironment(const std::string& name);

/** The names of every command in the table, for tests and for checking the
 *  table against the engine's own. */
std::vector<std::string> commandNames();
std::vector<std::string> environmentNames();

/** A sectioning command set on a line of its own (\section and below).
 *  \paragraph is a heading too, but LaTeX runs it into its paragraph, so
 *  it is not one of these. */
bool isHeadingLine(const std::string& name);
/** Any sectioning command, \paragraph included. */
bool isHeading(const std::string& name);
/** How deep it sits: 0 for \section ... 3 for \paragraph. */
int headingLevel(const std::string& name);

/** An environment LaTeX sets as a display (equation, align, gather, ...),
 *  starred or not, which a document sets on a line of its own. */
bool isDisplayEnvironment(const std::string& name);

/** An environment whose rows are numbered, unless it is starred (equation,
 *  which the prelude writes as an align, is one): by its name without the
 *  star. */
bool isNumberedEnvironment(const std::string& name);

/** A float (table, figure), which a document sets where it is written, as
 *  LaTeX's [h] placement does, apart from the paragraphs around it. */
bool isFloatEnvironment(const std::string& name);

/** A prelude environment of text blocks (abstract, thebibliography): its
 *  expansion is transparent, as document's is, so a document sets the
 *  headings and paragraphs in it as its own. */
bool isBlockEnvironment(const std::string& name);

/** \cite, and natbib's \citep, \citet and \citealp. */
bool isCitation(const std::string& name);

/** A rule across an alignment (\hline, \cline, booktabs' \specialrule):
 *  it ends the row it is in. */
bool isRule(const std::string& name);

/** \centering, \raggedleft or \raggedright: they align a document's lines
 *  to the end of their group, and do nothing in a label. */
bool isLineAlignment(const std::string& name);

}  // namespace microtex::front

#endif
