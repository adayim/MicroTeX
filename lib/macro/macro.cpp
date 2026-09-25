#include "macro/macro.h"

#include <string>
#include <vector>

using namespace std;
using namespace microtex;

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

std::vector<std::string> MacroInfo::names() {
  std::vector<std::string> out;
  out.reserve(_commands.size());
  for (const auto& kv : _commands) out.push_back(kv.first);
  return out;
}

void MacroInfo::_free_() {
  for (const auto& i : _commands) delete i.second;
  _commands.clear();
}
