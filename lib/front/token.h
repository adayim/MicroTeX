#ifndef GRIDMICROTEX_FRONT_TOKEN_H
#define GRIDMICROTEX_FRONT_TOKEN_H

#include <cstdint>
#include <string>

#include "front/diagnostics.h"
#include "utils/types.h"

namespace microtex::front {

/**
 * TeX's category codes: what a character means to the lexer. The values
 * are TeX's own numbers, so a table can be read against The TeXbook.
 */
enum class Cat : std::uint8_t {
  escape = 0,       // "\"
  beginGroup = 1,   // "{"
  endGroup = 2,     // "}"
  mathShift = 3,    // "$"
  alignTab = 4,     // "&"
  endLine = 5,      // line end
  param = 6,        // "#"
  superscript = 7,  // "^"
  subscript = 8,    // "_"
  ignored = 9,
  space = 10,       // space, tab
  letter = 11,      // A-Z a-z, and "@" under \makeatletter
  other = 12,       // everything else
  active = 13,      // "~"
  comment = 14,     // "%"
  invalid = 15,
};

enum class TokKind : std::uint8_t {
  /** `\alpha`: `text` is the name without the backslash. */
  controlWord,
  /** `\,` `\{` `\\` `\ `: `text` is the single character after the backslash. */
  controlSymbol,
  /** One character with its category in `cat`. `text` holds its UTF-8,
   *  including any variation selectors and joined code points after it,
   *  which cannot be drawn apart from it; `cp` is the first code point. */
  character,
  /** Spaces and at most one line end, collapsed into one token. */
  space,
  /** A blank line, when the lexer is asked to report paragraphs. */
  par,
  /** The end of the input. Returned again on every further call. */
  end,
};

struct Token {
  TokKind kind = TokKind::end;
  Cat cat = Cat::other;
  c32 cp = 0;
  std::string text;
  SourceSpan span;
  /** Byte offset, in the lexer's own source, where the whitespace and
   *  comments before this token begin. With `span` it recovers the exact
   *  source text between tokens. */
  std::uint32_t leadStart = 0;
  /** Line ends in the whitespace right before this token, including any a
   *  space token itself stands for. TeX drops some of them (after a
   *  control word, at the start of a line); the plot-label reading of a
   *  newline as a line break still needs to know they were there. */
  std::uint16_t lineEnds = 0;
  /** Set by \noexpand: the expander must pass this token through as is. */
  bool noexpand = false;
  /** Set on the `{` the expander opens a prelude environment's expansion
   *  with: 1 for one with code of its own (tabular, pmatrix), which a label
   *  sets as a unit, not as prose; 2 for one with none (document, table),
   *  whose content a label sets as if it were not there; 3 for a display
   *  (equation, displaymath), a unit that a document sets on a line of its
   *  own; 4 for a float (table, figure) and 5 for center, which have no
   *  code either but which a document sets apart from its paragraphs. */
  std::uint8_t environment = 0;

  bool isControl() const {
    return kind == TokKind::controlWord || kind == TokKind::controlSymbol;
  }

  /** A control sequence named `name` (without the backslash). */
  bool isCs(const std::string& name) const { return isControl() && text == name; }

  bool isChar(Cat c) const { return kind == TokKind::character && cat == c; }

  /** A character token for the ASCII character `c`. */
  bool isCharCode(char c) const {
    return kind == TokKind::character && cp == static_cast<unsigned char>(c);
  }
};

}  // namespace microtex::front

#endif
