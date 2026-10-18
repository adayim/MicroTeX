#ifndef MICROTEX_ATOM_VROW_H
#define MICROTEX_ATOM_VROW_H

#include <cstddef>
#include <map>

#include "atom/atom.h"
#include "atom/atom_space.h"
#include "box/box_group.h"
#include "env/units.h"
#include "graphic/graphic_basic.h"

namespace microtex {

/** The width of the formula of a numbered display, and of its number, as the layout of the display finds them: where the number
 *  is laid out (a matrix or a multline with a tag) these are set, and DisplayAtom reads them right after, so that TeX's choice
 *  of display skips -- by where the formula starts, not by the measure the number makes the row -- can be made. */
inline thread_local float gDisplayFormulaWidth = -1.f;
inline thread_local float gDisplayNumberWidth = 0.f;

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
 *  document), centred in the text width as TeX centres one in \hsize; or a
 *  line of a centred or right-aligned paragraph, broken to the width and
 *  each of its lines aligned. With no text width, the VRowAtom it is a
 *  line of centres it on its widest line instead. */
class DisplayAtom : public Atom {
private:
  sptr<Atom> _base;
  Alignment _align;
  bool _equation = false, _interrupts = false, _fullWidth = false;
  float _innerWidth = 0;

public:
  /** `align`: centred (a display, \centering) or to the right
   *  (\raggedleft). */
  explicit DisplayAtom(const sptr<Atom>& base, Alignment align = Alignment::center)
      : _base(base), _align(align) {}

  Alignment alignment() const { return _align; }

  /** A display equation, which TeX sets off from the lines round it by the display skips (VRowAtom decides which);
   *  `interrupts`: text of its paragraph stands before it, so the width of that line chooses between the long skips
   *  and the short ones. */
  void asEquation(bool interrupts, bool fullWidth = false) {
    _fullWidth = fullWidth;
    _equation = true;
    _interrupts = interrupts;
  }
  bool isEquation() const { return _equation; }
  bool interrupts() const { return _interrupts; }
  /** An alignment fills the line, so it starts at its left edge, and its skips are the long ones after any text. */
  bool fullWidth() const { return _fullWidth; }
  /** The width of what the display holds (known once it has its box): where it starts in the line follows from it. */
  float innerWidth() const { return _innerWidth; }

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

/** A body set between margins, as LaTeX's quote, quotation and verse set theirs (lists whose margins are \leftmargin
 *  of 2.5em): narrower than the line by `left` and `right` em of the text around, and `left` in from its left edge. */
class IndentAtom : public Atom {
private:
  sptr<Atom> _body;
  float _left, _right;

public:
  IndentAtom(const sptr<Atom>& body, float left, float right) : _body(body), _left(left), _right(right) {}

  sptr<Box> createBox(Env& env) override;
};

/** What a tcolorbox looks like: the keys of the package that are drawn, with its defaults (tcolorbox.sty, `size=normal`).
 *  Lengths are in em of the text around, a millimetre being 0.2845em as at LaTeX's 10pt, so that a grob's size scales the
 *  box with its text. A transparent `backTitle` is a title on the colour of the frame. */
struct TcolorboxStyle {
  color back = 0xFFF2F2F2, frame = 0xFF404040, backTitle = transparent, titleText = 0xFFFFFFFF;
  /** `sharp corners`: no arc, inside or out. */
  bool sharp = false;
  float rule = 0.142264f, arc = 0.284528f, left = 1.138112f, right = 1.138112f, top = 0.569056f, bottom = 0.569056f,
        boxsep = 0.284528f;
};

/** `\begin{tcolorbox}[title=..]`: the body set to the width left by the frame and its padding, in a frame of
 *  `boxrule` whose inner corners have radius `arc` and outer ones `boxrule + arc` (auto outer arc), and above it, when
 *  there is a title, a band of the frame's colour (or `colbacktitle`) with the title in `coltitle`, set off from the
 *  body by a rule. It is as wide as the line, as tcolorbox is. */
class TcolorboxAtom : public Atom {
private:
  sptr<Atom> _body, _title;
  TcolorboxStyle _style;

public:
  TcolorboxAtom(const sptr<Atom>& body, const sptr<Atom>& title, const TcolorboxStyle& style)
      : _body(body), _title(title), _style(style) {}

  sptr<Box> createBox(Env& env) override;
};

/** The box of a multicols: its header and its row of columns, set as TeX sets a run of lines -- the line above is spaced
 *  to its first line, the one below from its last line's depth `_lastDepth`, not from the whole box's. */
class ColumnsBox : public VBox {
public:
  float _lastDepth = 0.f;

  boxname(ColumnsBox);
};

/** `\columnbreak`: a box with no size that the columns of a multicols are cut at, and which anywhere else draws nothing. */
class ColumnBreakAtom : public Atom {
public:
  sptr<Box> createBox(Env& env) override;
};

/** `\begin{multicols}{n}[header]`: the body set to the width of one column, `(width - (n-1)\columnsep) / n`, and its
 *  lines cut into `n` columns as even as they can be -- the shortest height they fit `n` columns in, each column filled
 *  from the top, as multicol balances them -- with the header above, over the whole width, and a `\multicolsep` (12pt)
 *  between it and the columns. A \columnbreak ends a column early. With no width to divide, the body is one column. */
class MulticolsAtom : public Atom {
private:
  sptr<Atom> _body, _header;
  int _count;
  Dimen _sep;

public:
  MulticolsAtom(const sptr<Atom>& body, const sptr<Atom>& header, int count, const Dimen& sep)
      : _body(body), _header(header), _count(count), _sep(sep) {}

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
