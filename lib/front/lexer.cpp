#include "front/lexer.h"

#include <cctype>
#include <string>

#include "utils/utf.h"

namespace microtex::front {

namespace {

constexpr c32 kReplacement = 0xFFFD;
constexpr const char* kReplacementUtf8 = "\xEF\xBF\xBD";

}  // namespace

CatcodeTable::CatcodeTable() {
  // LaTeX's catcodes. Every other ASCII character is `other`.
  _cat.fill(Cat::other);
  for (char c = 'a'; c <= 'z'; c++) _cat[static_cast<std::size_t>(c)] = Cat::letter;
  for (char c = 'A'; c <= 'Z'; c++) _cat[static_cast<std::size_t>(c)] = Cat::letter;
  _cat['\\'] = Cat::escape;
  _cat['{'] = Cat::beginGroup;
  _cat['}'] = Cat::endGroup;
  _cat['$'] = Cat::mathShift;
  _cat['&'] = Cat::alignTab;
  _cat['\r'] = Cat::endLine;
  _cat['\n'] = Cat::endLine;
  _cat['#'] = Cat::param;
  _cat['^'] = Cat::superscript;
  _cat['_'] = Cat::subscript;
  _cat[0] = Cat::ignored;
  _cat[' '] = Cat::space;
  _cat['\t'] = Cat::space;
  _cat['~'] = Cat::active;
  _cat['%'] = Cat::comment;
  _cat[127] = Cat::invalid;
}

Lexer::Lexer(std::string_view source, LexOptions options, Diagnostics& diagnostics,
             CatcodeTable& catcodes)
    : _src(source), _cat(catcodes), _opts(options), _diags(diagnostics) {
  if (_opts.startMidLine) _state = State::midLine;
  // A byte-order mark is an encoding signature, not text.
  if (_src.compare(0, 3, "\xEF\xBB\xBF") == 0) _pos = _lastEnd = 3;
}

c32 Lexer::decode(std::size_t at, int& len) const {
  const auto* s = reinterpret_cast<const unsigned char*>(_src.data());
  const std::size_t n = _src.size();
  const unsigned char b0 = s[at];
  len = 1;
  if (b0 < 0x80) return b0;

  int need = 0;
  c32 cp = 0;
  c32 least = 0;
  if ((b0 & 0xE0) == 0xC0) {
    need = 1, cp = b0 & 0x1F, least = 0x80;
  } else if ((b0 & 0xF0) == 0xE0) {
    need = 2, cp = b0 & 0x0F, least = 0x800;
  } else if ((b0 & 0xF8) == 0xF0) {
    need = 3, cp = b0 & 0x07, least = 0x10000;
  } else {
    return kReplacement;
  }
  if (at + need >= n) return kReplacement;
  for (int i = 1; i <= need; i++) {
    const unsigned char b = s[at + i];
    if ((b & 0xC0) != 0x80) return kReplacement;
    cp = (cp << 6) | (b & 0x3F);
  }
  // Overlong forms, surrogates and values past Unicode are not characters.
  if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return kReplacement;
  len = need + 1;
  return cp;
}

int Lexer::lineEndAt(std::size_t at) const {
  if (at >= _src.size()) return 0;
  if (_src[at] == '\n') return 1;
  if (_src[at] == '\r') return (at + 1 < _src.size() && _src[at + 1] == '\n') ? 2 : 1;
  return 0;
}

void Lexer::advance(std::size_t bytes, std::uint32_t cps) {
  _pos += bytes;
  _col += cps;
}

void Lexer::advanceLine(int bytes) {
  _pos += static_cast<std::size_t>(bytes);
  _line++;
  _col = 1;
}

Token Lexer::make(TokKind kind, std::size_t start, std::uint32_t line, std::uint32_t col) {
  Token t;
  t.kind = kind;
  t.span = {static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(_pos - start), line, col};
  t.leadStart = static_cast<std::uint32_t>(_lastEnd);
  _lastEnd = _pos;
  t.lineEnds = _pendingLineEnds;
  _pendingLineEnds = 0;
  return t;
}

Token Lexer::next() {
  while (_pos < _src.size()) {
    const std::size_t start = _pos;
    const std::uint32_t line = _line;
    const std::uint32_t col = _col;
    int len = 1;
    const c32 cp = decode(_pos, len);
    // An invalid byte decodes to U+FFFD with length 1 and is drawn as that.
    const bool invalidUtf8 = (cp == kReplacement && len == 1);
    const Cat cat = invalidUtf8 ? Cat::other : catcode(cp);

    switch (cat) {
      case Cat::escape:
        return controlSequence();

      case Cat::endLine: {
        const int eol = lineEndAt(_pos);
        advanceLine(eol > 0 ? eol : len);
        const State was = _state;
        _state = State::newLine;
        _pendingLineEnds++;
        if (was == State::midLine) {
          Token t = make(TokKind::space, start, line, col);
          t.text = " ";
          return t;
        }
        if (was == State::newLine && _opts.blankLineIsPar) {
          return make(TokKind::par, start, line, col);
        }
        continue;
      }

      case Cat::space:
        advance(static_cast<std::size_t>(len), 1);
        if (_state == State::midLine) {
          // The rest of the run is dropped in the skipBlanks state.
          _state = State::skipBlanks;
          Token t = make(TokKind::space, start, line, col);
          t.text = " ";
          return t;
        }
        continue;

      case Cat::comment:
        skipComment();
        continue;

      case Cat::ignored:
        advance(static_cast<std::size_t>(len), 1);
        continue;

      case Cat::invalid:
        _diags.warn({static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(len), line, col},
                    "invalid character ignored");
        advance(static_cast<std::size_t>(len), 1);
        continue;

      default:
        return character(cp, len);
    }
  }
  return make(TokKind::end, _pos, _line, _col);
}

Token Lexer::controlSequence() {
  const std::size_t start = _pos;
  const std::uint32_t line = _line;
  const std::uint32_t col = _col;
  advance(1, 1);  // the backslash

  if (_pos >= _src.size()) {
    _diags.warn({static_cast<std::uint32_t>(start), 1, line, col},
                "a backslash at the end of the input is ignored");
    return next();
  }

  int len = 1;
  const c32 cp = decode(_pos, len);
  const bool invalidUtf8 = (cp == kReplacement && len == 1);

  // As KaTeX has it, `@` is a letter in a name that starts with it, as if
  // \makeatletter were in effect (\@ifstar, \@firstoftwo): a bare `\@` stays
  // TeX's control symbol.
  const bool atName = !invalidUtf8 && _src[_pos] == '@' && catcode(cp) != Cat::letter &&
                      _pos + 1 < _src.size() && std::isalpha(static_cast<unsigned char>(_src[_pos + 1]));
  if (atName || (!invalidUtf8 && catcode(cp) == Cat::letter)) {
    const std::size_t nameStart = _pos;
    if (atName) advance(1, 1);
    while (_pos < _src.size()) {
      const auto b = static_cast<unsigned char>(_src[_pos]);
      if (b >= 0x80 || (_cat.get(b) != Cat::letter && !(atName && b == '@'))) break;
      advance(1, 1);
    }
    _state = State::skipBlanks;
    Token t = make(TokKind::controlWord, start, line, col);
    t.text = std::string(_src.substr(nameStart, _pos - nameStart));
    return t;
  }

  // A backslash before a line end is TeX's control space, `\ `.
  const int eol = invalidUtf8 ? 0 : (catcode(cp) == Cat::endLine ? lineEndAt(_pos) : 0);
  if (eol > 0) {
    advanceLine(eol);
    _state = State::newLine;
    Token t = make(TokKind::controlSymbol, start, line, col);
    t.text = " ";
    return t;
  }

  if (invalidUtf8) {
    _diags.warn({static_cast<std::uint32_t>(_pos), 1, _line, _col},
                "invalid UTF-8 byte; drawn as U+FFFD");
  }
  advance(static_cast<std::size_t>(len), 1);
  const bool space = !invalidUtf8 && catcode(cp) == Cat::space;
  _state = space ? State::skipBlanks : State::midLine;
  Token t = make(TokKind::controlSymbol, start, line, col);
  t.text = invalidUtf8 ? kReplacementUtf8 : space ? " " : std::string(_src.substr(start + 1, static_cast<std::size_t>(len)));
  return t;
}

Token Lexer::character(c32 cp, int len) {
  const std::size_t start = _pos;
  const std::uint32_t line = _line;
  const std::uint32_t col = _col;
  const bool invalidUtf8 = (cp == kReplacement && len == 1);
  if (invalidUtf8) {
    _diags.warn({static_cast<std::uint32_t>(start), 1, line, col},
                "invalid UTF-8 byte; drawn as U+FFFD");
  }
  advance(static_cast<std::size_t>(len), 1);
  const Cat cat = invalidUtf8 ? Cat::other : catcode(cp);

  // Variation selectors, and anything joined on with a zero-width joiner,
  // belong to this character: drawn apart they would be wrong or invisible.
  // A joined ASCII character is only taken when it is a plain letter or
  // symbol, never one that means something to TeX.
  if (!invalidUtf8 && (cat == Cat::letter || cat == Cat::other)) {
    while (_pos < _src.size()) {
      int l2 = 1;
      const c32 next = decode(_pos, l2);
      if (next == kReplacement && l2 == 1) break;
      if (isVariationSelector(next)) {
        advance(static_cast<std::size_t>(l2), 1);
        continue;
      }
      if (!isJoiner(next)) break;
      advance(static_cast<std::size_t>(l2), 1);
      if (_pos >= _src.size()) break;
      int l3 = 1;
      const c32 joined = decode(_pos, l3);
      if (joined == kReplacement && l3 == 1) break;
      const Cat jc = catcode(joined);
      if (jc == Cat::letter || jc == Cat::other) advance(static_cast<std::size_t>(l3), 1);
    }
  }

  _state = State::midLine;
  Token t = make(TokKind::character, start, line, col);
  t.cat = cat;
  t.cp = cp;
  t.text = invalidUtf8 ? kReplacementUtf8 : std::string(_src.substr(start, _pos - start));
  return t;
}

void Lexer::skipComment() {
  // TeX drops the comment and the line end after it, so text either side
  // of a `%` at the end of a line joins up with no space between.
  while (_pos < _src.size()) {
    const int eol = lineEndAt(_pos);
    if (eol > 0) {
      advanceLine(eol);
      break;
    }
    int len = 1;
    decode(_pos, len);
    advance(static_cast<std::size_t>(len), 1);
  }
  _state = State::newLine;
}

}  // namespace microtex::front
