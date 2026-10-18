#ifndef MICROTEX_FRONT_LENGTHS_H
#define MICROTEX_FRONT_LENGTHS_H

#include <map>
#include <string>
#include <vector>

#include "env/units.h"

namespace microtex {

/**
 * LaTeX's named lengths of one input: \parindent, \tabcolsep, the ones a
 * document makes with \newlength. A length is one number and one unit, so
 * `\addtolength` of another kind of unit is refused. The built-in ones are in
 * em, as TeX's article class sets them at 10pt, so that they follow the text
 * size a grob is drawn at (a `pt` does not).
 *
 * A group ends what was set in it: every change is kept in a log, and a
 * LengthScope (front/lower.cpp) undoes the ones made since it began.
 */
class Lengths {
public:
  Lengths() {
    // article, 10pt.
    _values["parindent"] = {1.5f, UnitType::em};
    _values["parskip"] = {0.f, UnitType::em};
    _values["tabcolsep"] = {0.6f, UnitType::em};
    _values["arraycolsep"] = {0.5f, UnitType::em};
    _values["fboxsep"] = {0.3f, UnitType::em};
    _values["fboxrule"] = {0.04f, UnitType::em};
    _values["arrayrulewidth"] = {0.04f, UnitType::em};
    _values["baselineskip"] = {1.2f, UnitType::em};
    _values["columnsep"] = {1.f, UnitType::em};
  }

  bool get(const std::string& name, Dimen& out) const {
    const auto it = _values.find(name);
    if (it == _values.end()) return false;
    out = it->second;
    return true;
  }

  bool known(const std::string& name) const { return _values.count(name) > 0; }

  /** \newlength: a length of zero; global, so not logged. */
  void define(const std::string& name) { _values.emplace(name, Dimen{0.f, UnitType::pt}); }

  void set(const std::string& name, const Dimen& value) {
    const auto it = _values.find(name);
    _log.push_back({name, it != _values.end(), it != _values.end() ? it->second : Dimen{}});
    _values[name] = value;
  }

  std::size_t mark() const { return _log.size(); }

  /** Undo what was set since `mark`. */
  void release(std::size_t mark) {
    while (_log.size() > mark) {
      const Undo& u = _log.back();
      if (u.had) {
        _values[u.name] = u.old;
      } else {
        _values.erase(u.name);
      }
      _log.pop_back();
    }
  }

private:
  struct Undo {
    std::string name;
    bool had;
    Dimen old;
  };
  std::map<std::string, Dimen> _values;
  std::vector<Undo> _log;
};

}  // namespace microtex

#endif  // MICROTEX_FRONT_LENGTHS_H
