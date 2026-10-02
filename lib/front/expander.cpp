#include "front/expander.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "front/prelude.h"
#include "front/mhchem.h"
#include "front/siunitx.h"
#include "front/spec.h"
#include "utils/exceptions.h"

namespace microtex::front {

namespace {

/** A token with the exact source text it came from, so that output made
 *  of unexpanded tokens is the input byte for byte. */
struct ExpToken {
  Token tok;
  /** Whitespace and comments before the token in its own source. */
  std::string lead;
  /** The token's own source text. Points into a buffer the expander keeps
   *  alive until it is destroyed. */
  std::string_view text;
};

/** One source being read: the input itself, or one expansion. */
struct Frame {
  std::string_view buf;
  std::unique_ptr<Lexer> lexer;
  bool root;
  /** For an expansion, where in the input the outermost macro was used:
   *  that is where a problem inside it is reported, because it is what
   *  the user can find. */
  SourceSpan origin;
  /** Tokens read from this source and put back. They belong to it, not to
   *  the reader: an expansion opened after a token was put back is read
   *  before it, as in TeX's input stack. */
  std::vector<ExpToken> pushback;
};

bool sameToken(const Token& a, const Token& b) {
  if (a.kind != b.kind) return false;
  if (a.kind == TokKind::character) return a.cat == b.cat && a.text == b.text;
  if (a.isControl()) return a.text == b.text;
  return true;
}

std::string trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

/** A definition command's error, worded as the old parser worded it (its
 *  command handlers wrapped every error this way). */
[[noreturn]] void fail(const std::string& command, const std::string& message) {
  throw ex_parse("Problem with command: " + command + "\n caused by: " + message);
}

/** Runaway expansion: nothing after it can be trusted. */
class ExpansionLimit : public ex_parse {
public:
  explicit ExpansionLimit(const std::string& msg) : ex_parse(msg) {}
};

/** An error's text for a diagnostic: without the dispatcher's wrapper. */
std::string cleanMessage(std::string msg) {
  const std::string mark = "caused by: ";
  const auto p = msg.rfind(mark);
  if (p != std::string::npos) msg = msg.substr(p + mark.size());
  for (char& c : msg) {
    if (c == '\n') c = ' ';
  }
  return msg;
}

std::vector<BodyPiece> literal(std::string text) {
  std::vector<BodyPiece> pieces;
  pieces.push_back({std::move(text), 0});
  return pieces;
}

/** Starred forms of built-ins, which the old parser cannot read: it takes
 *  the `*` for the first argument. */
const std::unordered_map<std::string, MacroDef>& starredBuiltins() {
  static const std::unordered_map<std::string, MacroDef> table = [] {
    std::unordered_map<std::string, MacroDef> t;
    const auto add = [&](const char* name, int nparams, std::vector<BodyPiece> body) {
      MacroDef def;
      def.nparams = nparams;
      def.body = std::move(body);
      t.emplace(name, std::move(def));
    };
    add("operatorname*", 1, {{"\\mathop{\\mathrm{", 0}, {"", 1}, {"}}\\limits", 0}});
    add("hspace*", 1, {{"\\hspace{", 0}, {"", 1}, {"}", 0}});
    add("vspace*", 1, {{"\\vspace{", 0}, {"", 1}, {"}", 0}});
    // amsmath's \tag*{x}: the tag without its parentheses.
    add("tag*", 1, {{"\\gmtagstar{", 0}, {"", 1}, {"}", 0}});
    add("\\*", 0, {{"\\\\", 0}});
    // It clips to the bounding box, and there is nothing outside it here.
    add("includegraphics*", 0, {{"\\includegraphics", 0}});
    return t;
  }();
  return table;
}

std::vector<std::pair<std::string, std::string>>& persistentTable() {
  static std::vector<std::pair<std::string, std::string>> table;
  return table;
}

std::uint64_t& generation() {
  static std::uint64_t value = 0;
  return value;
}

/** Macros and environments defined once and shared by every parse. */
struct Definitions {
  std::unordered_map<std::string, MacroDef> macros;
  std::unordered_map<std::string, MacroDef> envs;
  /** The macros a document may define itself (preludeSoftSource()). */
  std::unordered_set<std::string> soft;
};

}  // namespace

struct Expander::Impl {
  /** The prelude's definitions, made by expanding its LaTeX once. */
  static const Definitions& prelude() {
    static const Definitions defs = [] {
      Diagnostics ignored;
      ExpanderOptions o;
      o.persistent = false;
      Impl impl(std::string(preludeSource()), std::move(o), ignored);
      impl.run();
      Definitions defs{std::move(impl.macros), std::move(impl.envs), {}};
      ExpanderOptions soft;
      soft.persistent = false;
      Impl more(std::string(preludeSoftSource()), std::move(soft), ignored);
      more.run();
      for (auto& kv : more.macros) {
        defs.soft.insert(kv.first);
        defs.macros.insert(std::move(kv));
      }
      return defs;
    }();
    return defs;
  }

  ExpanderOptions opts;
  Diagnostics& diags;
  const Definitions* shared = nullptr;
  /** Re-lexing text that was already lexed once would repeat its warnings. */
  Diagnostics quiet;
  CatcodeTable cats;
  std::deque<std::string> buffers;
  std::vector<Frame> frames;
  std::string pendingLead;
  std::unordered_map<std::string, MacroDef> macros;
  std::unordered_map<std::string, MacroDef> envs;
  std::size_t expansions = 0;
  std::size_t expandedBytes = 0;
  /** How deep readArg() is in commands given as arguments with their own. */
  int argDepth = 0;
  int atLetter = 0;
  /** \theoremstyle's: plain, definition or remark, for the \newtheorems
   *  after it. */
  std::string theoremStyle = "plain";
  /** Units a document made with \DeclareSIUnit. */
  siunitx::UserUnits siUnits;
  /** The counter a theorem environment is numbered within (`section`), by
   *  environment name, for those that share its counter. */
  std::unordered_map<std::string, std::string> theoremWithin;
  // Set after runaway expansion in recover mode: the input ends there.
  bool halted = false;
  // Where the input ends for now (Expander::endInputAt()), and the first
  // token past that, read and held back until the end moves on.
  std::uint32_t limit = UINT32_MAX;
  bool holding = false;
  ExpToken held;
  // Set when a delimited macro's use does not fit its definition: the
  // call is dropped (see readDelimitedArgs()).
  bool abandoned = false;
  // Tokens read by delimited arguments that were then abandoned.
  std::size_t runawayTokens = 0;
  std::string out;
  bool lastControlWord = false;

  Impl(std::string input, ExpanderOptions options, Diagnostics& diagnostics)
      : opts(std::move(options)), diags(diagnostics) {
    buffers.push_back(std::move(input));
    frames.push_back({buffers.back(), std::make_unique<Lexer>(buffers.back(), opts.lex, diags, cats), true, {}, {}});
    if (opts.persistent) {
      for (const auto& [name, body] : persistentTable()) {
        MacroDef def;
        def.body = literal(body);
        macros[name] = std::move(def);
      }
    }
    if (opts.prelude) shared = &prelude();
  }

