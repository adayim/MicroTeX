#ifndef ATOM_H_INCLUDED
#define ATOM_H_INCLUDED

#include "box/box.h"

namespace microtex {

class Env;

/**
 * An abstract superclass for all logical mathematical constructions that can be a part
 * of a Formula. The abstract method [createBox(Env)] transforms this logical unit into
 * a concrete box (that can be painted).
 *
 * It also defines its type, used for determining what glue to use between adjacent atoms
 * in a "row construction". That can be one single type by assigning one of the type
 * constants to the [_type] field. But they can also be defined as having two types: a
 * "left type" and a "right type". This can be done by implementing the methods
 * [leftType()] and [rightType()]. The left type will then be used for determining the
 * glue between this atom and the previous one (in a row, if any) and the right type for
 * the glue between this atom and the following one (in a row, if any).
 *
 * And the [_limitsType] used for determining what variant to use when constructing scripts,
 * defaults to [LimitsType::noLimits].
 */
class Atom {
public:
  /** The type of the atom (default value: ordinary atom) */
  AtomType _type = AtomType::ordinary;
  /** The limits type of the atom (default value: nolimits) */
  LimitsType _limitsType = LimitsType::noLimits;
  /** The alignment type of the atom (default value: none) */
  Alignment _alignment = Alignment::none;

  Atom() = default;

  // Once a destructor is user-declared the implicit copy operations are
  // deprecated (-Wdeprecated-copy-with-dtor), and subclasses do copy the
  // base -- CharSymbol in atom_char.h is one. Spell them out so the
  // behaviour is the same but no longer rests on a deprecated definition.
  Atom(const Atom&) = default;
  Atom& operator=(const Atom&) = default;

  /**
   * Get the type of the leftmost child atom. Most atoms have no child
   * atoms, so the "left type" and the "right type" are the same: the atom's
   * type. This also is the default implementation. But Some atoms are
   * composed of child atoms put one after another in a horizontal row. These
   * atoms must override this method.
   *
   * @return the type of the leftmost child atom
   */
  virtual AtomType leftType() const { return _type; }

  /**
   * Get the type of the rightmost child atom. Most atoms have no child
   * atoms, so the "left type" and the "right type" are the same: the atom's
   * type. This also is the default implementation. But Some atoms are
   * composed of child atoms put one after another in a horizontal row. These
   * atoms must override this method.
   *
   * @return the type of the rightmost child atom
   */
  virtual AtomType rightType() const { return _type; }

  /**
   * Convert this atom into a Box, using properties set by "parent"
   * atoms, like the TeX style, the last used font, color settings, ...
   *
   * @param env the current environment settings
   *
   * @return the resulting box.
   */
  virtual sptr<Box> createBox(Env& env) = 0;

  /** Test if this atom is a single character */
  virtual bool isChar() const { return false; }

  /**
   * Append the text this atom represents, for resolving bidirectional
   * embedding levels. A row needs the text of its whole subtree, not just
   * of the characters that happen to be its direct children: `\text{a}`
   * and `\textbf{b}` are sibling wrappers, and a row that saw neither of
   * them collected an empty string and was never reordered.
   *
   * The default appends nothing, so an atom that does not override this
   * simply takes no part in reordering -- which is what every atom did
   * before the hook existed.
   */
  virtual void collectBidiText(std::vector<c32>& out) const { (void)out; }

  /**
   * The embedding level this atom sits at, resolved once per parse over
   * the whole formula's text -- see the pre-pass in src/parse_latex.cpp.
   *
   * Resolving per row instead would ask FriBidi to judge each group's
   * direction from that group's text alone, so `\textbf{abc}` inside a
   * right-to-left paragraph came out level 0 where UAX #9 says 2, and a
   * neutral run spanning a group boundary was resolved twice against two
   * different contexts.
   */
  std::uint8_t _bidiLevel = 0;

  /**
   * Write levels onto this atom and its subtree, in document order.
   *
   * `cursor` indexes the text that collectBidiText() produced, so the two
   * traversals must visit the same atoms in the same order -- they are
   * written next to each other in each file for that reason. The default
   * takes a level but does not advance, which is right for an atom that
   * contributes no text.
   */
  virtual void assignBidiLevels(const std::vector<std::uint8_t>& lv, std::size_t& cursor) {
    if (cursor < lv.size()) _bidiLevel = lv[cursor];
  }

  /**
   * The level a container takes for itself: the **lowest** in its subtree.
   *
   * Not its first character's level. Rule L2 reverses runs at or above
   * the lowest odd level, and a container is reversed as a unit by its
   * parent, so it must join every run its contents belong to. Labelling
   * `\text{alpha }\textbf{...}` by its first character gave two adjacent
   * groups level 2 in a right-to-left paragraph; the parent then saw no
   * odd level at all and reversed nothing, while the words inside each
   * group reversed happily. Taking the minimum puts the container in the
   * run its right-to-left content belongs to, and the levels above that
   * minimum are resolved inside it, one level down.
   */
  static std::uint8_t subtreeLevel(
    const std::vector<std::uint8_t>& lv, std::size_t start, std::size_t end
  ) {
    if (start >= end) return start < lv.size() ? lv[start] : 0;
    std::uint8_t m = lv[start];
    for (std::size_t i = start + 1; i < end && i < lv.size(); i++) {
      if (lv[i] < m) m = lv[i];
    }
    return m;
  }

  virtual ~Atom() = default;
};

/** Atom decor contains another atom */
class WrapAtom : public Atom {
protected:
  sptr<Atom> _base;

public:
  explicit WrapAtom(const sptr<Atom>& base);

  inline sptr<Atom> base() const { return _base; }

  AtomType leftType() const override {
    return _base == nullptr ? Atom::leftType() : _base->leftType();
  }

  AtomType rightType() const override {
    return _base == nullptr ? Atom::rightType() : _base->rightType();
  }

  void collectBidiText(std::vector<c32>& out) const override {
    if (_base != nullptr) _base->collectBidiText(out);
  }

  void assignBidiLevels(const std::vector<std::uint8_t>& lv, std::size_t& cursor) override {
    const std::size_t start = cursor;
    if (_base != nullptr) _base->assignBidiLevels(lv, cursor);
    _bidiLevel = subtreeLevel(lv, start, cursor);
  }
};

}  // namespace microtex

#endif  // ATOM_H_INCLUDED
