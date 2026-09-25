#ifndef MICROTEX_ATOM_VROW_H
#define MICROTEX_ATOM_VROW_H

#include <cstddef>
#include <map>

#include "atom/atom.h"
#include "atom/atom_space.h"
#include "env/units.h"

namespace microtex {

/** An atom representing a vertical row of other atoms. */
class VRowAtom : public Atom {
private:
  std::vector<sptr<Atom>> _elements;
  sptr<SpaceAtom> _raise;
  bool _addInterline;
  /** Extra space below an element, by its index: `\\[len]`. */
  std::map<std::size_t, Dimen> _gaps;

public:
  Alignment _valign = Alignment::none;
  Alignment _halign = Alignment::none;

  VRowAtom();

  explicit VRowAtom(const sptr<Atom>& base);

  inline void setAddInterline(bool addInterline) { _addInterline = addInterline; }

  inline bool isAddInterline() const { return _addInterline; }

  inline void setAlignTop(bool vtop) { _valign = vtop ? Alignment::top : Alignment::center; }

  inline bool isAlignTop() const { return _valign == Alignment::top; }

  void setRaise(UnitType unit, float r);

  /** Add an atom at the front */
  void prepend(const sptr<Atom>& el);

  /** Add an atom at the tail */
  void append(const sptr<Atom>& el);

  /** Extra space below the last atom added, before the next one. */
  void addGapAfterLast(const Dimen& gap);

  sptr<Box> createBox(Env& env) override;
};

/** A display on a line of its own (`\[...\]`, equation, align ... in a
 *  document), centred in the text width as TeX centres one in \hsize. With
 *  no text width, the VRowAtom it is a line of centres it on its widest
 *  line instead. */
class DisplayAtom : public Atom {
private:
  sptr<Atom> _base;

public:
  explicit DisplayAtom(const sptr<Atom>& base) : _base(base) {}

  sptr<Box> createBox(Env& env) override;
};

/** LaTeX's minipage: its body, a little document of its own, set to its
 *  width -- which is its text width, so paragraphs break to it and
 *  \centering centres in it -- and placed in the line as one box. The
 *  line's baseline meets its first line (`t`), its last (`b`), or its
 *  middle (`c`, on the math axis). A height taller than the body leaves
 *  the room below it (inner position `t`), above it (`b`), or around it
 *  (`c`, `s`). */
class MinipageAtom : public Atom {
private:
  sptr<Atom> _body;
  Dimen _width;
  char _position;
  Dimen _height;
  char _inner;

public:
  MinipageAtom(const sptr<Atom>& body, const Dimen& width, char position, const Dimen& height,
               char inner)
      : _body(body), _width(width), _position(position), _height(height), _inner(inner) {}

  sptr<Box> createBox(Env& env) override;
};

/** A heading's number hung in the margin of its title, as LaTeX's
 *  \@hangfrom sets it: a title wider than the text width is broken, and
 *  its lines after the first start under its first, not under the number.
 *  `whole` is the heading set as one line, which is used as it is
 *  whenever it fits; `lead` is the number and the space after it, `rest`
 *  the title, each in the heading's font and size. */
class HangingAtom : public Atom {
private:
  sptr<Atom> _whole, _lead, _rest;

public:
  HangingAtom(const sptr<Atom>& whole, const sptr<Atom>& lead, const sptr<Atom>& rest)
      : _whole(whole), _lead(lead), _rest(rest) {}

  sptr<Box> createBox(Env& env) override;
};

}  // namespace microtex

#endif  // MICROTEX_ATOM_VROW_H