  // --- reading -----------------------------------------------------------

  /** While set, every token read is kept here, and one put back is dropped
   *  from it again: a delimited macro's call that is abandoned puts back
   *  everything it read, not only its last argument. */
  std::vector<ExpToken>* tape = nullptr;

  /** The next token, unexpanded. The end of an expansion is not a token:
   *  reading carries on in the source around it. */
  ExpToken raw() {
    ExpToken t = rawUntaped();
    if (tape != nullptr) tape->push_back(t);
    return t;
  }

  ExpToken rawUntaped() {
    if (halted) {
      ExpToken e;
      e.tok.kind = TokKind::end;
      return e;
    }
    while (true) {
      Frame& f = frames.back();
      if (!f.pushback.empty()) {
        ExpToken t = std::move(f.pushback.back());
        f.pushback.pop_back();
        return t;
      }
      if (f.root && holding) return endBeforeHeld();
      Token t = f.lexer->next();
      const std::string_view lead = f.buf.substr(t.leadStart, t.span.offset - t.leadStart);
      if (t.kind == TokKind::end && !f.root) {
        pendingLead.append(lead);
        frames.pop_back();
        continue;
      }
      ExpToken e;
      e.lead = std::move(pendingLead);
      pendingLead.clear();
      e.lead.append(lead);
      e.text = f.buf.substr(t.span.offset, t.span.length);
      e.tok = std::move(t);
      if (!f.root) e.tok.span = f.origin;
      if (f.root && e.tok.kind != TokKind::end && e.tok.span.offset >= limit) {
        held = std::move(e);
        holding = true;
        return endBeforeHeld();
      }
      return e;
    }
  }

  /** The end of the input as it is for now: where the held token is. */
  ExpToken endBeforeHeld() const {
    ExpToken e;
    e.tok.kind = TokKind::end;
    e.tok.span = held.tok.span;
    e.tok.span.length = 0;
    return e;
  }

  void endInputAt(std::uint32_t offset) {
    limit = offset;
    // An end read at the old limit was not the input's.
    for (Frame& f : frames) {
      auto& p = f.pushback;
      p.erase(std::remove_if(p.begin(), p.end(),
                             [](const ExpToken& t) { return t.tok.kind == TokKind::end; }),
              p.end());
    }
    if (holding && held.tok.span.offset < limit) {
      // Read after anything put back before it.
      auto& root = frames.front().pushback;
      root.insert(root.begin(), std::move(held));
      holding = false;
    }
    // The groups begun before it end with it, and what they defined.
    depth = 0;
    endLocalGroup();
  }

  void unread(ExpToken t) {
    if (tape != nullptr && !tape->empty()) tape->pop_back();
    frames.back().pushback.push_back(std::move(t));
  }

  ExpToken nextNonSpace() {
    ExpToken t = raw();
    while (t.tok.kind == TokKind::space) t = raw();
    return t;
  }

  static bool isChar(const ExpToken& t, char c) { return t.tok.isCharCode(c) && t.tok.cat == Cat::other; }

