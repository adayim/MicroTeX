#ifndef MICROTEX_ATOM_FONT_H
#define MICROTEX_ATOM_FONT_H

#include "atom/atom.h"
#include "atom/atom_basic.h"
#include "env/env.h"
#include "graphic/font_style.h"

namespace microtex {

class FontStyleAtom : public Atom {
private:
  FontStyle _style = FontStyle::none;
  bool _mathMode = false;
  bool _nested = false;

  sptr<Atom> _atom;

public:
  FontStyleAtom() = delete;

  FontStyleAtom(FontStyle style, bool isMathMode, const sptr<Atom>& atom, bool nested = false)
      : _style(style), _mathMode(isMathMode), _nested(nested), _atom(atom) {
        if (_atom == nullptr)
	        _atom = sptrOf<EmptyAtom>();
      }

  AtomType leftType() const override { return _atom->leftType(); }

  AtomType rightType() const override { return _atom->rightType(); }

  // \text, \textbf, \textit, \textsf, \texttt and \textrm are all built
  // from this one type, so forwarding here is what lets a row see the
  // text of a sibling group at all.
  void collectBidiText(std::vector<c32>& out) const override {
    if (_atom != nullptr) _atom->collectBidiText(out);
  }

  void assignBidiLevels(const std::vector<std::uint8_t>& lv, std::size_t& cursor) override {
    const std::size_t start = cursor;
    if (_atom != nullptr) _atom->assignBidiLevels(lv, cursor);
    _bidiLevel = subtreeLevel(lv, start, cursor);
  }

  sptr<Box> createBox(Env& env) override;
};

/** Atom to modify math font and style */
class MathFontAtom : public Atom {
private:
  MathStyle _mathStyle;
  std::string _name;

public:
  MathFontAtom(MathStyle style, std::string name) : _mathStyle(style), _name(std::move(name)) {}

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_FONT_H
