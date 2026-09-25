#ifndef MACRO_H_INCLUDED
#define MACRO_H_INCLUDED

#include <map>
#include <string>
#include <vector>

#include "atom/atom.h"
#include "utils/utils.h"

namespace microtex {

/**
 * The engine's command handlers, by name. The front end (front/lower.cpp)
 * reads a command and, for one it does not build itself, looks its handler
 * up here; each handler reads its arguments through CommandArgs
 * (macro/macro_args.h).
 *
 * The entries are raw `new`ed pointers in a static container, freed by the
 * MacroRegistryCleanup object at the end of macro_def.cpp -- see CLAUDE.md
 * on why that object has to stay last in its file.
 */
class MacroInfo {
private:
  static std::map<std::string, MacroInfo*> _commands;

public:
  /** Add a macro, replace it if the macro is exists. */
  static void add(const std::string& name, MacroInfo* mac);

  /** Get the macro info from given name, return nullptr if not found. */
  static MacroInfo* get(const std::string& name);

  /** Every registered name, for checking the front end's command table
   *  against this one. */
  static std::vector<std::string> names();

  /** Remove and delete the macro info entry for the given name. No-op
   *  if the name is not registered. */
  static void remove(const std::string& name);

  /** Remove the entry for the given name *without* deleting it, and hand
   *  it to the caller; nullptr if the name is not registered. */
  static MacroInfo* release(const std::string& name);

  // Number of arguments
  const int argc;
  // Options' position, can be  0, 1 and 2
  // 0 represents this macro has no options
  // 1 represents the options appear after the command name, e.g.:
  //      \sqrt[3]{2}
  // 2 represents the options appear after the first argument, e.g.:
  //      \scalebox{0.5}[2]{\LaTeX}
  const int opt;

  no_copy_assign(MacroInfo);

  MacroInfo() : argc(0), opt(0) {}

  MacroInfo(int argc, int opt) : argc(argc), opt(opt) {}

  explicit MacroInfo(int argc) : argc(argc), opt(0) {}

  virtual ~MacroInfo() = default;

  static void _free_();
};

}  // namespace microtex

#endif  // MACRO_H_INCLUDED
