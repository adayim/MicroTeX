#ifndef GRIDMICROTEX_FRONT_EXPANDER_H
#define GRIDMICROTEX_FRONT_EXPANDER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "front/diagnostics.h"
#include "front/lexer.h"
#include "front/token.h"

namespace microtex::front {

/** One piece of a macro's replacement text: literal source text, or a
 *  parameter (1..9) to be replaced by that argument's text. */
struct BodyPiece {
  std::string text;
  int param = 0;
};

/** One argument of a \NewDocumentCommand, and how it is read. */
struct XArg {
  /** xparse's letters: m o O s t r R d D g G. */
  char type = 'm';
  /** t's token; the opening and closing delimiters of r, R, d and D. */
  Token open;
  Token close;
  /** What O, R, D and G give when the argument is not there. */
  std::string deflt;
};

/**
 * A user macro or environment. The replacement text is split into pieces
 * when it is defined, so an expansion substitutes every `#n` in one pass
 * and never scans an argument's own text for parameters.
 */
struct MacroDef {
  int nparams = 0;
  /** \newcommand[n][default]: the first parameter is optional. */
  bool hasOptional = false;
  std::string optionalDefault;
  /** \def's parameter text: delimiters[0] must follow the name, and
   *  delimiters[i] ends parameter i. Empty means undelimited. */
  std::vector<std::vector<Token>> delimiters;
  /** \NewDocumentCommand's arguments, one for each parameter; empty for a
   *  macro made any other way. */
  std::vector<XArg> xargs;
  std::vector<BodyPiece> body;
  /** For an environment, the code at \end. */
  std::vector<BodyPiece> endBody;
  /** \let to something that is not a user macro: the expansion is that one
   *  token, handed on as it is and never expanded again. That is what
   *  keeps `\let\oldfrac\frac \renewcommand{\frac}...{\oldfrac...}` from
   *  looping. */
  bool alias = false;
  /** `@` was a letter when this was defined (\makeatletter): its body names
   *  `\my@a` as one name, and is read so again wherever it is used, after
   *  \makeatother too. */
  bool atLetter = false;
  /** The package whose file defined it: `LaTeX` for the format itself, empty
   *  for what the input defines (and define_macro()). A name from a package
   *  the input did not ask for is free for the input to define. */
  std::string package;
};

struct ExpanderOptions {
  /** Is `name` a command the parser itself defines, such as \frac?
   *  \newcommand refuses it and \renewcommand accepts it. */
  std::function<bool(const std::string&)> isBuiltinCommand;
  /** The same for environments. */
  std::function<bool(const std::string&)> isBuiltinEnvironment;
  /** Is `name` a symbol or a predefined formula, which the parser knows
   *  without its being a command? \ifdefined and \ifx ask. */
  std::function<bool(const std::string&)> isKnownName;
  LexOptions lex;
  /** TeX stops runaway recursion with its capacity limits; these are ours.
   *  The bytes are all the expansions of one parse together, and reading
   *  and parsing them is the cost, about 0.8 s per MB -- and all of it
   *  wasted, since a runaway is an error.
   *
   *  The byte cap is what bounds the time; the count is a second net, for
   *  a recursion that expands to nothing (`\def\a{\a}`) and so never
   *  reaches the byte cap. 10,000 was too tight for a document: a
   *  book-length body of ordinary prose reached it at about 16,000
   *  expansions (970 KB of source, 0.25 s in), while a runaway that
   *  produces no bytes still stops in well under a millisecond at
   *  100,000. The byte cap stays where it was: nothing real has reached
   *  it (that same body expanded to about 220 KB), and raising it would
   *  buy nothing while making every runaway proportionally slower to
   *  stop. */
  std::size_t maxExpansions = 100000;
  std::size_t maxExpandedBytes = std::size_t{1} << 20;
  /** Start from the format and the packages every input starts with: the
   *  commands and environments the engine defines in LaTeX (\dfrac,
   *  pmatrix, ...), which the host registered (front/packages.h). */
  bool prelude = false;
  /** With `prelude`: packages to start from, after the format, as
   *  latex_options(packages = ) names them. They are read once for all the
   *  parses with the same list, and an input need not ask for them; what
   *  they do besides define is read before the input. */
  std::vector<std::string> packages;
  /** See the define_macro() macros (setPersistentMacro). */
  bool persistent = true;
  /** Report a bad definition or runaway recursion as a diagnostic and go
   *  on (the new front end), rather than throwing ex_parse as the old
   *  parser did (its text path). */
  bool recover = false;
};

/** A token after expansion, with the source text it came from: the
 *  whitespace and comments before it, and its own text. */
struct ExpandedToken {
  Token tok;
  std::string lead;
  std::string text;
};

/**
 * Expands user macros at the token level.
 *
 * Handles \newcommand, \renewcommand, \providecommand and
 * \DeclareMathOperator (each also starred), \def and \gdef with delimited
 * parameters, \let, \newenvironment and \renewenvironment, the macros
 * defined with define_macro(), and a few starred forms of built-ins. Macro
 * arguments are read as TeX reads them: one token or one braced group,
 * never a byte.
 *
 * Built-in commands are passed through. Until the new parser exists
 * (plan Stage 3), the result goes back to the old parser as text: text
 * with no user macros in it comes back byte for byte, whitespace and
 * comments included, so the old parser sees exactly what it saw before.
 *
 * Errors the old parser raised for the same input (redefining an existing
 * command with \newcommand, runaway recursion) are still thrown as
 * ex_parse with the same messages. Other problems are recorded in the
 * diagnostics and expansion carries on.
 */
class Expander {
public:
  Expander(std::string input, ExpanderOptions options, Diagnostics& diagnostics);
  ~Expander();
  Expander(const Expander&) = delete;
  Expander& operator=(const Expander&) = delete;