  /** The text inside a group whose `{` has been read, and the `}`. */
  std::string readGroupContent(const ExpToken& open) {
    std::string s;
    int depth = 1;
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        diags.warn(open.tok.span, "missing } inserted");
        unread(std::move(t));
        return s;
      }
      if (t.tok.isChar(Cat::beginGroup)) {
        depth++;
      } else if (t.tok.isChar(Cat::endGroup) && --depth == 0) {
        s += t.lead;
        return s;
      }
      s += t.lead;
      s.append(t.text);
    }
  }

  /** An undelimited argument: spaces are skipped, then one braced group
   *  (without its braces) or one token.
   *
   *  With `greedy`, a command given as the argument brings its own
   *  arguments with it -- `\sq\frac12` passes `\frac12` -- as it does for
   *  the engine's commands and always did for macros here. TeX would pass
   *  the bare `\frac` and fail. */
  std::string readArg(const std::string& who, bool greedy = true) {
    ExpToken t = nextNonSpace();
    if (t.tok.kind == TokKind::end || t.tok.kind == TokKind::par) {
      diags.warn(t.tok.span, "missing argument for " + who);
      unread(std::move(t));
      return {};
    }
    if (t.tok.isChar(Cat::endGroup)) {
      diags.warn(t.tok.span, "argument of " + who + " has an extra }");
      unread(std::move(t));
      return {};
    }
    if (t.tok.isChar(Cat::beginGroup)) return readGroupContent(t);
    std::string s(t.text);
    if (greedy && t.tok.isControl()) {
      // `\emph\emph\emph...` nests one level per command: a capacity, as
      // nesting is in the parser, before the stack runs out.
      if (argDepth >= kMaxDepth) throw ExpansionLimit("Input nested too deeply");
      argDepth++;
      appendOwnArgs(t.tok.text, s);
      argDepth--;
    }
    return s;
  }

  /** Read the arguments of command `name` -- an engine command or a macro
   *  -- onto `s`, in source form. */
  void appendOwnArgs(const std::string& name, std::string& s) {
    const auto group = [&](const std::string& who) { s += "{" + readArg(who) + "}"; };
    const auto option = [&]() {
      if (const auto o = readOptional()) s += "[" + *o + "]";
    };
    if (const MacroDef* def = lookup(name)) {
      if (def->alias || !def->delimiters.empty()) return;
      int i = 0;
      if (def->hasOptional && def->nparams > 0) option(), i = 1;
      for (; i < def->nparams; i++) group("\\" + name);
      return;
    }
    const CommandSpec* spec = findCommand(name);
    if (spec == nullptr || spec->special || spec->shape != Shape::prefix) return;
    for (const ArgSpec& a : spec->args) {
      if (a.optional) {
        option();
      } else {
        group("\\" + name);
      }
    }
  }

  /** `[...]` if one comes next (after spaces), with braces protecting a `]`. */
  std::optional<std::string> readOptional() {
    ExpToken t = nextNonSpace();
    if (!isChar(t, '[')) {
      unread(std::move(t));
      return std::nullopt;
    }
    const SourceSpan at = t.tok.span;
    std::string s;
    int depth = 0;
    while (true) {
      ExpToken u = raw();
      if (u.tok.kind == TokKind::end) {
        diags.warn(at, "missing ] inserted");
        unread(std::move(u));
        return s;
      }
      if (u.tok.isChar(Cat::beginGroup)) {
        depth++;
      } else if (u.tok.isChar(Cat::endGroup)) {
        if (depth == 0) {
          diags.warn(at, "missing ] inserted");
          unread(std::move(u));
          return s;
        }
        depth--;
      } else if (depth == 0 && isChar(u, ']')) {
        s += u.lead;
        return s;
      }
      s += u.lead;
      s.append(u.text);
    }
  }

  /** The name being defined: `\name` or `{\name}`. `command` is the
   *  definition command, for the error. */
  std::string readCsName(const std::string& command) {
    ExpToken t = nextNonSpace();
    if (t.tok.isControl()) return t.tok.text;
    std::string seen;
    if (t.tok.isChar(Cat::beginGroup)) {
      std::string name;
      int count = 0;
      bool control = true;
      while (true) {
        ExpToken u = raw();
        if (u.tok.kind == TokKind::end) {
          unread(std::move(u));
          break;
        }
        if (u.tok.isChar(Cat::endGroup)) break;
        seen += u.lead;
        seen.append(u.text);
        if (u.tok.kind == TokKind::space) continue;
        if (++count == 1 && u.tok.isControl()) {
          name = u.tok.text;
        } else {
          control = false;
        }
      }
      if (count == 1 && control) return name;
    } else {
      seen = std::string(t.text);
    }
    fail(command, "Invalid name for the command '" + seen);
  }

  /** The text of a group like `{name}` of an environment, trimmed. */
  std::optional<std::string> readGroupText() {
    ExpToken t = nextNonSpace();
    if (!t.tok.isChar(Cat::beginGroup)) {
      unread(std::move(t));
      return std::nullopt;
    }
    return trim(readGroupContent(t));
  }

  /** A definition's `[n]`. When it is not a number the definition is
   *  dropped whole: its default and its `groups` groups go with it. */
  int readParamCount(const std::optional<std::string>& text, const std::string& command, int groups) {
    if (!text) return 0;
    const std::string n = trim(*text);
    if (n.size() != 1 || n[0] < '0' || n[0] > '9') {
      failDefinition(command, "the number of arguments must be 0 to 9, not '" + n + "'", groups, true);
    }
    return n[0] - '0';
  }

  // --- replacement text ---------------------------------------------------

  /** Split a replacement text into literal pieces and parameters. `##`
   *  stands for one `#` (a definition inside a definition). */
  std::vector<BodyPiece> splitBody(const std::string& text, int nparams, const std::string& who,
                                   const SourceSpan& where) {
    std::vector<BodyPiece> pieces;
    std::string lit;
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    Lexer lx(text, lo, quiet, cats);
    const std::string_view src(text);
    const auto leadOf = [&](const Token& t) { return src.substr(t.leadStart, t.span.offset - t.leadStart); };
    const auto rawOf = [&](const Token& t) { return src.substr(t.span.offset, t.span.length); };
    std::optional<Token> ahead;
    const auto get = [&]() {
      if (ahead) {
        Token t = std::move(*ahead);
        ahead.reset();
        return t;
      }
      return lx.next();
    };
    while (true) {
      Token t = get();
      lit.append(leadOf(t));
      if (t.kind == TokKind::end) break;
      if (!t.isChar(Cat::param)) {
        lit.append(rawOf(t));
        continue;
      }
      Token d = lx.next();
      const bool adjacent = d.leadStart == d.span.offset;
      if (adjacent && d.kind == TokKind::character && d.cp >= '1' && d.cp <= '9') {
        const int k = static_cast<int>(d.cp - '0');
        if (k <= nparams) {
          if (!lit.empty()) pieces.push_back({std::move(lit), 0});
          lit.clear();
          pieces.push_back({"", k});
          continue;
        }
        diags.warn(where, "illegal parameter number #" + std::to_string(k) + " in the definition of " + who);
        lit.append(rawOf(t));
        lit.append(rawOf(d));
        continue;
      }
      if (adjacent && d.isChar(Cat::param)) {
        lit.append(rawOf(d));
        continue;
      }
      lit.append(rawOf(t));
      ahead = std::move(d);
    }
    if (!lit.empty()) pieces.push_back({std::move(lit), 0});
    return pieces;
  }

  static std::string substitute(const std::vector<BodyPiece>& pieces,
                                const std::vector<std::string>& args) {
    std::string s;
    for (const auto& p : pieces) {
      if (p.param == 0) {
        s += p.text;
      } else if (static_cast<std::size_t>(p.param) <= args.size()) {
        s += args[static_cast<std::size_t>(p.param) - 1];
      }
    }
    return s;
  }

  // --- arguments of a use ---------------------------------------------------

  std::vector<std::string> readArgs(const MacroDef& def, const std::string& who) {
    std::vector<std::string> args(static_cast<std::size_t>(def.nparams));
    if (!def.delimiters.empty()) return readDelimitedArgs(def, who);
    int i = 0;
    if (def.hasOptional && def.nparams > 0) {
      const auto o = readOptional();
      args[0] = o ? *o : def.optionalDefault;
      i = 1;
    }
    for (; i < def.nparams; i++) args[static_cast<std::size_t>(i)] = readArg(who);
    return args;
  }

  /** A delimited macro's arguments. A use that does not fit the definition
   *  is abandoned, as after TeX's error, and everything the call read is
   *  read again as ordinary input. */
  std::vector<std::string> readDelimitedArgs(const MacroDef& def, const std::string& who) {
    std::vector<std::string> args(static_cast<std::size_t>(def.nparams));
    std::vector<ExpToken> taken;
    tape = &taken;
    struct Untape {
      std::vector<ExpToken>*& t;
      ~Untape() { t = nullptr; }
    } untape{tape};
    for (const Token& d : def.delimiters[0]) {
      const ExpToken t = raw();
      if (!sameToken(t.tok, d)) {
        diags.warn(t.tok.span, "use of " + who + " does not match its definition; it is left out");
        abandoned = true;
        break;
      }
    }
    for (int i = 1; i <= def.nparams && !abandoned; i++) {
      const auto& delim = def.delimiters[static_cast<std::size_t>(i)];
      args[static_cast<std::size_t>(i) - 1] = delim.empty() ? readArg(who) : readUntil(delim, who);
    }
    tape = nullptr;
    if (abandoned) {
      // Each abandoned call re-reads what it puts back, so one in a loop
      // reads the rest of the input once per turn: a capacity, like the
      // expansion caps.
      runawayTokens += taken.size();
      if (runawayTokens > opts.maxExpandedBytes) {
        throw ExpansionLimit("Too many runaway arguments: is a macro missing its delimiter?");
      }
      for (auto it = taken.rbegin(); it != taken.rend(); ++it) unread(std::move(*it));
    }
    return args;
  }

  /** A delimited argument: everything up to the delimiter at brace depth
   *  0. One pair of braces around the whole argument is removed. When the
   *  delimiter never comes -- the input or the group ends first -- the
   *  call is abandoned (see readDelimitedArgs()). Taking it all as the
   *  argument instead let a macro that calls itself copy the rest of the
   *  input into every expansion. */
  std::string readUntil(const std::vector<Token>& delim, const std::string& who) {
    std::vector<ExpToken> got;
    int depth = 0;
    while (true) {
      ExpToken t = raw();
      const bool end = t.tok.kind == TokKind::end;
      if (end || (t.tok.isChar(Cat::endGroup) && depth == 0)) {
        diags.warn(t.tok.span, end ? "runaway argument: " + who + " is missing its delimiter; it is left out"
                                   : "argument of " + who + " has an extra }; it is left out");
        abandoned = true;
        return {};
      }
      if (t.tok.isChar(Cat::beginGroup)) {
        depth++;
      } else if (t.tok.isChar(Cat::endGroup)) {
        depth--;
      }
      got.push_back(std::move(t));
      if (depth == 0 && got.size() >= delim.size()) {
        bool match = true;
        const std::size_t base = got.size() - delim.size();
        for (std::size_t k = 0; k < delim.size() && match; k++) match = sameToken(got[base + k].tok, delim[k]);
        if (match) {
          got.resize(base);
          break;
        }
      }
    }
    std::size_t from = 0, to = got.size();
    if (got.size() >= 2 && got.front().tok.isChar(Cat::beginGroup) && got.back().tok.isChar(Cat::endGroup)) {
      // Strip only if the first brace closes at the very end.
      int d = 0;
      bool whole = true;
      for (std::size_t k = 0; k < got.size(); k++) {
        if (got[k].tok.isChar(Cat::beginGroup)) d++;
        if (got[k].tok.isChar(Cat::endGroup)) d--;
        if (d == 0 && k + 1 < got.size()) whole = false;
      }
      if (whole) from = 1, to = got.size() - 1;
    }
    std::string s;
    for (std::size_t k = from; k < to; k++) {
      if (k > from) s += got[k].lead;
      s.append(got[k].text);
    }
    if (to < got.size() && to > from) s += got[to].lead;
    return s;
  }

  // --- output -------------------------------------------------------------

  void emitLead(const std::string& lead) {
    if (lead.empty()) return;
    out += lead;
    lastControlWord = false;
  }

  void emitText(std::string_view text, bool controlWord) {
    if (text.empty()) return;
    const auto c = static_cast<unsigned char>(text[0]);
    // Two tokens that were apart must not run together into one name.
    if (lastControlWord && c < 0x80 && cats.get(c) == Cat::letter) out += ' ';
    out.append(text);
    lastControlWord = controlWord;
  }

  void emit(const ExpToken& t) {
    emitLead(t.lead);
    emitText(t.text, t.tok.kind == TokKind::controlWord);
  }

  // --- expansion --------------------------------------------------------

  void pushExpansion(std::string text, const SourceSpan& at) {
    if (++expansions > opts.maxExpansions) {
      throw ExpansionLimit("Too many macro expansions: is a macro defined in terms of itself?");
    }
    expandedBytes += text.size();
    if (expandedBytes > opts.maxExpandedBytes) {
      throw ExpansionLimit("Macro expansion is too large: is a macro defined in terms of itself?");
    }
    buffers.push_back(std::move(text));
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    // `at` is already an input position: a token read from an expansion
    // carries the origin of that expansion.
    frames.push_back({buffers.back(), std::make_unique<Lexer>(buffers.back(), lo, quiet, cats), false, at, {}});
  }

  /** A name of the prelude's that a document is free to define itself. */
  bool isSoft(const std::string& name) const {
    return macros.count(name) == 0 && shared != nullptr && shared->soft.count(name) > 0;
  }

  bool isDefined(const std::string& name) const {
    if (isSoft(name)) return opts.isBuiltinCommand && opts.isBuiltinCommand(name);
    return lookup(name) != nullptr || (opts.isBuiltinCommand && opts.isBuiltinCommand(name));
  }

  /** A macro: defined in this parse (define_macro()'s are copied in),
   *  else in the prelude. */
  const MacroDef* lookup(const std::string& name) const {
    const auto it = macros.find(name);
    if (it != macros.end()) return &it->second;
    if (shared == nullptr) return nullptr;
    const auto p = shared->macros.find(name);
    return p == shared->macros.end() ? nullptr : &p->second;
  }

  const MacroDef* lookupEnv(const std::string& name) const {
    const auto it = envs.find(name);
    if (it != envs.end()) return &it->second;
    if (shared == nullptr) return nullptr;
    const auto p = shared->envs.find(name);
    if (p != shared->envs.end()) return &p->second;
    // A starred prelude environment is its plain form: the star turns off
    // numbering (equation*), and nothing is numbered here.
    if (name.size() > 1 && name.back() == '*') {
      const auto q = shared->envs.find(name.substr(0, name.size() - 1));
      if (q != shared->envs.end()) return &q->second;
    }
    return nullptr;
  }

  bool takeStar() {
    ExpToken t = raw();
    if (isChar(t, '*')) return true;
    unread(std::move(t));
    return false;
  }

  bool expandMacro(const ExpToken& e) {
    const std::string& name = e.tok.text;
    const MacroDef* def = nullptr;
    const auto& starred = starredBuiltins();
    const auto star = starred.find(name + "*");
    if (star != starred.end() && !macros.count(name)) {
      ExpToken t = raw();
      if (isChar(t, '*')) {
        def = &star->second;
      } else {
        unread(std::move(t));
      }
    }
    if (def == nullptr) {
      def = lookup(name);
      // A macro with a starred twin, `cmd*`, as \DeclarePairedDelimiter makes.
      if (def != nullptr) {
        if (const MacroDef* twin = lookup(name + "*")) {
          ExpToken t = raw();
          if (isChar(t, '*')) {
            def = twin;
          } else {
            unread(std::move(t));
          }
        }
      }
    }
    if (def == nullptr) return false;

    const std::string who = "\\" + name;
    if (def->alias) {
      unread(aliasToken(def->body.front().text, e.tok.span));
      return true;
    }
    std::vector<std::string> args = readArgs(*def, who);
    if (abandoned) {
      abandoned = false;
      return true;
    }
    pushExpansion(substitute(def->body, args), e.tok.span);
    return true;
  }

  /** The one token a \let alias stands for, marked so that it is never
   *  expanded again: it means what its name meant when \let ran. */
  ExpToken aliasToken(const std::string& target, const SourceSpan& at) {
    buffers.push_back(target);
    const std::string& buf = buffers.back();
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    Lexer lx(buf, lo, quiet, cats);
    ExpToken t;
    t.tok = lx.next();
    t.text = std::string_view(buf).substr(t.tok.span.offset, t.tok.span.length);
    t.tok.span = at;
    t.tok.noexpand = true;
    return t;
  }

  /** What \arraystretch is, as a number, when an array of this name takes
   *  it and it is not 1; else empty. */
  std::string arrayStretch(const std::string& env) const {
    static const char* names[] = {"array", "tabular", "tabular*", "tabularx", "longtable", "matrix",
                                  "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix", "smallmatrix"};
    bool takes = false;
    for (const char* n : names) takes = takes || env == n;
    const MacroDef* def = takes ? lookup("arraystretch") : nullptr;
    if (def == nullptr || def->nparams != 0) return "";
    std::string text;
    for (const auto& piece : def->body) text += piece.text;
    text = trim(text);
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || end == text.c_str() || *end != '\0' || !(v > 0.0) || v == 1.0) return "";
    return text;
  }

  bool environment(const ExpToken& e, bool begin) {
    ExpToken open = nextNonSpace();
    if (!open.tok.isChar(Cat::beginGroup)) {
      unread(std::move(open));
      return false;
    }
    std::vector<ExpToken> consumed;
    consumed.push_back(open);
    std::string name;
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        unread(std::move(t));
        break;
      }
      const bool close = t.tok.isChar(Cat::endGroup);
      if (!close) {
        name += t.lead;
        name.append(t.text);
      }
      consumed.push_back(std::move(t));
      if (close) break;
    }
    name = trim(name);
    const MacroDef* def = lookupEnv(name);
    if (def == nullptr) {
      // Not ours: hand `\begin{name}` on as it came. The name is read
      // again, and expanded, if it holds a macro (`\begin{\env}`).
      for (auto it = consumed.rbegin(); it != consumed.rend(); ++it) unread(std::move(*it));
      const std::string stretch = begin ? arrayStretch(name) : "";
      if (!stretch.empty()) {
        // \arraystretch is read where the array begins: what it says goes
        // ahead of \begin, which then passes as it is.
        ExpToken again = e;
        again.lead.clear();
        again.tok.noexpand = true;
        unread(std::move(again));
        pushExpansion("\\gmarraystretch{" + stretch + "}", e.tok.span);
        return true;
      }
      return false;
    }
    // An environment is a group, and the old parser made its expansion a
    // braced one -- an ordinary atom in math -- which spacing depends on.
    if (begin) {
      std::vector<std::string> args = readArgs(*def, "\\begin{" + name + "}");
      if (opts.recover) openEnvs.push_back({name, e.tok.span, depth});
      pushExpansion("{" + substitute(def->body, args), e.tok.span);
      ExpToken open = rawUntaped();
      if (envs.find(name) == envs.end()) {  // the prelude's, not the input's
        const bool transparent =
          (def->body.empty() && def->endBody.empty()) || isBlockEnvironment(name);
        open.tok.environment = !transparent             ? (isDisplayEnvironment(name) ? 3 : 1)
                               : isFloatEnvironment(name) ? 4
                               : name == "center"        ? 5
                               : name == "flushleft"     ? 6
                               : name == "flushright"    ? 7
                                                          : 2;
      }
      frames.back().pushback.push_back(std::move(open));
      return true;
    }
    if (opts.recover) {
      auto it = std::find_if(openEnvs.rbegin(), openEnvs.rend(),
                             [&](const OpenEnv& o) { return o.name == name; });
      if (it == openEnvs.rend()) {
        diags.warn(e.tok.span, "\\end{" + name + "} without \\begin ignored");
        return true;
      }
      openEnvs.erase(std::next(it).base());
    }
    endEnvironment(name, e.tok.span);
    return true;
  }

  void endEnvironment(const std::string& name, const SourceSpan& at) {
    pushExpansion(substitute(lookupEnv(name)->endBody, {}) + "}", at);
  }

  /** In recover mode, the environments of ours begun and not yet ended,
   *  each with the brace depth it began at: the end of the group around it,
   *  or of the input, closes it. */
  struct OpenEnv {
    std::string name;
    SourceSpan at;
    int depth;
  };
  std::vector<OpenEnv> openEnvs;

  /** The brace depth of the tokens handed out so far. An environment's
   *  expansion opens a group of its own, so a `}` that would close the
   *  group around an environment still open is one its \end should have
   *  come before. */
  int depth = 0;

  /** What a name meant before something defined it inside a group, to be
   *  put back when that group ends: a definition is local in TeX, unless
   *  \gdef makes it global. */
  struct Undo {
    std::string name;
    bool environment;
    bool had;
    MacroDef previous;
    int depth;
  };
  std::vector<Undo> undoStack;

  /** About to define `name` here: note what it means now, so the end of
   *  the group it is defined in puts that back. */
  void defineLocally(const std::string& name, bool environment = false) {
    if (depth <= 0) return;
    const auto& table = environment ? envs : macros;
    const auto it = table.find(name);
    if (it != table.end()) {
      // What the group keeps to put back counts against the byte cap: a
      // loop redefining a large macro in a group, `{\def\a{\let\c\b\a}\a}`,
      // would otherwise hold a copy of it per turn.
      std::size_t kept = it->second.optionalDefault.size();
      for (const auto& p : it->second.body) kept += p.text.size();
      for (const auto& p : it->second.endBody) kept += p.text.size();
      expandedBytes += kept;
      if (expandedBytes > opts.maxExpandedBytes) {
        throw ExpansionLimit("Macro expansion is too large: is a macro defined in terms of itself?");
      }
    }
    undoStack.push_back(
      {name, environment, it != table.end(), it != table.end() ? it->second : MacroDef(), depth});
  }

  /** A group has just ended: what it defined goes with it. */
  void endLocalGroup() {
    while (!undoStack.empty() && undoStack.back().depth > depth) {
      const Undo u = std::move(undoStack.back());
      undoStack.pop_back();
      auto& table = u.environment ? envs : macros;
      if (u.had) {
        table[u.name] = u.previous;
      } else {
        table.erase(u.name);
      }
    }
  }

  /** The end of the innermost environment left open, if any: true when
   *  there was one. */
  bool closeOpenEnvironment() {
    if (openEnvs.empty() || halted) return false;
    const OpenEnv o = openEnvs.back();
    openEnvs.pop_back();
    diags.warn(o.at, "missing \\end{" + o.name + "} inserted");
    endEnvironment(o.name, o.at);
    return true;
  }

  // --- definitions ------------------------------------------------------

  /** Skip to the next group and past it. */
  void skipGroup() {
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        unread(std::move(t));
        return;
      }
      if (t.tok.isChar(Cat::beginGroup)) {
        readGroupContent(t);
        return;
      }
    }
  }

  /** A definition that cannot be made is dropped whole in recover mode:
   *  the rest of it -- optional arguments, then `groups` groups -- is
   *  skipped, or it would be drawn. Then the error is raised. */
  [[noreturn]] void failDefinition(const std::string& command, const std::string& message,
                                   int groups, bool optionals = false) {
    if (opts.recover) {
      while (optionals && readOptional()) {
      }
      for (int i = 0; i < groups; i++) skipGroup();
    }
    fail(command, message);
  }

  void defineCommand(const ExpToken& e, const std::string& kind) {
    takeStar();
    std::string name;
    try {
      name = readCsName(kind);
    } catch (const ex_parse& x) {
      failDefinition(kind, cleanMessage(x.what()), 1, true);
    }
    MacroDef def;
    def.nparams = readParamCount(readOptional(), kind, 1);
    if (const auto d = readOptional()) {
      def.hasOptional = true;
      def.optionalDefault = *d;
    }
    const std::string body = readArg("\\" + kind, false);
    def.body = splitBody(body, def.nparams, "\\" + name, e.tok.span);

    const bool exists = isDefined(name);
    if (kind == "newcommand" && exists) {
      fail(kind, "Command " + name + " already exists! Use renewcommand instead!");
    }
    if (kind == "renewcommand" && !exists && !isSoft(name) && !isSiunitx(name)) {
      // LaTeX complains and defines it anyway; so does recover mode.
      if (!opts.recover) fail(kind, "Command " + name + " is no defined! Use newcommand instead!");
      diags.warn(e.tok.span, "\\renewcommand: \\" + name + " was not defined; defined now");
    }
    if (!(kind == "providecommand" && exists)) {
      defineLocally(name);
      macros[name] = std::move(def);
    }
  }

  void declareOperator() {
    const bool star = takeStar();
    const std::string name = readCsName("newcommand");
    const std::string text = readArg("\\DeclareMathOperator");
    if (isDefined(name)) {
      fail("newcommand", "Command " + name + " already exists! Use renewcommand instead!");
    }
    MacroDef def;
    def.body = literal("\\mathop{\\mathrm{" + text + "}}" + (star ? "\\limits" : "\\nolimits"));
    defineLocally(name);
    macros[name] = std::move(def);
  }

  /** mathtools' \DeclarePairedDelimiter{\cmd}{left}{right}: `\cmd{x}` sets
   *  the delimiters at the size they are in, `\cmd[\big]{x}` at the size
   *  given, and `\cmd*{x}` as \left...\right does. The starred form is
   *  the macro `cmd*`, which expandMacro() looks for after a `*`. */
  void declarePairedDelimiter(const ExpToken& e) {
    const std::string name = readCsName("newcommand");
    const std::string left = readArg("\\DeclarePairedDelimiter");
    const std::string right = readArg("\\DeclarePairedDelimiter");
    if (isDefined(name)) {
      fail("newcommand", "Command " + name + " already exists! Use renewcommand instead!");
    }
    const std::string who = "\\" + name;
    // A space after the left delimiter, which an argument could run into.
    MacroDef plain;
    plain.nparams = 2;
    plain.hasOptional = true;
    plain.body = splitBody("#1" + left + " #2#1" + right, 2, who, e.tok.span);
    MacroDef starred;
    starred.nparams = 1;
    starred.body = splitBody("\\left" + left + " #1\\right" + right, 1, who + "*", e.tok.span);
    defineLocally(name);
    macros[name] = std::move(plain);
    defineLocally(name + "*");
    macros[name + "*"] = std::move(starred);
  }

  /** amsthm's \newtheorem{name}[shared]{Title}[within], and the starred one
   *  with no number: the environment `name`, whose head (the title, its
   *  number and the optional note) is the lowering's \gmtheorem, which
   *  holds the counters. The style is \theoremstyle's: plain sets its body
   *  in italics, the others upright; a remark's head is italic too. */
  void declareTheorem(const ExpToken& e) {
    const bool star = takeStar();
    const auto name = readGroupText();
    if (!name || name->empty()) {
      failDefinition("newtheorem", "\\newtheorem: missing the environment name", 2, true);
    }
    const auto shared = readOptional();
    const std::string title = readArg("\\newtheorem", false);
    const auto within = readOptional();
    if (lookupEnv(*name) != nullptr ||
        (opts.isBuiltinEnvironment && opts.isBuiltinEnvironment(*name))) {
      fail("newtheorem", "Environment " + *name + " already defined!");
    }
    const std::string counter = star ? "" : shared ? trim(*shared) : *name;
    std::string in = within && !shared && !star ? trim(*within) : "";
    if (shared && !star) {
      const auto it = theoremWithin.find(counter);
      in = it == theoremWithin.end() ? "" : it->second;
    }
    theoremWithin[*name] = in;
    MacroDef def;
    def.nparams = 1;
    def.hasOptional = true;
    const std::string begin = "\\par\\gmtheorem{" + theoremStyle + "}{" + counter + "}{" + in +
                              "}{" + title + "}{#1}\\ " +
                              (theoremStyle == "plain" ? "\\itshape " : "");
    def.body = splitBody(begin, 1, "\\begin{" + *name + "}", e.tok.span);
    def.endBody = splitBody("\\par", 0, "\\end{" + *name + "}", e.tok.span);
    defineLocally(*name, true);
    envs[*name] = std::move(def);
  }

  /** \ce and \pu, a subset of mhchem (front/mhchem.h): the text is read and what
   *  it makes is read in its place. */
  void chemCommand(const ExpToken& e) {
    const std::string name = e.tok.text;
    const std::string arg = readArg("\\" + name);
    std::string problem;
    const std::string out =
      name == "ce" ? mhchem::ce(arg, problem) : mhchem::pu(arg, siUnits, problem);
    if (!problem.empty()) diags.warn(e.tok.span, "\\" + name + ": " + problem);
    pushExpansion("\\ensuremath{" + out + "}", e.tok.span);
  }

  static bool isSiunitx(const std::string& n) {
    static const char* names[] = {"num",      "si",      "SI",      "unit",    "qty",
                                  "ang",      "numrange", "SIrange", "qtyrange", "numlist",
                                  "SIlist",   "qtylist", "sisetup", "DeclareSIUnit"};
    for (const char* x : names) {
      if (n == x) return true;
    }
    return false;
  }

  /** A subset of siunitx (front/siunitx.h): the command's arguments are
   *  read, formatted, and what they make is read in their place. Its
   *  options are read and not used. */
  void siunitxCommand(const ExpToken& e) {
    const std::string name = e.tok.text;
    const SourceSpan at = e.tok.span;
    readOptional();
    std::string problem;
    const auto say = [&] {
      if (!problem.empty()) diags.warn(at, "\\" + name + ": " + problem);
      problem.clear();
    };
    const std::string who = "\\" + name;
    if (name == "sisetup") {
      readArg(who);
      return;
    }
    if (name == "DeclareSIUnit") {
      std::string macro = trim(readArg(who));
      const std::string text = readArg(who);
      if (!macro.empty() && macro[0] == '\\') macro.erase(0, 1);
      if (!macro.empty()) siUnits[macro] = text;
      return;
    }
    const auto num = [&](const std::string& s) {
      const std::string out = siunitx::number(s, problem);
      say();
      return out;
    };
    const auto unitOf = [&](const std::string& s) {
      const std::string out = siunitx::unit(s, siUnits, problem);
      say();
      return out;
    };
    const auto with = [&](const std::string& n, const std::string& u, const std::string& pre) {
      const std::string unit = u.empty() ? "" : unitOf(u);
      return num(n) + pre + (unit.empty() ? "" : "\\," + unit);
    };
    std::string out;
    if (name == "num") {
      out = num(readArg(who));
    } else if (name == "si" || name == "unit") {
      out = unitOf(readArg(who));
    } else if (name == "SI") {
      const std::string n = readArg(who);
      const auto pre = readOptional();
      out = with(n, readArg(who), pre ? *pre : "");
    } else if (name == "qty") {
      const std::string n = readArg(who);
      out = with(n, readArg(who), "");
    } else if (name == "ang") {
      out = siunitx::angle(readArg(who), problem);
      say();
    } else if (name == "numrange") {
      const std::string a = readArg(who);
      out = siunitx::range(num(a), num(readArg(who)));
    } else if (name == "SIrange" || name == "qtyrange") {
      const std::string a = readArg(who), b = readArg(who), u = readArg(who);
      out = siunitx::range(with(a, u, ""), with(b, u, ""));
    } else if (name == "numlist") {
      out = siunitx::numberList(readArg(who), problem);
      say();
    } else {  // SIlist, qtylist: each number with the unit, as siunitx repeats it
      const std::string list = readArg(who), u = readArg(who);
      std::vector<std::string> items{""};
      for (const char c : list) {
        if (c == ';') {
          items.emplace_back();
        } else {
          items.back() += c;
        }
      }
      for (std::size_t k = 0; k < items.size(); k++) {
        if (k > 0) out += items.size() == 2 ? "\\text{ and }" : k + 1 == items.size() ? "\\text{, and }" : "\\text{, }";
        out += with(items[k], u, "");
      }
    }
    pushExpansion("\\ensuremath{" + out + "}", at);
  }

  /** \global was the last thing read: the definition after it is for good. */
  bool globalPrefix = false;

  bool takeGlobal() {
    const bool g = globalPrefix;
    globalPrefix = false;
    return g;
  }

  void defineDef(const ExpToken& e) {
    const bool global = takeGlobal();
    ExpToken n = raw();
    if (!n.tok.isControl()) failDefinition("def", "\\def: expected '\\' before the name", 1);
    const std::string name = n.tok.text;
    MacroDef def;
    def.delimiters.emplace_back();
    std::string body;
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        fail("def", "\\def: missing '{' before the body of \\" + name);
      }
      if (t.tok.isChar(Cat::beginGroup)) {
        body = readGroupContent(t);
        break;
      }
      if (t.tok.isChar(Cat::param)) {
        ExpToken d = raw();
        if (d.tok.kind == TokKind::character && d.tok.cp >= '1' && d.tok.cp <= '9') {
          const int k = static_cast<int>(d.tok.cp - '0');
          if (k != def.nparams + 1) failDefinition("def", "\\def: parameters must be sequential starting at #1", 1);
          def.nparams = k;
          def.delimiters.emplace_back();
          continue;
        }
        if (d.tok.isChar(Cat::beginGroup)) {
          body = readGroupContent(d);
          break;
        }
        failDefinition("def", "\\def: '#' must be followed by a digit 1..9", 1);
      }
      def.delimiters.back().push_back(t.tok);
    }
    bool delimited = false;
    for (const auto& d : def.delimiters) delimited = delimited || !d.empty();
    if (!delimited) def.delimiters.clear();
    if (e.tok.text == "edef" || e.tok.text == "xdef") body = expandBody(body, e.tok.span);
    def.body = splitBody(body, def.nparams, "\\" + name, e.tok.span);
    // \gdef defines globally; \def, like everything else, in its group.
    if (e.tok.text != "gdef" && e.tok.text != "xdef" && !global) defineLocally(name);
    macros[name] = std::move(def);
  }

  void defineLet() {
    const bool global = takeGlobal();
    ExpToken n = raw();
    if (!n.tok.isControl()) fail("let", "\\let: expected a control sequence after \\let");
    const std::string name = n.tok.text;
    ExpToken t = nextNonSpace();
    if (isChar(t, '=')) {
      t = raw();
      if (t.tok.kind == TokKind::space) t = raw();
    }
    if (t.tok.kind == TokKind::end) fail("let", "\\let: missing the token to copy into \\" + name);
    letFrom(name, t, global);
  }

  /** `\name` made to mean what the token `t` means now. */
  void letFrom(const std::string& name, const ExpToken& t, bool global = false) {
    MacroDef def;
    const MacroDef* user = t.tok.isControl() ? lookup(t.tok.text) : nullptr;
    if (user != nullptr) {
      def = *user;
    } else {
      def.alias = true;
      def.body = literal(std::string(t.text));
    }
    if (!global) defineLocally(name);
    macros[name] = std::move(def);
  }

  /** \futurelet\cs\a\b: `\cs` means what `\b` does, and `\a\b` is read as if
   *  nothing had been looked at. */
  void futureLet() {
    const bool global = takeGlobal();
    ExpToken n = raw();
    if (!n.tok.isControl()) fail("futurelet", "\\futurelet: expected a control sequence after \\futurelet");
    ExpToken first = raw();
    ExpToken second = raw();
    if (second.tok.kind == TokKind::end) fail("futurelet", "\\futurelet: missing the token to copy into \\" + n.tok.text);
    letFrom(n.tok.text, second, global);
    unread(std::move(second));
    unread(std::move(first));
  }

  /** \@ifstar{yes}{no} and \@ifnextchar X{yes}{no}, as LaTeX's: `yes` when the next
   *  token (spaces skipped) is the star, or X; \@ifstar takes the star. */
  void ifNextChar(const ExpToken& e, bool star) {
    const std::string who = "\\" + e.tok.text;
    const ExpToken test = star ? ExpToken() : nextNonSpace();
    const std::string yes = readArg(who, false), no = readArg(who, false);
    ExpToken next = nextNonSpace();
    const bool match = star ? isChar(next, '*') : (next.tok.kind == test.tok.kind && next.text == test.text);
    if (!(match && star)) unread(std::move(next));
    pushExpansion(match ? yes : no, e.tok.span);
  }

  /** \expandafter\a\b: `\b` is expanded once, then `\a` is read before what it made. */
  void expandAfter() {
    ExpToken first = raw();
    ExpToken second = raw();
    if (second.tok.kind == TokKind::controlWord && second.tok.text == "expandafter" && !second.tok.noexpand &&
        !macros.count("expandafter")) {
      expandAfter();
    } else if (second.tok.kind == TokKind::end || !second.tok.isControl() || second.tok.noexpand ||
               !expandMacro(second)) {
      unread(std::move(second));
    }
    unread(std::move(first));
  }

  /** The text `body` expands to, as \edef makes a macro's: everything that expands
   *  does, and \noexpand's token and the parameters are left alone. */
  std::string expandBody(const std::string& body, const SourceSpan& at) {
    std::string saved = std::move(out), savedCarry = std::move(carry);
    const bool savedLast = lastControlWord;
    out.clear();
    carry.clear();
    lastControlWord = false;
    pushExpansion(body + "\\gmedefend ", at);
    while (true) {
      ExpToken t = nextExpanded();
      if (t.tok.kind == TokKind::end || (t.tok.kind == TokKind::controlWord && t.tok.text == "gmedefend")) break;
      emit(t);
    }
    std::string result = std::move(out);
    out = std::move(saved);
    carry = std::move(savedCarry);
    lastControlWord = savedLast;
    return result;
  }

  void defineEnvironment(const ExpToken& e, const std::string& kind) {
    takeStar();
    const auto name = readGroupText();
    if (!name || name->empty()) failDefinition(kind, "\\" + kind + ": missing the environment name", 2, true);
    MacroDef def;
    def.nparams = readParamCount(readOptional(), kind, 2);
    if (const auto d = readOptional()) {
      def.hasOptional = true;
      def.optionalDefault = *d;
    }
    const std::string begin = readArg("\\" + kind, false);
    const std::string end = readArg("\\" + kind, false);
    def.body = splitBody(begin, def.nparams, "\\begin{" + *name + "}", e.tok.span);
    def.endBody = splitBody(end, 0, "\\end{" + *name + "}", e.tok.span);

    const bool exists = lookupEnv(*name) != nullptr ||
                        (opts.isBuiltinEnvironment && opts.isBuiltinEnvironment(*name));
    if (kind == "newenvironment" && exists) {
      fail(kind, "Command " + *name + "@env already exists! Use renewcommand instead!");
    }
    if (kind == "renewenvironment" && !exists) {
      fail(kind, "Environment " + *name + " is not defined! Use newenvironment instead!");
    }
    defineLocally(*name, true);
    envs[*name] = std::move(def);
  }

  /** Handle a control sequence that is a definition, an environment or a
   *  macro: true when it was consumed, false when it passes through. What
   *  it consumed puts nothing in the output but its leading whitespace,
   *  which the caller carries on to the next token. */
  bool process(const ExpToken& e) {
    const std::string& name = e.tok.text;
    if (e.tok.kind == TokKind::controlWord) {
      if (name == "newcommand" || name == "renewcommand" || name == "providecommand") {
        defineCommand(e, name);
        return true;
      }
      if (name == "DeclareMathOperator") {
        declareOperator();
        return true;
      }
      if (name == "DeclarePairedDelimiter") {
        declarePairedDelimiter(e);
        return true;
      }
      if ((name == "ce" || name == "pu") && !macros.count(name)) {
        chemCommand(e);
        return true;
      }
      if (isSiunitx(name) && !macros.count(name)) {
        siunitxCommand(e);
        return true;
      }
      if (name == "newtheorem") {
        declareTheorem(e);
        return true;
      }
      if (name == "theoremstyle") {
        if (const auto style = readGroupText()) theoremStyle = *style;
        return true;
      }
      if (name == "def" || name == "gdef" || name == "edef" || name == "xdef") {
        defineDef(e);
        return true;
      }
      if ((name == "@ifstar" || name == "@ifnextchar") && !macros.count(name)) {
        ifNextChar(e, name == "@ifstar");
        return true;
      }
      if (name == "global" && !macros.count(name)) {
        globalPrefix = true;
        return true;
      }
      if (name == "expandafter" && !macros.count(name)) {
        expandAfter();
        return true;
      }
      if (name == "noexpand" && !macros.count(name)) {
        ExpToken t = raw();
        t.tok.noexpand = true;
        unread(std::move(t));
        return true;
      }
      if (name == "futurelet" && !macros.count(name)) {
        futureLet();
        return true;
      }
      if (name == "let") {
        defineLet();
        return true;
      }
      if (name == "newenvironment" || name == "renewenvironment") {
        defineEnvironment(e, name);
        return true;
      }
      if (name == "makeatletter" || name == "makeatother") {
        // Passed on too: the old parser keeps its own count.
        atLetter += name == "makeatletter" ? 1 : -1;
        cats.set('@', atLetter > 0 ? Cat::letter : Cat::other);
        return false;
      }
      if ((name == "begingroup" || name == "endgroup") && !macros.count(name)) {
        // TeX's group without braces, which kableExtra's font_size writes.
        pushExpansion(name == "begingroup" ? "{" : "}", e.tok.span);
        return true;
      }
      if ((name == "begin" || name == "end") && !macros.count(name)) {
        return environment(e, name == "begin");
      }
    }
    return expandMacro(e);
  }

  /** process(), with errors reported rather than thrown in recover mode: a
   *  bad definition is skipped; runaway expansion ends the input there. */
  bool consume(const ExpToken& e) {
    if (!opts.recover) return process(e);
    try {
      return process(e);
    } catch (const ExpansionLimit& x) {
      diags.error(e.tok.span, cleanMessage(x.what()));
      halted = true;
      return true;
    } catch (const ex_parse& x) {
      std::string msg = cleanMessage(x.what());
      const std::string who = "\\" + e.tok.text + ":";
      if (msg.compare(0, who.size(), who) != 0) msg = who + " " + msg;
      diags.warn(e.tok.span, msg);
      return true;
    }
  }

  /** Leading whitespace of consumed tokens, owed to the next token out. */
  std::string carry;

  /** The next token after expansion. */
  ExpToken nextExpanded() {
    while (true) {
      ExpToken e = raw();
      if (e.tok.kind != TokKind::end && e.tok.isControl() && !e.tok.noexpand && consume(e)) {
        carry += e.lead;
        continue;
      }
      if (e.tok.kind == TokKind::end && frames.size() == 1 && !openEnvs.empty()) {
        // The end is read again after the environment's closing code.
        unread(std::move(e));
        if (closeOpenEnvironment()) continue;
        e = raw();
      }
      if (e.tok.isChar(Cat::endGroup) && !openEnvs.empty() && !halted &&
          depth == openEnvs.back().depth + 1) {
        // This `}` closes the group the environment began in: the
        // environment ends first, and the `}` is read again after it.
        unread(std::move(e));
        closeOpenEnvironment();
        continue;
      }
      if (e.tok.isChar(Cat::beginGroup)) depth++;
      if (e.tok.isChar(Cat::endGroup)) {
        depth--;
        endLocalGroup();
      }
      if (!carry.empty()) {
        e.lead = carry + e.lead;
        carry.clear();
      }
      return e;
    }
  }

  std::string run() {
    while (true) {
      ExpToken e = nextExpanded();
      if (e.tok.kind == TokKind::end) {
        emitLead(e.lead);
        break;
      }
      emit(e);
    }
    return std::move(out);
  }
};

