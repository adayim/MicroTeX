#ifndef MICROTEX_ATOM_SPACE_H
#define MICROTEX_ATOM_SPACE_H

#include "atom/atom.h"
#include "utils/utils.h"

namespace microtex {

/** An atom representing whitespace. */
class SpaceAtom : public Atom {
private:
  bool _isMathMode = true;
  // whether a hard space should be represented
  bool _blankSpace = false;
  // thin-mu-skip, med-mu-skip, thick-mu-skip
  SpaceType _blankType{};
  // dimensions
  float _width = 0, _height = 0, _depth = 0;
  // units of the dimensions
  UnitType _unit{};

public:
  explicit SpaceAtom(bool isMathMode = true) noexcept
      : _isMathMode(isMathMode), _blankSpace(true) {}

  explicit SpaceAtom(SpaceType type) noexcept : _blankSpace(true), _blankType(type) {}

  SpaceAtom(UnitType unit, float width, float height, float depth) noexcept
      : _width(width), _height(height), _depth(depth), _unit(unit) {}

  /**
   * Whether this is an ordinary space between words, as opposed to a
   * measured skip such as `\quad` or an explicit dimension.
   *
   * RowAtom needs to tell them apart: a word space may be folded into the
   * text run around it so the device sees a whole phrase, a `\quad` may
   * not. These are the two conditions createBox() itself branches on.
   */
  bool isInterword() const { return _blankSpace && _blankType == SpaceType::none; }

  void collectBidiText(std::vector<c32>& out) const override {
    // Only the word space is text; a \quad is a measured skip.
    if (isInterword()) out.push_back(U' ');
  }

  void assignBidiLevels(const std::vector<std::uint8_t>& lv, std::size_t& cursor) override {
    if (!isInterword()) return;               // contributed no character
    Atom::assignBidiLevels(lv, cursor);
    cursor++;
  }

  sptr<Box> createBox(Env& env) override;

  static sptr<SpaceAtom> empty() { return sptrOf<SpaceAtom>(UnitType::em, 0.f, 0.f, 0.f); }
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_SPACE_H
