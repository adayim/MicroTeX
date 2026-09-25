#ifndef GRIDMICROTEX_FRONT_LEXER_H
#define GRIDMICROTEX_FRONT_LEXER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "front/diagnostics.h"
#include "front/token.h"

namespace microtex::front {

/**
 * The category code of every ASCII character; code points above 127 are
 * always `other`. One table is shared by every lexer reading a document,
 * so a change made while reading a macro's expansion (`\makeatletter`)
 * still holds for the text after it.
 */
class CatcodeTable {
public:
  /** LaTeX's defaults. */
  CatcodeTable();

  Cat get(c32 ch) const { return ch < _cat.size() ? _cat[ch] : Cat::other; }

  void set(c32 ch, Cat cat) {
    if (ch < _cat.size()) _cat[ch] = cat;
  }

private:
  std::array<Cat, 128> _cat{};
};

struct LexOptions {
  /** Report a blank line as a `par` token, as TeX does. Without it a
   *  blank line is only whitespace, which is what math input wants. */
  bool blankLineIsPar = false;
  /** Start as if in the middle of a line. A macro's expansion is not the
   *  start of a line, so its leading spaces are text, not indentation. */
  bool startMidLine = false;
};

/**
 * Turns UTF-8 source into TeX tokens, one at a time.
 *
 * Tokens are made on demand, never ahead: a catcode change made after a
 * token is read applies to the very next one. That is how `\url` reads a
 * `%` as a character rather than a comment, and how `\makeatletter` makes
 * `@` part of a name. Reading ahead would have frozen the old catcodes.
 *
 * Whitespace follows TeX's three line states. A control word swallows the
 * spaces after it; a run of spaces is one space token; a line end is a
 * space; the spaces that start a line are dropped; a blank line is a
 * paragraph break (when asked for); and a `%` comment takes the line end
 * with it.
 *
 * Control-word names are ASCII letters only, plus `@` while it has the
 * letter catcode. Invalid UTF-8 becomes U+FFFD with a warning.
 *
 * Every step consumes input, so the lexer is linear in the input length.
 *
 * The lexer does not own its source: `source` and `catcodes` must outlive it.
 */
class Lexer {
public:
  Lexer(std::string_view source, LexOptions options, Diagnostics& diagnostics,
        CatcodeTable& catcodes);

  /** The next token. At the end of the input, an `end` token, every time. */
  Token next();

  /** Change the category of an ASCII character from the next token on.
   *  Other code points keep `other`. */
  void setCatcode(c32 ch, Cat cat) { _cat.set(ch, cat); }

  Cat catcode(c32 ch) const { return _cat.get(ch); }

  bool atEnd() const { return _pos >= _src.size(); }

  std::string_view source() const { return _src; }

private:
  enum class State : std::uint8_t { newLine, midLine, skipBlanks };

  std::string_view _src;
  std::size_t _pos = 0;
  std::size_t _lastEnd = 0;
  std::uint32_t _line = 1;
  std::uint32_t _col = 1;
  State _state = State::newLine;
  std::uint16_t _pendingLineEnds = 0;
  CatcodeTable& _cat;
  LexOptions _opts;
  Diagnostics& _diags;

  /** Decode the code point at `at`, setting `len` to its byte length.
   *  Invalid or truncated sequences give U+FFFD and a length of 1. */
  c32 decode(std::size_t at, int& len) const;

  /** Line-end length at `at`: 2 for CR LF, 1 for CR or LF, else 0. */
  int lineEndAt(std::size_t at) const;

  /** Move past `bytes` bytes forming `cps` code points on one line. */
  void advance(std::size_t bytes, std::uint32_t cps);

  /** Move past a line end of `bytes` bytes. */
  void advanceLine(int bytes);

  Token make(TokKind kind, std::size_t start, std::uint32_t line, std::uint32_t col);

  Token controlSequence();
  Token character(c32 cp, int len);
  void skipComment();
};

}  // namespace microtex::front

#endif
