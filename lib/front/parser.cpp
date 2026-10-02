#include "front/parser.h"

#include <algorithm>
#include <utility>

#include "utils/utf.h"

namespace microtex::front {

namespace {

bool isOther(const Token& t, char c) {
  return t.kind == TokKind::character && t.cat == Cat::other && t.cp == static_cast<unsigned char>(c);
}

// `b` is a `$` right after the `$` `a`, making `$$`: next to it in the
// source, or in the same macro's expansion (which is where both are
// placed). A definition between them -- `$\newcommand{..}{..}$`, which
// the expander consumes -- is something between them, as it is to TeX.
bool secondShift(const ExpandedToken& a, const ExpandedToken& b) {
  if (!b.tok.isChar(Cat::mathShift) || !b.lead.empty()) return false;
  return b.tok.span.offset == a.tok.span.offset + 1 || b.tok.span.offset == a.tok.span.offset;
}

std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

/** Sets a flag for a scope, and puts it back after. */
struct Scoped {
  bool& flag;
  bool was;
  Scoped(bool& f, bool value) : flag(f), was(f) { flag = value; }
  ~Scoped() { flag = was; }
  Scoped(const Scoped&) = delete;
  Scoped& operator=(const Scoped&) = delete;
};

char kindCode(ArgKind kind) {
  switch (kind) {
    case ArgKind::math: return 'm';
    case ArgKind::text: return 't';
    case ArgKind::current: return 'c';
    case ArgKind::raw: return 'r';
    case ArgKind::dimen: return 'd';
    case ArgKind::delim: return 'l';
    case ArgKind::url: return 'u';
  }
  return '?';
}

}  // namespace

Parser::Parser(Expander& input, Ast& ast, Diagnostics& diagnostics, ParserOptions options)
    : _in(input), _ast(ast), _diags(diagnostics), _opts(std::move(options)) {}

// --- tokens ------------------------------------------------------------------

ExpandedToken Parser::next() {
  // After a capacity error (a runaway macro, nesting past the limit) the
  // parse is an error whatever follows, so reading ends there, the tokens
  // put back included, as the expander's input ends. Working on through
  // what a runaway left collected -- an optional argument never closed,
  // read again at every level it nests -- cost seconds and gigabytes.
  if (_diags.firstError() != nullptr) {
    if (!_ahead.empty()) _ahead.pop_back();
    ExpandedToken end;
    end.tok.kind = TokKind::end;
    _lastReplay = false;
    _lastOrigin = kFresh;
    _consumed++;
    return end;
  }
  ExpandedToken t;
  bool replay = false;
  std::size_t origin = kFresh;
  if (!_ahead.empty()) {
    t = std::move(_ahead.back().tok);
    replay = _ahead.back().replay;
    origin = _ahead.back().origin;
    _ahead.pop_back();
  } else {
    t = _in.next();
  }
  _lastReplay = replay;
  _lastOrigin = origin;
  // A token read again is recorded again, for a recording begun since it was
  // first read; one begun before has it already (stopRecording()).
  if (_recording > 0) _log.push_back({_consumed, t, origin});
  _consumed++;
  return t;
}

void Parser::unread(ExpandedToken t) {
  _consumed--;
  if (!_log.empty() && _log.back().index == _consumed) _log.pop_back();
  _ahead.push_back({std::move(t), _lastReplay, _lastOrigin});
}

const ExpandedToken& Parser::peek() {
  ExpandedToken t = next();
  unread(std::move(t));
  return _ahead.back().tok;
}

const ExpandedToken& Parser::peekNonSpace() {
  ExpandedToken t = nextNonSpace();
  unread(std::move(t));
  return _ahead.back().tok;
}

ExpandedToken Parser::nextNonSpace() {
  ExpandedToken t = next();
  while (t.tok.kind == TokKind::space) t = next();
  return t;
}

std::size_t Parser::startRecording() {
  if (_recording == 0) _log.clear();
  _recording++;
  return _consumed;
}

std::string Parser::stopRecording(std::size_t mark, std::size_t dropLast, bool firstLead) {
  _recording--;
  const std::size_t end = _consumed - dropLast;
  std::string s;
  bool first = true;
  // The log is in consumption order, so the argument's first token is
  // found by bisection: scanning from the front made nested arguments
  // quadratic in the size of the outermost one.
  const auto from = std::lower_bound(_log.begin(), _log.end(), mark,
                                     [](const Logged& l, std::size_t m) { return l.index < m; });
  for (auto it = from; it != _log.end(); ++it) {
    const Logged& l = *it;
    if (l.index >= end) {
      // A dropped closing token: its lead is still inside the argument.
      if (l.index == end) s += l.tok.lead;
      break;
    }
    // Read again, and first read since `mark`: recorded then.
    if (l.origin != kFresh && l.origin >= mark) continue;
    if (!first || firstLead) s += l.tok.lead;
    s += l.tok.text;
    first = false;
  }
  return s;
}

// --- structure ---------------------------------------------------------------

bool Parser::atStop(const ExpandedToken& t, const Stop& stop) const {
  const Token& k = t.tok;
  if (k.kind == TokKind::end) return true;
  if (stop.group && k.isChar(Cat::endGroup)) return true;
  if ((stop.cell || stop.overArg) &&
      (k.isChar(Cat::alignTab) || k.isCs("\\") || k.isCs("cr"))) {
    return true;
  }
  // A line end in mixed-mode prose is a `\\`, and stops where one does. In
  // a document it is a space, and a paragraph break does not end a
  // declaration either: \small lasts to the end of its group, as in TeX.
  if (stop.overArg && _prose && _opts.lineEndsBreak && k.lineEnds > 0) return true;
  if (stop.lineEnd && k.lineEnds > 0) return true;
  if ((stop.cell || stop.end) && k.isCs("end")) return true;
  if (stop.right && (k.isCs("right") || k.isCs("middle"))) return true;
  if ((stop.dollar || stop.displayDollar) && k.isChar(Cat::mathShift)) return true;
  if (!stop.closeSymbol.empty() && k.kind == TokKind::controlSymbol && k.text == stop.closeSymbol) {
    return true;
  }
  return false;
}

NodeId Parser::emptyList(SourceSpan at, Mode mode) {
  Node n;
  n.kind = NodeKind::list;
  n.mode = mode;
  n.span = at;
  return _ast.add(std::move(n), {});
}

NodeId Parser::lineBreak(SourceSpan at, Mode mode, bool paragraph) {
  Node n;
  n.kind = NodeKind::command;
  n.mode = mode;
  n.span = at;
  n.text = "\\";
  // A paragraph is a line break that also carries the space between
  // paragraphs, and indents what follows it.
  n.aux = paragraph ? 1 : 0;
  return _ast.add(std::move(n), {absentArgument(at, mode)});
}

NodeId Parser::absentArgument(SourceSpan at, Mode mode) {
  Node n;
  n.kind = NodeKind::argument;
  n.mode = mode;
  n.span = at;
  n.flag = false;
  return _ast.add(std::move(n), {});
}

NodeId Parser::character(const ExpandedToken& t, Mode mode) {
  Node n;
  n.kind = NodeKind::character;
  n.mode = mode;
  n.span = t.tok.span;
  n.cp = t.tok.cp;
  n.text = t.tok.text;
  return _ast.add(std::move(n), {});
}

NodeId Parser::parse() {
  _prose = (_opts.lineEndsBreak || _opts.parBreaks) && _opts.startMode == Mode::text;
  std::vector<NodeId> items;
  _depth++;
  if (_opts.bodyStart > 0) {
    // A whole file: its preamble is read as if the input ended where the
    // body begins -- for every reader, a macro's arguments included -- so
    // that nothing begun in it (a declaration such as \large, an
    // environment, \over) takes the body into what is read only for what
    // it defines. Its warnings are about settings a grob cannot honour.
    _in.endInputAt(_opts.bodyStart);
    {
      const Diagnostics::Quiet quiet(_diags);
      readItems(_opts.startMode, Stop{}, items);
    }
    _ast.preamble = static_cast<std::uint32_t>(items.size());
    // That end was not the input's.
    _ahead.erase(std::remove_if(_ahead.begin(), _ahead.end(),
                                [](const Pending& p) { return p.tok.tok.kind == TokKind::end; }),
                 _ahead.end());
  }
  // What follows \end{document} is never read, as LaTeX never reads it.
  _in.endInputAt(_opts.bodyEnd);
  readItems(_opts.startMode, Stop{}, items);
  _depth--;
  Node root;
  root.kind = NodeKind::list;
  root.mode = _opts.startMode;
  _ast.root = _ast.add(std::move(root), items);
  // Anything left over is after a stray closing token at the top level.
  while (true) {
    ExpandedToken t = next();
    if (t.tok.kind == TokKind::end) break;
    _diags.warn(t.tok.span, "extra " + t.text + " ignored");
  }
  return _ast.root;
}

NodeId Parser::parseList(Mode mode, const Stop& stop, SourceSpan at) {
  std::vector<NodeId> items;
  // Math is not prose, and neither is text inside it (\text{} in a formula).
  const Scoped prose(_prose, _prose && mode == Mode::text);
  if (_depth >= kMaxDepth) {
    // Too deep to recurse: skip to where this list would have ended.
    _diags.error(at, "Input nested too deeply");
    int depth = 0;
    while (true) {
      ExpandedToken t = next();
      if (t.tok.kind == TokKind::end) {
        unread(std::move(t));
        break;
      }
      if (t.tok.isChar(Cat::beginGroup)) depth++;
      if (t.tok.isChar(Cat::endGroup)) {
        if (depth == 0) {
          unread(std::move(t));
          break;
        }
        depth--;
      }
    }
    return emptyList(at, mode);
  }
  _depth++;
  readItems(mode, stop, items);
  _depth--;
  Node n;
  n.kind = NodeKind::list;
  n.mode = mode;
  n.span = at;
  return _ast.add(std::move(n), items);
}

void Parser::readItems(Mode mode, const Stop& stop, std::vector<NodeId>& items) {
  _rowEnded = false;
  while (true) {
    const ExpandedToken& t = peek();
    if (atStop(t, stop)) break;
    const Token& k = t.tok;
    if (mode == Mode::math && (k.isChar(Cat::superscript) || k.isChar(Cat::subscript) ||
                               isOther(k, '\''))) {
      // Scripts belong to the item before them, as in the old parser.
      NodeId base = kNoNode;
      if (!items.empty()) {
        base = items.back();
        items.pop_back();
      }
      items.push_back(parseScripts(base, mode, k.span));
      continue;
    }
    if (!parseItem(mode, stop, items)) break;
    if (_rowEnded && stop.cellTop) break;
  }
}

bool Parser::parseItem(Mode mode, const Stop& stop, std::vector<NodeId>& items) {
  ExpandedToken t = next();
  const Token& k = t.tok;

  // A blank line in document-mode prose starts a paragraph. It is read
  // before the line-end rule below, which mixed mode uses instead.
  if (_prose && _opts.parBreaks && k.kind == TokKind::par) {
    items.push_back(lineBreak(k.span, mode, true));
    return true;
  }

  // A line end in mixed-mode prose breaks the line: the space that holds
  // it, or one TeX dropped before this token (after a control word).
  if (_prose && _opts.lineEndsBreak && k.lineEnds > 0 && k.kind != TokKind::end) {
    items.push_back(lineBreak(k.span, mode));
    if (k.kind == TokKind::space || k.kind == TokKind::par) return true;
  }

  switch (k.kind) {
    case TokKind::end:
      unread(std::move(t));
      return false;
    case TokKind::space:
    case TokKind::par:
      if (mode == Mode::text) {
        Node s;
        s.kind = NodeKind::space;
        s.mode = mode;
        s.span = k.span;
        // The whole run of whitespace, which the lexer split between this
        // token and the next one's lead: the engine counted its line ends.
        s.raw = t.lead + t.text + peek().lead;
        items.push_back(_ast.add(std::move(s), {}));
      }
      return true;
    case TokKind::controlWord:
    case TokKind::controlSymbol: {
      bool unused = false;
      const NodeId n = parseCommand(std::move(t), mode, stop, items, unused);
      if (n != kNoNode) items.push_back(n);
      return true;
    }
    case TokKind::character:
      break;
  }

  switch (k.cat) {
    case Cat::beginGroup:
      items.push_back(parseGroupAfterOpen(t, mode));
      return true;
    case Cat::endGroup:
      // Not where this list stops: a `}` with no `{`.
      _diags.warn(k.span, "extra } ignored");
      return true;
    case Cat::mathShift: {
      if (mode == Mode::math) return true;  // the old parser ignored `$` in math
      const bool display = _restricted == 0 && secondShift(t, peek());
      if (display) next();
      items.push_back(parseMath(t, display, "", stop.group));
      return true;
    }
    case Cat::alignTab:
      _diags.warn(k.span, "& outside an alignment is drawn as a character");
      items.push_back(character(t, mode));
      return true;
    case Cat::param:
      _diags.warn(k.span, "macro parameter # outside a definition is drawn as a character");
      items.push_back(character(t, mode));
      return true;
    case Cat::superscript:
    case Cat::subscript:
      // Only text reaches here: in math the list reads them as scripts.
      _diags.warn(k.span, t.text + " outside math is drawn as a character");
      items.push_back(character(t, mode));
      return true;
    case Cat::active: {
      Node s;
      s.kind = NodeKind::space;
      s.mode = mode;
      s.span = k.span;
      s.aux = 1;  // ~, a space that does not break
      s.raw = t.text;
      items.push_back(_ast.add(std::move(s), {}));
      return true;
    }
    default:
      if (mode == Mode::text && !_monospace) {
        const NodeId lig = ligature(t, mode);
        if (lig != kNoNode) {
          items.push_back(lig);
          return true;
        }
      }
      items.push_back(character(t, mode));
      return true;
  }
}

/** TeX's text ligatures: `--` and `---` are the dashes, `` and '' the
 *  double quotes, and a single ` and ' the single ones, which is how TeX's
 *  text fonts set them. Only in text, and only where the characters are
 *  written next to each other; in math `''` is a double prime, and a lone
 *  `-` is a hyphen as it always was. Returns kNoNode when this is not one,
 *  having read nothing. */
NodeId Parser::ligature(const ExpandedToken& first, Mode mode) {
  const char c = isOther(first.tok, '-')    ? '-'
                 : isOther(first.tok, '`')  ? '`'
                 : isOther(first.tok, '\'') ? '\''
                                            : '\0';
  if (c == '\0') return kNoNode;
  const int most = c == '-' ? 3 : 2;
  std::vector<ExpandedToken> taken;
  while (static_cast<int>(taken.size()) + 1 < most) {
    ExpandedToken u = next();
    if (u.lead.empty() && isOther(u.tok, c)) {
      taken.push_back(std::move(u));
      continue;
    }
    unread(std::move(u));
    break;
  }
  const std::size_t n = taken.size() + 1;
  c32 cp = 0;
  if (c == '-' && n == 2) cp = 0x2013;        // en dash
  if (c == '-' && n == 3) cp = 0x2014;        // em dash
  if (c == '`') cp = n == 2 ? 0x201C : 0x2018;   // opening quotes
  if (c == '\'') cp = n == 2 ? 0x201D : 0x2019;  // closing quotes, apostrophe
  if (cp == 0) {
    // Not a ligature after all: put back what was read beyond the first.
    for (auto it = taken.rbegin(); it != taken.rend(); ++it) unread(std::move(*it));
    return kNoNode;
  }
  Node n2;
  n2.kind = NodeKind::character;
  n2.mode = mode;
  n2.span = first.tok.span;
  n2.cp = cp;
  appendToUtf8(n2.text, cp);
  return _ast.add(std::move(n2), {});
}

NodeId Parser::parseGroupAfterOpen(const ExpandedToken& open, Mode mode) {
  Stop inner;
  inner.group = true;
  const NodeId list = parseList(mode, inner, open.tok.span);
  ExpandedToken close = next();
  if (!close.tok.isChar(Cat::endGroup)) {
    _diags.warn(open.tok.span, "missing } inserted");
    unread(std::move(close));
  }
  Node g;
  g.kind = NodeKind::group;
  g.mode = mode;
  g.span = open.tok.span;
  g.aux = open.tok.environment;  // a prelude environment's expansion
  return _ast.add(std::move(g), {list});
}

// --- scripts -----------------------------------------------------------------

NodeId Parser::parseScripts(NodeId base, Mode mode, SourceSpan at) {
  NodeId sub = kNoNode, sup = kNoNode;
  int primes = 0;
  std::string order;
  while (true) {
    ExpandedToken t = next();
    const Token& k = t.tok;
    if (isOther(k, '\'')) {
      if (sup != kNoNode) {
        unread(std::move(t));
        break;
      }
      // Primes are a superscript: a second run of them, after a subscript
      // (f'_1'), is TeX's double superscript.
      if (primes > 0 && order.back() == '_') {
        _diags.warn(k.span, "double superscript: read as {...}^");
        unread(std::move(t));
        break;
      }
      // Only `'` is a prime, as in TeX: ` and " are characters.
      primes++;
      order += '\'';
      continue;
    }
    if (k.isChar(Cat::superscript)) {
      // Only a `^` right after the primes joins them (f'^2); after a
      // subscript between (f'_1^2) it is a second superscript.
      if (sup != kNoNode || (primes > 0 && order.back() == '_')) {
        _diags.warn(k.span, "double superscript: read as {...}^");
        unread(std::move(t));
        break;
      }
      sup = parseScriptArgument(mode, k.span);
      order += '^';
      continue;
    }
    if (k.isChar(Cat::subscript)) {
      if (sub != kNoNode) {
        _diags.warn(k.span, "double subscript: read as {...}_");
        unread(std::move(t));
        break;
      }
      sub = parseScriptArgument(mode, k.span);
      order += '_';
      continue;
    }
    unread(std::move(t));
    break;
  }
  Node n;
  n.kind = NodeKind::scripts;
  n.mode = mode;
  n.span = at;
  n.aux = static_cast<std::uint16_t>(primes);
  n.flag = sub != kNoNode;
  n.star = sup != kNoNode;
  // The order the parts came in: the old parser built `f_1'` differently
  // from `f'_1`, and the lowering follows it.
  n.text = order;
  // Each absent part gets its own empty list: a node has one parent.
  if (base == kNoNode) base = emptyList(at, mode);
  if (sub == kNoNode) sub = emptyList(at, mode);
  if (sup == kNoNode) sup = emptyList(at, mode);
  return _ast.add(std::move(n), {base, sub, sup});
}

NodeId Parser::parseScriptArgument(Mode mode, SourceSpan at) {
  ExpandedToken t = nextNonSpace();
  const Token& k = t.tok;
  if (k.kind == TokKind::end || k.isChar(Cat::endGroup) || k.isChar(Cat::alignTab) ||
      k.isChar(Cat::superscript) || k.isChar(Cat::subscript)) {
    _diags.warn(at, "missing script: an empty one is used");
    unread(std::move(t));
    return emptyList(at, mode);
  }
  std::vector<NodeId> items;
  if (k.isChar(Cat::beginGroup)) {
    items.push_back(parseGroupAfterOpen(t, mode));
  } else if (k.isControl()) {
    // A command with its own arguments, as the old parser read `x^\frac12`.
    bool unused = false;
    Stop single;
    single.argument = true;
    const NodeId n = parseCommand(std::move(t), mode, single, items, unused);
    if (n != kNoNode) items.push_back(n);
  } else {
    items.push_back(character(t, mode));
  }
  Node l;
  l.kind = NodeKind::list;
  l.mode = mode;
  l.span = at;
  return _ast.add(std::move(l), items);
}

// --- commands ----------------------------------------------------------------

NodeId Parser::parseCommand(ExpandedToken t, Mode mode, const Stop& stop,
                            std::vector<NodeId>& items, bool& consumedRest) {
  const std::string name = t.tok.text;
  const SourceSpan at = t.tok.span;
  const std::string who = "\\" + name;
  const CommandSpec* spec = findCommand(name);

  Node n;
  n.kind = NodeKind::command;
  n.mode = mode;
  n.span = at;
  n.text = name;

  if (spec == nullptr) {
    if (!(_opts.isKnownName && _opts.isKnownName(name))) {
      _diags.warn(at, "unknown command " + who + ": drawn as its name");
      n.flag = true;
    }
    return _ast.add(std::move(n), {});
  }

  if (spec->special) {
    if (name == "left") return parseLeftRight(t, mode);
    if (name == "begin") return parseEnvironment(t, mode);
    if (name == "(") return parseMath(t, false, ")", stop.group);
    if (name == "ensuremath" && mode == Mode::math) {
      // In math it is not there, as in LaTeX: its argument is read on as
      // part of this list, so `90\degree` is 90^\circ.
      // An argument is still being read while replayed tokens stand above
      // its mark (a token peeked from the input may sit on top).
      std::size_t pending = _ahead.size();
      while (pending > 0 && !_ahead[pending - 1].replay) pending--;
      while (!_ensureMarks.empty() && _ensureMarks.back() >= pending) _ensureMarks.pop_back();
      if (static_cast<int>(_ensureMarks.size()) >= kMaxDepth) {
        // Each level reads what is inside it again: nesting costs its square.
        _diags.error(at, "Input nested too deeply");
        return kNoNode;
      }
      ExpandedToken open = nextNonSpace();
      if (!open.tok.isChar(Cat::beginGroup)) {
        unread(std::move(open));
        return kNoNode;
      }
      const std::size_t origin = _consumed;
      std::vector<ExpandedToken> inner;
      int depth = 0;
      while (true) {
        ExpandedToken u = next();
        if (u.tok.kind == TokKind::end) {
          _diags.warn(open.tok.span, "missing } inserted");
          unread(std::move(u));
          break;
        }
        if (u.tok.isChar(Cat::beginGroup)) depth++;
        if (u.tok.isChar(Cat::endGroup) && depth-- == 0) break;
        inner.push_back(std::move(u));
      }
      _ensureMarks.push_back(_ahead.size());
      for (auto it = inner.rbegin(); it != inner.rend(); ++it) _ahead.push_back({std::move(*it), true, origin});
      return kNoNode;
    }
    if (name == "ensuremath") {
      // In text, a formula: what `$...$` would be, but safe inside one.
      const NodeId arg = parseArgument(spec->args[0], mode, who);
      const NodeId list = _ast.childCount(arg) > 0 ? _ast.child(arg, 0) : emptyList(at, Mode::math);
      Node m;
      m.kind = NodeKind::math;
      m.mode = Mode::math;
      m.span = at;
      return _ast.add(std::move(m), {list});
    }
    if (name == "[") return parseMath(t, true, "]", stop.group);
    if (name == "right") {
      _diags.warn(at, "\\right without \\left: drawn as its name");
      n.flag = true;
      return _ast.add(std::move(n), {});
    }
    if (name == "end") {
      const std::string env = readGroupName();
      _diags.warn(at, "\\end{" + env + "} without \\begin ignored");
      return kNoNode;
    }
    if (name == "middle") {
      return _ast.add(std::move(n), {parseDelimiter(who)});
    }
    if (isHeading(name)) {
      // `\section*` is the same heading without its number. The star is a
      // character to the lexer, so it is read here, where the spec says
      // which names have a starred form; LaTeX looks for it past spaces.
      if (isOther(peek().tok, '*')) {
        next();
        n.star = true;
      }
      // The short title for a table of contents, which a grob has not got,
      // then the title, set as text whatever mode it was met in.
      const NodeId shortTitle = parseArgument(spec->args[0], mode, who);
      return _ast.add(std::move(n), {shortTitle, parseArgument(spec->args[1], Mode::text, who)});
    }
    if (name == "noindent" || isLineAlignment(name)) return _ast.add(std::move(n), {});
    if (name == "caption") {
      // A line of text, ended as the prelude used to end it, with a line
      // break. A document also starts it on a line of its own (lower.cpp).
      // Its star (no number, and a table has none to give) is read and
      // means nothing here.
      if (isOther(peek().tok, '*')) next();
      std::vector<NodeId> args;
      for (const ArgSpec& a : spec->args) args.push_back(parseArgument(a, Mode::text, who));
      items.push_back(_ast.add(std::move(n), args));
      return lineBreak(at, mode);
    }
    if (name == "ref" || name == "pageref" || name == "eqref" || isCitation(name) ||
        name == "footnote") {
      std::vector<NodeId> args;
      for (const ArgSpec& a : spec->args) args.push_back(parseArgument(a, mode, who));
      const Node& last = _ast.node(args.back());
      const std::string key = trim(last.raw);
      if (!last.flag) {
        // No argument at all: parseArgument() has said so.
      } else if (name == "footnote") {
        _diags.warn(at, "\\footnote has no page to put a note on: its text is drawn here");
      } else if (isCitation(name)) {
        // What natbib draws for its own: \citet names an author it does
        // not have, \citealp drops the brackets.
        const std::string drawn = name == "citet" ? "(author?) [?]" : name == "citealp" ? "?" : "[?]";
        _diags.warn(at, "citation `" + key + "' is undefined: drawn as " + drawn);
      } else if (name == "pageref") {
        _diags.warn(at, "reference `" + key + "' is undefined: drawn as ??");
      }
      // \ref and \eqref name a label, which may come after them: the
      // lowering, which sees them all, says which are undefined.
      return _ast.add(std::move(n), args);
    }
    if (name == "par") {
      // TeX's own paragraph break. Where there are no paragraphs (a label,
      // a formula) it is the line break the prelude used to define.
      return lineBreak(at, mode, _opts.parBreaks);
    }
    if (name == "\\" || name == "cr") {
      // A line break outside an alignment (in one, it ends the row and is
      // read by the environment). `\\[4pt]`: only a bracket right after it
      // is the gap, as in amsmath, so `\\ [0,1]` stays text.
      std::vector<NodeId> args;
      if (name == "\\") {
        const ExpandedToken& p = peek();
        if (isOther(p.tok, '[') && p.lead.empty()) {
          args.push_back(parseArgument({ArgKind::dimen, true}, mode, who));
        } else {
          args.push_back(absentArgument(at, mode));
        }
      }
      return _ast.add(std::move(n), args);
    }
    if (name == "cmidrule") {
      // booktabs' \cmidrule[width](trim){a-b}: the rule of \cline{a-b};
      // its width and trims have nothing to act on here.
      if (isOther(peekNonSpace().tok, '[')) {
        nextNonSpace();
        collectBracketed(who);
      }
      if (isOther(peekNonSpace().tok, '(')) {
        nextNonSpace();
        while (true) {
          ExpandedToken u = next();
          if (u.tok.kind == TokKind::end) {
            unread(std::move(u));
            break;
          }
          if (isOther(u.tok, ')')) break;
        }
      }
      n.text = "cline";
    }
    if (isRule(n.text) || name == "intertext") {
      std::vector<NodeId> args;
      for (const ArgSpec& a : spec->args) args.push_back(parseArgument(a, mode, who));
      if (stop.cellTop) {
        _rowEnded = true;
      } else {
        _diags.warn(at, who + " outside an alignment is ignored");
      }
      return _ast.add(std::move(n), args);
    }
  }

  // Inside a one-token argument (`\frac\bf ab`), a command is just itself.
  const bool single = stop.argument;

  if (spec->shape == Shape::postfix && !single) {
    // \limits: modifies the item before it, as the old parser's popBack did.
    NodeId base = kNoNode;
    if (!items.empty()) {
      base = items.back();
      items.pop_back();
    }
    std::vector<NodeId> kids;
    kids.push_back(base == kNoNode ? emptyList(at, mode) : base);
    return _ast.add(std::move(n), kids);
  }

  std::vector<NodeId> args;
  {
    // A typewriter font has no ligatures in TeX, so `--v` in \texttt is two
    // hyphens, not a dash. It is the font that decides there; naming the
    // commands is the closest the parser can come to it. (\tt is a
    // declaration: its body is read below.)
    const Scoped verbatim(_monospace, _monospace || name == "texttt" || name == "mathtt");
    for (const ArgSpec& a : spec->args) args.push_back(parseArgument(a, mode, who));
    if (spec->bare != Bare::none) args.push_back(parseBare(spec->bare, who));
  }

  if (spec->shape == Shape::infix && !single) {
    // The numerator is everything before it in this list; the denominator
    // everything after, up to where the list ends or a `\\` or \cr at this
    // level (a line end in a label's prose), which the list goes on past,
    // as the old parser's getOverArgument() stopped there.
    Node num;
    num.kind = NodeKind::list;
    num.mode = mode;
    num.span = at;
    // The row it is in, that is: a `\\` or \cr before it ends a row, which
    // stays in the list with the rows before it.
    auto from = items.end();
    while (from != items.begin()) {
      const Node& x = _ast.node(*(from - 1));
      if (x.kind == NodeKind::command && (x.text == "\\" || x.text == "cr")) break;
      --from;
    }
    const NodeId numerator = _ast.add(std::move(num), std::vector<NodeId>(from, items.end()));
    items.erase(from, items.end());
    Stop rest = stop;
    rest.cellTop = false;
    rest.overArg = true;
    rest.lineEnd = _prose && _opts.lineEndsBreak;
    // The engine reads the denominator in math mode, even in text.
    const NodeId denominator = parseList(Mode::math, rest, at);
    n.kind = NodeKind::infix;
    std::vector<NodeId> kids{numerator, denominator};
    kids.insert(kids.end(), args.begin(), args.end());
    consumedRest = true;
    return _ast.add(std::move(n), kids);
  }

  const bool declaration = spec->shape == Shape::declaration ||
                           (spec->shape == Shape::groupDeclaration && !stop.cellTop);
  if (declaration && !single) {
    // \color in a cell itself colours the cell, and has no body.
    Stop body = stop;
    body.cellTop = false;
    body.argument = false;
    body.overArg = spec->shape == Shape::declaration;
    const Mode bodyMode = spec->body == ArgKind::math ? Mode::math : mode;
    const Scoped verbatim(_monospace, _monospace || name == "tt");
    const NodeId list = parseList(bodyMode, body, at);
    n.kind = NodeKind::declaration;
    args.push_back(list);
    return _ast.add(std::move(n), args);
  }

  return _ast.add(std::move(n), args);
}

NodeId Parser::parseArgument(const ArgSpec& spec, Mode mode, const std::string& who) {
  if (spec.kind == ArgKind::raw || spec.kind == ArgKind::dimen || spec.kind == ArgKind::url ||
      spec.kind == ArgKind::delim) {
    return parseRawArgument(spec, who);
  }
  const Mode argMode = spec.kind == ArgKind::math   ? Mode::math
                       : spec.kind == ArgKind::text ? Mode::text
                                                    : mode;
  Node arg;
  arg.kind = NodeKind::argument;
  arg.mode = argMode;
  arg.flag = true;
  arg.text = std::string(1, kindCode(spec.kind));
  const bool restricted = argMode == Mode::text;
  if (restricted) _restricted++;
  struct Unrestrict {
    int& n;
    bool on;
    ~Unrestrict() {
      if (on) n--;
    }
  } unrestrict{_restricted, restricted};

  if (spec.optional) {
    ExpandedToken t = nextNonSpace();
    if (!isOther(t.tok, '[')) {
      const SourceSpan at = t.tok.span;
      unread(std::move(t));
      return absentArgument(at, argMode);
    }
    arg.span = t.tok.span;
    const std::size_t origin = _consumed;
    std::vector<ExpandedToken> toks = collectBracketed(who);
    for (const ExpandedToken& u : toks) {
      arg.raw += u.lead;
      arg.raw += u.text;
    }
    const NodeId list = parseTokens(std::move(toks), argMode, arg.span, origin);
    return _ast.add(std::move(arg), {list});
  }

  ExpandedToken t = nextNonSpace();
  const Token& k = t.tok;
  arg.span = k.span;
  if (k.kind == TokKind::end || k.kind == TokKind::par || k.isChar(Cat::endGroup)) {
    _diags.warn(k.span, "missing argument for " + who + ": an empty one is used");
    unread(std::move(t));
    return absentArgument(k.span, argMode);
  }
  if (k.isChar(Cat::beginGroup)) {
    const std::size_t mark = startRecording();
    Stop inner;
    inner.group = true;
    const NodeId list = parseList(argMode, inner, k.span);
    ExpandedToken close = next();
    const bool closed = close.tok.isChar(Cat::endGroup);
    arg.raw = stopRecording(mark, closed ? 1 : 0);
    if (!closed) {
      _diags.warn(k.span, "missing } inserted");
      unread(std::move(close));
    }
    return _ast.add(std::move(arg), {list});
  }
  // One token: a character, or a command with its own arguments.
  unread(std::move(t));
  const std::size_t mark = startRecording();
  std::vector<NodeId> items;
  Stop single;
  single.argument = true;
  ExpandedToken u = next();
  if (u.tok.isControl()) {
    // A command's arguments nest like a group's: `\frac\frac\frac...`.
    if (_depth >= kMaxDepth) {
      _diags.error(u.tok.span, "Input nested too deeply");
    } else {
      _depth++;
      bool unused = false;
      const NodeId n = parseCommand(std::move(u), argMode, single, items, unused);
      _depth--;
      if (n != kNoNode) items.push_back(n);
    }
  } else if (u.tok.isChar(Cat::active)) {
    unread(std::move(u));
    parseItem(argMode, single, items);
  } else {
    items.push_back(character(u, argMode));
  }
  arg.raw = stopRecording(mark, 0, false);
  Node l;
  l.kind = NodeKind::list;
  l.mode = argMode;
  l.span = arg.span;
  const NodeId list = _ast.add(std::move(l), items);
  return _ast.add(std::move(arg), {list});
}

NodeId Parser::parseRawArgument(const ArgSpec& spec, const std::string& who) {
  Node arg;
  arg.kind = NodeKind::argument;
  arg.mode = Mode::text;
  arg.flag = true;
  arg.text = std::string(1, kindCode(spec.kind));

  if (spec.optional) {
    ExpandedToken t = nextNonSpace();
    if (!isOther(t.tok, '[')) {
      const SourceSpan at = t.tok.span;
      unread(std::move(t));
      return absentArgument(at, Mode::text);
    }
    arg.span = t.tok.span;
    for (const ExpandedToken& u : collectBracketed(who)) {
      arg.raw += u.lead;
      arg.raw += u.text;
    }
    return _ast.add(std::move(arg), {});
  }

  ExpandedToken t = nextNonSpace();
  const Token& k = t.tok;
  arg.span = k.span;
  if (k.kind == TokKind::end || k.kind == TokKind::par || k.isChar(Cat::endGroup)) {
    _diags.warn(k.span, "missing argument for " + who + ": an empty one is used");
    unread(std::move(t));
    return absentArgument(k.span, Mode::text);
  }
  if (!k.isChar(Cat::beginGroup)) {
    arg.raw = t.text;
    return _ast.add(std::move(arg), {});
  }
  // A URL or file name reads its special characters as characters, a
  // backslash included, so nothing in it is a command to expand. The `{` is
  // read already; nothing after it has been lexed yet.
  struct Saved {
    c32 ch;
    Cat cat;
  };
  std::vector<Saved> saved;
  if (spec.kind == ArgKind::url) {
    for (const char c : std::string("\\%#_^~&$")) {
      saved.push_back({static_cast<c32>(c), _in.catcode(static_cast<c32>(c))});
      _in.setCatcode(static_cast<c32>(c), Cat::other);
    }
  }
  const std::size_t mark = startRecording();
  int depth = 0;
  bool closed = false;
  // In a URL, a backslash right before a brace makes it a character (a
  // file name holding one, as markdown writes it); the host unescapes it.
  bool escaped = false;
  while (true) {
    ExpandedToken u = next();
    if (u.tok.kind == TokKind::end) {
      unread(std::move(u));
      break;
    }
    const bool brace = u.tok.isChar(Cat::beginGroup) || u.tok.isChar(Cat::endGroup);
    if (!(escaped && brace && u.lead.empty())) {
      if (u.tok.isChar(Cat::beginGroup)) depth++;
      if (u.tok.isChar(Cat::endGroup)) {
        if (depth == 0) {
          closed = true;
          break;
        }
        depth--;
      }
    }
    escaped = spec.kind == ArgKind::url && isOther(u.tok, '\\') && !escaped;
  }
  arg.raw = stopRecording(mark, closed ? 1 : 0);
  for (const Saved& s : saved) _in.setCatcode(s.ch, s.cat);
  if (!closed) _diags.warn(k.span, "missing } inserted");
  return _ast.add(std::move(arg), {});
}

std::vector<ExpandedToken> Parser::collectBracketed(const std::string& who) {
  std::vector<ExpandedToken> toks;
  int brackets = 0, braces = 0;
  while (true) {
    ExpandedToken t = next();
    const Token& k = t.tok;
    if (k.kind == TokKind::end) {
      _diags.warn(k.span, "missing ] inserted in the optional argument of " + who);
      unread(std::move(t));
      break;
    }
    if (k.isChar(Cat::beginGroup)) braces++;
    if (k.isChar(Cat::endGroup)) {
      if (braces == 0) {
        _diags.warn(k.span, "missing ] inserted in the optional argument of " + who);
        unread(std::move(t));
        break;
      }
      braces--;
    }
    if (braces == 0 && isOther(k, '[')) brackets++;
    if (braces == 0 && isOther(k, ']')) {
      if (brackets == 0) break;
      brackets--;
    }
    toks.push_back(std::move(t));
  }
  return toks;
}

NodeId Parser::parseTokens(std::vector<ExpandedToken> tokens, Mode mode, SourceSpan at,
                           std::size_t origin) {
  // Read the tokens again, then an `end` that stops the list, then carry on
  // with whatever was waiting before.
  std::vector<Pending> saved = std::move(_ahead);
  _ahead.clear();
  ExpandedToken sentinel;
  sentinel.tok.kind = TokKind::end;
  sentinel.tok.span = at;
  _ahead.push_back({std::move(sentinel), true, origin});
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) _ahead.push_back({std::move(*it), true, origin});
  const NodeId list = parseList(mode, Stop{}, at);
  next();  // the sentinel
  _ahead = std::move(saved);
  return list;
}