  /** The whole input with every user macro expanded and every definition
   *  removed. */
  std::string expandToText();

  /** The next token after expansion; an `end` token at the end, every
   *  time. Use either this or expandToText(), not both. */
  ExpandedToken next();

  /** The input ends before its first token at or past byte `offset`, until
   *  this is called again with a later one: then reading goes on from
   *  there. Every reader sees that end, a macro's arguments too, so what
   *  began before it is closed there -- environments as at the end of the
   *  input, and the groups and what was defined in them. A whole file's
   *  preamble is read so, and its body up to \end{document}. */
  void endInputAt(std::uint32_t offset);

  /** Change a catcode from the next token on, for every source being read
   *  (the parser reads a URL with `%` as a character this way). */
  void setCatcode(c32 ch, Cat cat);

  /** `text` is read next, as if it stood in the input here: its macros are expanded, with the definitions the input
   *  has made so far. The parser puts the code of an `alltt` back this way. */
  void pushText(const std::string& text, const SourceSpan& at);

  /** Whether `name` was made a verbatim environment by \DefineVerbatimEnvironment, and if so the environment it is
   *  and the options it has. */
  bool verbatimEnvironment(const std::string& name, std::string& base, std::string& options) const;

  /** The `\tikzset` and `\tikzstyle` read so far, as statements: what a tikzpicture begins with. */
  const std::string& tikzStyles() const;
  Cat catcode(c32 ch) const;

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;

  friend std::vector<std::string> preludeCommandNames();
  friend std::vector<std::string> preludeEnvironmentNames();
  friend std::vector<std::string> preludeProblems();
  friend std::vector<std::string> preludeTransparentEnvironmentNames();
};

/** The names the format and the packages every input starts with define
 *  (packages.h), for checking them against the engine. */
std::vector<std::string> preludeCommandNames();
std::vector<std::string> preludeEnvironmentNames();
/** What went wrong reading them, one line each: a name defined twice, a
 *  command the expander could not read. Empty when all is well. */
std::vector<std::string> preludeProblems();

/** Read the registered package `name` on top of the format, as an input that
 *  asks for it would, and report what went wrong, one line each, as the
 *  package's own: "package NAME line L:C: message". Empty when all is well. */
std::vector<std::string> checkPackage(const std::string& name);
/** Those of them that expand to nothing at either end (`document`, `table`,
 *  `figure`): they wrap content rather than make any of it math. */
std::vector<std::string> preludeTransparentEnvironmentNames();

/**
 * Macros that outlive a parse: those made with define_macro(). Zero
 * parameters; a parse sees them all, and a \renewcommand in a label shadows
 * one for that label only. The generation changes on every edit, so a
 * cache of layouts can tell when its entries went stale.
 */
void setPersistentMacro(const std::string& name, const std::string& body);
bool removePersistentMacro(const std::string& name);
void clearPersistentMacros();
const std::vector<std::pair<std::string, std::string>>& persistentMacros();
std::uint64_t persistentMacroGeneration();

}  // namespace microtex::front

#endif
