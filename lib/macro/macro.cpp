#include "macro/macro.h"

#include <string>
#include <vector>

#include "macro/macro_misc.h"

using namespace std;
using namespace microtex;

bool NewCommandMacro::_errIfConflict = true;

bool NewCommandMacro::isMacro(const string& name) {
  auto it = _codes.find(name);
  return (it != _codes.end());
}

// A built-in command (\frac, \sqrt, ...) counts as defined, as it does in
// LaTeX, so \newcommand refuses it and \renewcommand accepts it. Only the
// code macros were checked before: \newcommand{\frac} replaced the built-in,
// and clearUserMacros() then removed the replacement, so no later parse in
// the process had a \frac at all.
static bool isDefined(const string& name) {
  return NewCommandMacro::isMacro(name) || MacroInfo::get(name) != nullptr;
}

void NewCommandMacro::checkNew(const string& name) {
  if (_errIfConflict && (_sealed ? isDefined(name) : isMacro(name)))
    throw ex_parse("Command " + name + " already exists! Use renewcommand instead!");
}

void NewCommandMacro::checkRenew(const string& name) {
  if (NewCommandMacro::_errIfConflict && !(_sealed ? isDefined(name) : isMacro(name)))
    throw ex_parse("Command " + name + " is no defined! Use newcommand instead!");
}

// Record what defining `name` is about to displace, the first time it is
// defined in a parse, so clearUserMacros() can restore it -- and take the
// displaced MacroInfo out of the registry rather than let MacroInfo::add()
// delete it.
void NewCommandMacro::save(const string& name) {
  if (!_sealed || _displaced.count(name) != 0) return;
  Displaced d;
  const auto code = _codes.find(name);
  if (code != _codes.end()) {
    d.hadCode = true;
    d.code = code->second;
  }
  const auto rep = _replacements.find(name);
  if (rep != _replacements.end()) {
    d.hadReplacement = true;
    d.replacement = rep->second;
  }
  d.info = MacroInfo::release(name);
  _displaced[name] = d;
}

void NewCommandMacro::addNewCommand(const string& name, const string& code, int argc) {
  checkNew(name);
  save(name);
  _codes[name] = code;
  MacroInfo::add(name, new InflationMacroInfo(_instance, argc));
}

void NewCommandMacro::addNewCommand(
  const string& name,
  const string& code,
  int argc,
  const string& def
) {
  checkNew(name);
  save(name);
  _codes[name] = code;
  _replacements[name] = def;
  MacroInfo::add(name, new InflationMacroInfo(_instance, argc, 1));
}

void NewCommandMacro::addRenewCommand(const string& name, const string& code, int argc) {
  checkRenew(name);
  save(name);
  _codes[name] = code;
  MacroInfo::add(name, new InflationMacroInfo(_instance, argc));
}

void NewCommandMacro::addRenewCommand(
  const string& name,
  const string& code,
  int argc,
  const string& def
) {
  checkRenew(name);
  save(name);
  _codes[name] = code;
  _replacements[name] = def;
  MacroInfo::add(name, new InflationMacroInfo(_instance, argc, 1));
}

void NewCommandMacro::addDefCommand(const string& name, const string& code, int argc) {
  save(name);
  _codes[name] = code;
  MacroInfo::add(name, new InflationMacroInfo(_instance, argc));
}

void NewCommandMacro::execute(Parser& tp, vector<string>& args) {
  string code = _codes[args[0]];
  string rep;
  size_t argc = args.size() - 12;
  int dec = 0;

  auto it = _replacements.find(args[0]);

  if (!args[argc + 1].empty()) {
    dec = 1;
    replaceAll(code, "#1", args[argc + 1]);
  } else if (it != _replacements.end()) {
    dec = 1;
    replaceAll(code, "#1", it->second);
  }

  for (size_t i = 1; i <= argc; i++) {
    rep = args[i];
    replaceAll(code, "#" + toString(i + dec), rep);
  }
  args.push_back(code);
}

void NewEnvironmentMacro::addNewEnvironment(
  const string& name,
  const string& begDef,
  const string& endDef,
  int argc
) {
  string n = name + "@env";
  string def = begDef + " #" + toString(argc + 1) + " " + endDef;
  addNewCommand(n, def, argc + 1);
}

void NewEnvironmentMacro::addRenewEnvironment(
  const string& name,
  const string& begDef,
  const string& endDef,
  int argc
) {
  if (_codes.find(name + "@env") == _codes.end()) {
    throw ex_parse("Environment " + name + "is not defined! Use newenvironment instead!");
  }
  addRenewCommand(name + "@env", begDef + " #" + toString(argc + 1) + " " + endDef, argc + 1);
}

void NewCommandMacro::clearUserMacros() {
  for (auto& kv : _displaced) {
    const string& name = kv.first;
    const Displaced& d = kv.second;
    // add() deletes the definition the parse made; remove() does the same
    // when there was nothing before it.
    if (d.info != nullptr) {
      MacroInfo::add(name, d.info);
    } else {
      MacroInfo::remove(name);
    }
    if (d.hadCode) {
      _codes[name] = d.code;
    } else {
      _codes.erase(name);
    }
    if (d.hadReplacement) {
      _replacements[name] = d.replacement;
    } else {
      _replacements.erase(name);
    }
  }
  _displaced.clear();
}

void NewCommandMacro::snapshotBuiltins() {
  _sealed = true;
}

void NewCommandMacro::_free_() {
  // A built-in displaced by the last parse is owned here, not by the
  // registry, which MacroInfo::_free_() has already emptied.
  for (auto& kv : _displaced) delete kv.second.info;
  _displaced.clear();
  delete _instance;
  _instance = nullptr;
  _codes.clear();
  _replacements.clear();
}

void MacroInfo::remove(const string& name) {
  auto it = _commands.find(name);
  if (it == _commands.end()) return;
  delete it->second;
  _commands.erase(it);
}

MacroInfo* MacroInfo::release(const string& name) {
  auto it = _commands.find(name);
  if (it == _commands.end()) return nullptr;
  MacroInfo* mac = it->second;
  _commands.erase(it);
  return mac;
}

void MacroInfo::add(const string& name, MacroInfo* mac) {
  auto it = _commands.find(name);
  if (it != _commands.end()) delete it->second;
  _commands[name] = mac;
}

MacroInfo* MacroInfo::get(const std::string& name) {
  auto it = _commands.find(name);
  if (it == _commands.end()) return nullptr;
  return it->second;
}

void MacroInfo::_free_() {
  for (const auto& i : _commands) delete i.second;
  _commands.clear();
}

sptr<Atom> PreDefMacro::invoke(Parser& tp, vector<string>& args) {
  try {
    return _delegate(tp, args);
  } catch (ex_parse& e) {
    throw ex_parse("Problem with command: " + args[0] + "\n caused by: " + e.what());
  }
}