NodeId Parser::parseBare(Bare bare, const std::string& who) {
  Node arg;
  arg.kind = NodeKind::argument;
  arg.mode = Mode::text;
  arg.flag = true;
  arg.text = bare == Bare::dimen ? "d" : "n";
  ExpandedToken t = nextNonSpace();
  arg.span = t.tok.span;
  if (t.tok.isChar(Cat::beginGroup)) {
    // `\kern{3pt}`, which TeX would not take, but the engine's own
    // definitions write.
    unread(std::move(t));
    const NodeId raw = parseRawArgument({ArgKind::dimen, false}, who);
    arg.raw = _ast.node(raw).raw;
    return _ast.add(std::move(arg), {});
  }
  // As the old parser read it: characters up to a space or a command.
  while (t.tok.kind == TokKind::character && !t.tok.isChar(Cat::beginGroup) &&
         !t.tok.isChar(Cat::endGroup) && !t.tok.isChar(Cat::mathShift) &&
         !t.tok.isChar(Cat::alignTab)) {
    if (bare == Bare::number) {
      const c32 c = t.tok.cp;
      const bool ok = c == '\'' || c == '"' || (c >= '0' && c <= '9') ||
                      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      if (!ok) break;
    }
    arg.raw += t.text;
    t = next();
    if (!t.lead.empty()) break;
  }
  // TeX takes one optional space after a number or a dimension as part of
  // it (`\kern3pt x`) -- but a line end in a label still breaks the line,
  // as one after a control word does.
  const bool optionalSpace = !arg.raw.empty() && t.tok.kind == TokKind::space &&
                             !(_prose && _opts.lineEndsBreak && t.tok.lineEnds > 0);
  if (!optionalSpace) unread(std::move(t));
  if (arg.raw.empty()) _diags.warn(arg.span, "missing value after " + who);
  return _ast.add(std::move(arg), {});
}

