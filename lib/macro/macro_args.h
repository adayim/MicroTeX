#ifndef MICROTEX_MACRO_ARGS_H
#define MICROTEX_MACRO_ARGS_H

#include <string>
#include <vector>

#include "atom/atom.h"
#include "macro/macro.h"

namespace microtex {

class ArrayFormula;

/**
 * How a command handler reads its arguments. Index 0 is the command's
 * name, 1 to argc its mandatory arguments, its optional ones after those.
 *
 * The front end has read the arguments already, and hands over what it
 * built from them (front/lower.cpp), so an argument is read once, with the
 * rest of the input, and its problems are reported where they are.
 */
class CommandArgs {
public:
  virtual ~CommandArgs() = default;

  /** Argument i's source text; "" when an optional one is absent. */
  virtual const std::string& text(std::size_t i) const = 0;

  /** Argument i as a formula, read in math or text mode. nullptr when it is
   *  empty. `preprocess` is a leftover of the old parser's interface, and
   *  means nothing now: the front end has nothing left to preprocess. */
  virtual sptr<Atom> formula(std::size_t i, bool math = true, bool preprocess = false) = 0;

  /** Argument i as the body of an alignment, its rows and cells in a
   *  formula of its own, which the caller finishes (checkDimensions()). */
  virtual sptr<ArrayFormula> alignment(std::size_t i) = 0;

  /** LaTeX a handler put together itself, read as a formula. */
  virtual sptr<Atom> formulaOf(const std::string& latex, bool math = true) = 0;

  /** LaTeX a handler put together itself, read as the body of an
   *  alignment (rows at `\\`, cells at `&`). */
  virtual sptr<ArrayFormula> alignmentOfText(const std::string& latex) = 0;

  /** The alignment the command is in, which a rule or \multicolumn works
   *  on; nullptr outside one. */
  virtual ArrayFormula* alignmentHere() = 0;

  /** Whether the command itself was read in math mode. */
  virtual bool isMathMode() const = 0;

  /** The old parser's partial flag: an unknown command is drawn, not an
   *  error. Always so now; the handlers that still ask go in Stage 8. */
  virtual bool isPartial() const = 0;

  /** As formulaOf(), but its problems are reported at the command, for
   *  text of the user's that a handler cut out of an environment. */
  virtual sptr<Atom> formulaChecked(const std::string& latex, bool math = true) {
    return formulaOf(latex, math);
  }

  /** Say something about the command's input, at the command. */
  virtual void warn(const std::string& message) { (void)message; }
};

typedef sptr<Atom> (*CommandDelegate)(CommandArgs& args);

/** A command whose handler reads its arguments through CommandArgs. */
class CommandMacro : public MacroInfo {
private:
  CommandDelegate _delegate;

public:
  no_copy_assign(CommandMacro);

  CommandMacro(int argc, int posOpts, CommandDelegate delegate)
      : MacroInfo(argc, posOpts), _delegate(delegate) {}

  CommandMacro(int argc, CommandDelegate delegate) : MacroInfo(argc), _delegate(delegate) {}

  sptr<Atom> call(CommandArgs& args) { return _delegate(args); }
};

}  // namespace microtex

/** A command handler; its arguments are `args`. */
#define cmdmacro(name) sptr<Atom> macro_##name(CommandArgs& args)

#endif  // MICROTEX_MACRO_ARGS_H