Expander::Expander(std::string input, ExpanderOptions options, Diagnostics& diagnostics)
    : _impl(std::make_unique<Impl>(std::move(input), std::move(options), diagnostics)) {}

Expander::~Expander() = default;

std::string Expander::expandToText() {
  return _impl->run();
}

std::vector<std::string> preludeCommandNames() {
  std::vector<std::string> names;
  for (const auto& kv : Expander::Impl::prelude().macros) names.push_back(kv.first);
  return names;
}

std::vector<std::string> preludeEnvironmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : Expander::Impl::prelude().envs) names.push_back(kv.first);
  return names;
}

std::vector<std::string> preludeTransparentEnvironmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : Expander::Impl::prelude().envs) {
    // The same test that marks the group transparent when one is expanded.
    if ((kv.second.body.empty() && kv.second.endBody.empty()) || isBlockEnvironment(kv.first)) {
      names.push_back(kv.first);
    }
  }
  return names;
}

ExpandedToken Expander::next() {
  ExpToken e = _impl->nextExpanded();
  return {std::move(e.tok), std::move(e.lead), std::string(e.text)};
}

void Expander::endInputAt(std::uint32_t offset) {
  _impl->endInputAt(offset);
}

void Expander::setCatcode(c32 ch, Cat cat) {
  _impl->cats.set(ch, cat);
}

Cat Expander::catcode(c32 ch) const {
  return _impl->cats.get(ch);
}

void setPersistentMacro(const std::string& name, const std::string& body) {
  auto& table = persistentTable();
  generation()++;
  for (auto& entry : table) {
    if (entry.first == name) {
      entry.second = body;
      return;
    }
  }
  table.emplace_back(name, body);
}

bool removePersistentMacro(const std::string& name) {
  auto& table = persistentTable();
  for (auto it = table.begin(); it != table.end(); ++it) {
    if (it->first == name) {
      table.erase(it);
      generation()++;
      return true;
    }
  }
  return false;
}

void clearPersistentMacros() {
  if (persistentTable().empty()) return;
  persistentTable().clear();
  generation()++;
}

const std::vector<std::pair<std::string, std::string>>& persistentMacros() {
  return persistentTable();
}

std::uint64_t persistentMacroGeneration() {
  return generation();
}

}  // namespace microtex::front
