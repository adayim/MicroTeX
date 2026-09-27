#ifndef MICROTEX_MACRO_FONTS_H
#define MICROTEX_MACRO_FONTS_H

#include "atom/atom_font.h"
#include "atom/atom_misc.h"
#include "macro/macro_decl.h"
#include "unimath/uni_font.h"
#include "utils/utf.h"

namespace microtex {

// \text and \mbox: upright text in the current text font, as amsmath's
// \text and LaTeX's \mbox set it -- bold inside \textbf, italic inside
// \textit -- so the style around them is added to, not replaced.
inline cmdmacro(text) {
  const auto atom = args.formula(1, false);
  return sptrOf<FontStyleAtom>(FontStyle::rm, false, atom, true);
}

// \textnormal: LaTeX's \normalfont -- upright, medium, in the caller's
// family -- whatever is around it, so the style around it is replaced.
inline cmdmacro(textnormal) {
  return sptrOf<FontStyleAtom>(FontStyle::rm, false, args.formula(1, false));
}

// \oldstylenums{digits}: the digits as text. Old-style figures are a font
// feature the engine does not select, so they are the font's own figures;
// the \textfont handler it shared with \cal lost them altogether.
inline cmdmacro(oldstylenums) {
  return sptrOf<FontStyleAtom>(FontStyle::rm, false, args.formula(1, false));
}

inline sptr<Atom> _textfontnested(CommandArgs& args, FontStyle style) {
  const auto atom = args.formula(1, false);
  return sptrOf<FontStyleAtom>(style, false, atom, true);
}

inline cmdmacro(textit) {
  return _textfontnested(args, FontStyle::it);
}

inline cmdmacro(textbf) {
  return _textfontnested(args, FontStyle::bf);
}

inline cmdmacro(textsf) {
  return _textfontnested(args, FontStyle::sf);
}

inline cmdmacro(texttt) {
  return _textfontnested(args, FontStyle::tt);
}

inline cmdmacro(textrm) {
  return _textfontnested(args, FontStyle::rm);
}

inline sptr<Atom> _mathfont(CommandArgs& args, FontStyle style) {
  const auto atom = args.formula(1, args.isMathMode());
  return sptrOf<FontStyleAtom>(style, true, atom);
}

inline cmdmacro(mathfont) {
  return _mathfont(args, FontContext::mathFontStyleOf(args.text(0)));
}

inline cmdmacro(Bbb) {
  return _mathfont(args, FontStyle::bb);
}

inline cmdmacro(mathds) {
  return _mathfont(args, FontStyle::bb);
}

inline cmdmacro(bold) {
  return _mathfont(args, FontStyle::bf);
}

cmdmacro(intertext);

cmdmacro(mathversion);

}  // namespace microtex

#endif  // MICROTEX_MACRO_FONTS_H
