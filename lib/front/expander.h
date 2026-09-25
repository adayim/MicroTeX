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
  std::vector<BodyPiece> body;
  /** For an environment, the code at \end. */
  std::vector<BodyPiece> endBody;
  /** \let to something that is not a user macro: the expansion is that one
   *  token, handed on as it is and never expanded again. That is what
   *  keeps `\let\oldfrac\frac \renewcommand{\frac}...{\oldfrac...}` from
   *  looping. */
  bool alias = false;
};

struct ExpanderOptions {
  /** Is `name` a command the parser itself defines, such as \frac?
   *  \newcommand refuses it and \renewcommand accepts it. */
  std::function<bool(const std::string&)> isBuiltinCommand;
  /** The same for environments. */
  std::function<bool(const std::string&)> isBuiltinEnvironment;
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
  /** Expand the prelude: the commands and environments the engine defines
   *  in LaTeX (\dfrac, pmatrix, ...; front/prelude.cpp). The old parser
   *  expands those itself, so only the new one wants this. */
  bool prelude = false;
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

  /** Change a catcode from the next token on, for every source being read
   *  (the parser reads a URL with `%` as a character this way). */
  void setCatcode(c32 ch, Cat cat);
  Cat catcode(c32 ch) const;

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;

  friend std::vector<std::string> preludeCommandNames();
  friend std::vector<std::string> preludeEnvironmentNames();
  friend std::vector<std::string> preludeTransparentEnvironmentNames();
};

/** The names the prelude defines, for checking it against the engine. */
std::vector<std::string> preludeCommandNames();
std::vector<std::string> preludeEnvironmentNames();
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