// --- \left \right ------------------------------------------------------------

NodeId Parser::parseDelimiter(const std::string& who) {
  return parseRawArgument({ArgKind::delim, false}, who);
}

NodeId Parser::parseLeftRight(const ExpandedToken& left, Mode mode) {
  std::vector<NodeId> kids;
  kids.push_back(parseDelimiter("\\left"));
  Stop inner;
  inner.right = true;
  while (true) {
    // Math even in text: \left exists only in math, and the old parser
    // read what it encloses as math wherever it met it.
    kids.push_back(parseList(Mode::math, inner, left.tok.span));
    ExpandedToken t = next();
    if (t.tok.isCs("middle")) {
      kids.push_back(parseDelimiter("\\middle"));
      continue;
    }
    if (t.tok.isCs("right")) {
      kids.push_back(parseDelimiter("\\right"));
      break;
    }
    _diags.warn(left.tok.span, "missing \\right. inserted");
    unread(std::move(t));
    kids.push_back(absentArgument(left.tok.span, Mode::text));
    break;
  }
  Node n;
  n.kind = NodeKind::leftRight;
  n.mode = mode;
  n.span = left.tok.span;
  return _ast.add(std::move(n), kids);
}

// --- environments ------------------------------------------------------------

std::string Parser::readGroupName() {
  ExpandedToken t = nextNonSpace();
  if (!t.tok.isChar(Cat::beginGroup)) {
    unread(std::move(t));
    return "";
  }
  // Built from the tokens, not from the recorded source.
  std::string name;
  int depth = 0;
  while (true) {
    ExpandedToken u = next();
    if (u.tok.kind == TokKind::end) {
      _diags.warn(t.tok.span, "missing } inserted");
      unread(std::move(u));
      break;
    }
    if (u.tok.isChar(Cat::beginGroup)) depth++;
    if (u.tok.isChar(Cat::endGroup) && depth-- == 0) break;
    name += u.lead;
    name += u.text;
  }
  return trim(name);
}

NodeId Parser::parseTextBody(const std::string& name, SourceSpan at) {
  Stop body;
  body.end = true;
  const NodeId list = parseList(Mode::text, body, at);
  ExpandedToken t = next();
  if (t.tok.isCs("end")) {
    const std::string closing = readGroupName();
    if (closing != name) {
      _diags.warn(t.tok.span, "\\end{" + closing + "} ends \\begin{" + name + "}");
    }
  } else {
    _diags.warn(at, "missing \\end{" + name + "} inserted");
    unread(std::move(t));
  }
  return list;
}

NodeId Parser::parseEnvironment(const ExpandedToken& begin, Mode mode) {
  const SourceSpan at = begin.tok.span;
  // An environment's line ends are its own business, as in TeX.
  const Scoped prose(_prose, false);
  const std::string name = readGroupName();
  const EnvSpec* spec = findEnvironment(name);
  Node n;
  n.kind = NodeKind::environment;
  n.mode = mode;
  n.span = at;
  n.text = name;
  if (spec == nullptr && name.size() > 1 && name.back() == '*') {
    // A starred environment is its plain form; amsmath's star turns off
    // its numbering, which the lowering reads from the node.
    spec = findEnvironment(name.substr(0, name.size() - 1));
    if (spec != nullptr) {
      n.text = name.substr(0, name.size() - 1);
      n.star = true;
    }
  }
  if (spec == nullptr && mode == Mode::text) {
    // LaTeX's own recovery: after "Environment ... undefined" the body is
    // set as ordinary text, in a group -- a paragraph of an abstract is a
    // paragraph, with its spaces, not a row of math.
    _diags.warn(at, "unknown environment " + name + ": its body is set as text");
    // Text as around it, line ends included.
    const Scoped text(_prose, prose.was);
    const NodeId list = parseTextBody(name, at);
    // Transparent, as document's expansion is: its content is set as if
    // the environment were not there.
    Node g;
    g.kind = NodeKind::group;
    g.mode = Mode::text;
    g.span = at;
    g.aux = 2;
    return _ast.add(std::move(g), {list});
  }
  if (spec == nullptr) {
    _diags.warn(at, "unknown environment " + name + ": read as an array");
  }
  // A table or list met in text has text cells or items, as in LaTeX.
  n.flag = spec != nullptr && spec->textInText && mode == Mode::text;
  const Mode cellMode = n.flag ? Mode::text : Mode::math;
  std::vector<NodeId> kids;
  if (spec != nullptr) {
    for (const ArgSpec& a : spec->args) kids.push_back(parseArgument(a, mode, "\\begin{" + name + "}"));
  }
  if (spec != nullptr && spec->body == EnvBody::text) {
    // Paragraphs, in text whatever the mode around it: a blank line in a
    // minipage in a document is a paragraph, as it is outside one.
    const Scoped text(_prose, prose.was);
    kids.push_back(parseTextBody(name, at));
    return _ast.add(std::move(n), kids);
  }

  const std::size_t mark = startRecording();
  if (spec != nullptr && spec->body == EnvBody::raw) {
    // Kept as text up to the \end that closes it. Every environment begun
    // in it is counted, so the first \end with none open is that one,
    // whatever its name: LaTeX's "\begin{itemize} ended by \end{enumerate}"
    // ends the list, where looking for \end{itemize} read the rest of the
    // input into its last item.
    int depth = 0;
    while (true) {
      ExpandedToken t = next();
      if (t.tok.kind == TokKind::end) {
        unread(std::move(t));
        n.raw = stopRecording(mark, 0);
        _diags.warn(at, "missing \\end{" + name + "} inserted");
        return _ast.add(std::move(n), kids);
      }
      if (!t.tok.isCs("begin") && !t.tok.isCs("end")) continue;
      const bool open = t.tok.isCs("begin");
      std::size_t used = 1;
      std::string inner;
      ExpandedToken brace = next();
      used++;
      if (!brace.tok.isChar(Cat::beginGroup)) {
        unread(std::move(brace));
        continue;
      }
      while (true) {
        ExpandedToken u = next();
        if (u.tok.kind == TokKind::end) {
          unread(std::move(u));
          break;
        }
        used++;
        if (u.tok.isChar(Cat::endGroup)) break;
        inner += u.text;
      }
      if (open) {
        depth++;
        continue;
      }
      if (depth-- > 0) continue;
      const std::string closing = trim(inner);
      if (closing != name) {
        _diags.warn(t.tok.span, "\\end{" + closing + "} ends \\begin{" + name + "}");
      }
      // The body is everything before this \end.
      n.raw = stopRecording(mark, used);
      return _ast.add(std::move(n), kids);
    }
  }

  // An alignment: rows at `\\` and `\cr`, cells at `&`; a rule or
  // \intertext ends its row, as it did in the old parser.
  while (true) {
    std::vector<NodeId> cells;
    Node row;
    row.kind = NodeKind::row;
    row.mode = cellMode;
    bool firstCell = true;
    while (true) {
      Stop cellStop;
      cellStop.cell = true;
      cellStop.cellTop = true;
      const SourceSpan cellAt = peek().tok.span;
      if (firstCell) row.span = cellAt;
      firstCell = false;
      const NodeId list = parseList(cellMode, cellStop, cellAt);
      Node cell;
      cell.kind = NodeKind::cell;
      cell.mode = cellMode;
      cell.span = cellAt;
      cells.push_back(_ast.add(std::move(cell), {list}));
      if (_rowEnded) {
        // The rule is the last item of the cell just read.
        const NodeId items = list;
        const std::uint32_t count = _ast.childCount(items);
        row.text = count > 0 ? _ast.node(_ast.child(items, count - 1)).text : "";
        _rowEnded = false;
        break;
      }
      const ExpandedToken& p = peek();
      if (p.tok.isChar(Cat::alignTab)) {
        next();
        continue;
      }
      break;
    }
    if (!row.text.empty()) {
      kids.push_back(_ast.add(std::move(row), cells));
      continue;
    }
    ExpandedToken t = next();
    if (t.tok.isCs("\\")) {
      row.text = "\\";
      const ExpandedToken& p = peek();
      if (isOther(p.tok, '[') && p.lead.empty()) {
        const NodeId gap = parseArgument({ArgKind::dimen, true}, Mode::math, "\\\\");
        row.raw = _ast.node(gap).raw;
      }
      kids.push_back(_ast.add(std::move(row), cells));
      continue;
    }
    if (t.tok.isCs("cr")) {
      row.text = "cr";
      kids.push_back(_ast.add(std::move(row), cells));
      continue;
    }
    // \end, or the end of the input: the last row.
    kids.push_back(_ast.add(std::move(row), cells));
    if (t.tok.isCs("end")) {
      n.raw = stopRecording(mark, 1);
      const std::string closing = readGroupName();
      if (closing != name) {
        _diags.warn(t.tok.span, "\\end{" + closing + "} ends \\begin{" + name + "}");
      }
    } else {
      n.raw = stopRecording(mark, 0);
      _diags.warn(at, "missing \\end{" + name + "} inserted");
      unread(std::move(t));
    }
    break;
  }
  return _ast.add(std::move(n), kids);
}

// --- math in text --------------------------------------------------------------

NodeId Parser::parseMath(const ExpandedToken& open, bool display, const std::string& closeSymbol,
                         bool inGroup) {
  Stop inner;
  inner.group = inGroup;
  if (closeSymbol.empty()) {
    inner.dollar = true;
  } else {
    inner.closeSymbol = closeSymbol;
  }
  const NodeId list = parseList(Mode::math, inner, open.tok.span);
  ExpandedToken close = next();
  bool closed = false;
  if (closeSymbol.empty()) {
    if (close.tok.isChar(Cat::mathShift)) {
      closed = true;
      if (display && secondShift(close, peek())) next();
    }
  } else {
    closed = close.tok.kind == TokKind::controlSymbol && close.tok.text == closeSymbol;
  }
  if (!closed) {
    _diags.warn(open.tok.span, closeSymbol.empty() ? "missing $ inserted"
                                                   : "missing \\" + closeSymbol + " inserted");
    unread(std::move(close));
  }
  Node m;
  m.kind = NodeKind::math;
  m.mode = Mode::math;
  m.flag = display;
  m.span = open.tok.span;
  return _ast.add(std::move(m), {list});
}

}  // namespace microtex::front
