#include "front/expander.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "front/hooks.h"
#include "front/packages.h"
#include "front/registers.h"
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
  /** It was read from text written with `@` a letter (Frame::atLetter): what
   *  a macro it names is given to read is so too, whoever wrote the macro. */
  bool atLetter = false;
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
  /** A package's file: its tokens keep the positions they have in it, which
   *  Diagnostics::Package reports as the file's own. */
  bool keepSpan = false;
  /** Its lexer reads `@` as a letter in a name (LexOptions::atLetter). */
  bool atLetter = false;
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
    // Its limits go over and under in a display and beside it in text, as \lim's do (amsmath: no \nolimits).
    add("operatorname*", 1, {{"\\mathop{\\mathrm{", 0}, {"", 1}, {"}}", 0}});
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

/** Macros and environments defined once and shared by every parse: what the
 *  format and the packages every input starts with define. */
struct Definitions {
  std::unordered_map<std::string, MacroDef> macros;
  std::unordered_map<std::string, MacroDef> envs;
  /** The packages read to make them. */
  std::unordered_set<std::string> loaded;
  /** What the format and the packages declared of their environments (`\gmenvironmentkind`): name -> EnvironmentKind bits. */
  std::unordered_map<std::string, std::uint8_t> kinds;
  /** The commands whose first optional argument is a colour model (`\gmcolourmodel`). */
  std::unordered_set<std::string> colourModel;
  /** What the format and the packages declared as data for the lowering (`\gmdeclare{table}{key}{value}`), by "table\x1fkey". */
  std::map<std::string, std::string> declared;
  /** The registers the format and the packages made (\newcount, \newdimen, \newskip, \newlength) and what they hold. */
  std::map<RegKey, Reg> regs;
  std::unordered_map<std::string, RegKey> regNames;
  int nextRegister = 1000;
  /** What the start packages left behind that is not a definition
   *  (\definecolor, ...): every input reads it first. */
  std::string startOutput;
  /** What went wrong reading them, one line each: a name defined twice, a
   *  command the expander could not read. Empty when all is well. */
  std::vector<std::string> problems;
};

/** The package a definition belongs to, for the format itself. */
constexpr const char* kFormat = "LaTeX";

}  // namespace

struct Expander::Impl {
  /** The format, the packages it loads, and then the packages `start` names
   *  (latex_options(packages = )), made by expanding their LaTeX once for
   *  each list and generation of the files (packages.h). */
  static const Definitions& snapshot(const std::vector<std::string>& start = {}) {
    static std::map<std::string, std::unique_ptr<Definitions>> made;
    static std::uint64_t built = 0;
    // Only ever between parses: a parse holds on to the one it started from.
    if (built != packageGeneration()) {
      made.clear();
      built = packageGeneration();
    }
    std::string key;
    for (const std::string& name : start) key += name + "\n";
    std::unique_ptr<Definitions>& slot = made[key];
    if (slot == nullptr) slot = makeSnapshot(start);
    return *slot;
  }

  static std::unique_ptr<Definitions> makeSnapshot(const std::vector<std::string>& start) {
    Diagnostics found;
    ExpanderOptions o;
    o.persistent = false;
    // A bad definition is a problem to report, not an exception.
    o.recover = true;
    Impl impl(preludeText(), std::move(o), found);
    impl.currentPackage = kFormat;
    impl.run();
    auto fresh = std::make_unique<Definitions>();
    // Each one the input does not have to ask for; what they leave behind,
    // such as a \definecolor, is read first by every input.
    for (const std::string& name : start) {
      impl.requested.insert(name);
      fresh->startOutput += impl.loadPackage(name, {});
    }
    fresh->startOutput = trim(fresh->startOutput);
    fresh->macros = std::move(impl.macros);
    fresh->envs = std::move(impl.envs);
    fresh->loaded = std::move(impl.loaded);
    fresh->kinds = std::move(impl.kinds);
    fresh->colourModel = std::move(impl.colourModel);
    fresh->declared = std::move(impl.declared);
    fresh->regs = std::move(impl.regs);
    fresh->regNames = std::move(impl.regNames);
    fresh->nextRegister = impl.nextRegister;
    for (const Diagnostic& d : found.items()) {
      fresh->problems.push_back(std::to_string(d.span.line) + ":" + std::to_string(d.span.col) + ": " +
                                d.message);
    }
    return fresh;
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
  /** The package whose file is being read, which the definitions it makes
   *  belong to (MacroDef::package); empty for the input's own. */
  std::string currentPackage;
  /** The packages read into these tables, as against the shared ones. */
  std::unordered_set<std::string> loaded;
  /** The packages the input asked for (\usepackage, \RequirePackage), those
   *  already loaded included: their names are no longer free to define. */
  std::unordered_set<std::string> requested;
  std::size_t expansions = 0;
  std::size_t expandedBytes = 0;
  /** How deep readArg() is in commands given as arguments with their own. */
  int argDepth = 0;
  int atLetter = 0;
  /** Units a document made with \DeclareSIUnit. */
  siunitx::UserUnits siUnits;
  /** `\sisetup{...}`, as written: the options every later number is read with. */
  std::string siSetup;
  // Set after runaway expansion in recover mode: the input ends there.
  bool halted = false;
  /** What a package declared with `\gmenvironmentkind` while it was read into these tables, as against the shared ones. */
  std::unordered_map<std::string, std::uint8_t> kinds;
  std::uint8_t kindsOf(const std::string& name) const {
    std::uint8_t found = 0;
    const auto own = kinds.find(name);
    if (own != kinds.end()) found |= own->second;
    if (shared != nullptr) {
      const auto theirs = shared->kinds.find(name);
      if (theirs != shared->kinds.end()) found |= theirs->second;
    }
    return found;
  }
  bool isKeyEnvironment(const std::string& name) const { return (kindsOf(name) & EnvironmentKind::keyValue) != 0; }
  /** What a package declared with `\gmcolourmodel`, here and in the shared tables. */
  std::unordered_set<std::string> colourModel;
  /** What was declared with `\gmdeclare` while these tables were made, as against the shared ones. */
  std::map<std::string, std::string> declared;
  // The arguments of an environment whose arguments are key=value data (EnvironmentKind::keyValue), while they pass: 0 not in them,
  // 1 after `\begin{name}`, 2 in a `[...]`, 3 in a `{...}`. `keyArgsAt` is the group depth they opened at and
  // `keyArgsLeft` how many groups may still come.
  int keyArgs = 0;
  int keyArgsAt = 0;
  int keyArgsLeft = 0;
  // Between `\foreach` and the `in` after it: the names it loops over are its own, even where the
  // format has a macro of that name (`\i`, `\j`), as pgf takes them.
  bool inForeach = false;
  std::unordered_set<std::string> foreachNames;  // the names some \foreach loops over, read as written from then on
  // `\gmprepend{environment}{text}` so far: what the environment's body begins with, as the package that made the
  // statements says (tikz's `\tikzset`, tikz-cd's `\tikzcdset`).
  std::unordered_map<std::string, std::string> prepended;
  // fancyvrb's \DefineVerbatimEnvironment{name}{Verbatim}{options}: name -> (the environment it is, its options).
  std::unordered_map<std::string, std::pair<std::string, std::string>> verbatimEnvironments;
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
    // What the text this is read from had defined: its own now, over define_macro()'s.
    if (opts.inherit != nullptr) {
      for (const auto& [name, def] : opts.inherit->macros) macros[name] = def;
      for (const auto& [name, def] : opts.inherit->envs) envs[name] = def;
    }
    if (opts.prelude) {
      shared = &snapshot(opts.packages);
      regs = shared->regs;
      regNames = shared->regNames;
      nextRegister = shared->nextRegister;
      // Asked for, in effect, by whoever set them: no longer free to define.
      for (const std::string& name : opts.packages) requested.insert(name);
      if (!shared->startOutput.empty()) pushExpansion(shared->startOutput, {}, true);
    }
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
      if (!f.root && !f.keepSpan) e.tok.span = f.origin;
      e.atLetter = f.atLetter;
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
      if (def->alias || !def->delimiters.empty() || !def->xargs.empty()) return;
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

  /** Is the byte part of a control word's name when text is read with
   *  `atLetter` (`@` a letter) or not? */
  bool nameByte(unsigned char b, bool atLetter) const {
    return b < 0x80 && (cats.get(b) == Cat::letter || (atLetter && b == '@'));
  }

  /** Does `s` end in a control word: letters after a backslash that is not
   *  itself escaped (`\\ab` is a control symbol and two letters)? */
  bool endsInControlWord(const std::string& s, bool atLetter) const {
    std::size_t i = s.size();
    while (i > 0 && nameByte(static_cast<unsigned char>(s[i - 1]), atLetter)) i--;
    if (i == s.size()) return false;
    std::size_t slashes = 0;
    while (slashes < i && s[i - 1 - slashes] == '\\') slashes++;
    return slashes % 2 == 1;
  }

  /** A body with its parameters replaced. TeX never pastes tokens, so a
   *  letter that falls straight after a control word -- the argument's, or
   *  the body's own after an argument -- is kept apart from it by a space:
   *  read again as text, `\alpha` and `b` would be the one name `\alphab`. */
  std::string substitute(const std::vector<BodyPiece>& pieces,
                         const std::vector<std::string>& args, bool atLetter = false) const {
    std::string s;
    const auto append = [&](const std::string& text) {
      if (text.empty()) return;
      if (!s.empty() && nameByte(static_cast<unsigned char>(text[0]), atLetter) &&
          endsInControlWord(s, atLetter)) {
        s += ' ';
      }
      s += text;
    };
    for (const auto& p : pieces) {
      if (p.param == 0) {
        append(p.text);
      } else if (static_cast<std::size_t>(p.param) <= args.size()) {
        append(args[static_cast<std::size_t>(p.param) - 1]);
      }
    }
    return s;
  }

  // --- arguments of a use ---------------------------------------------------

  std::vector<std::string> readArgs(const MacroDef& def, const std::string& who) {
    if (!def.xargs.empty()) {
      std::vector<std::string> given;
      for (const XArg& a : def.xargs) given.push_back(processed(a, readXArg(a, who), who));
      return given;
    }
    std::vector<std::string> args(static_cast<std::size_t>(def.nparams));
    if (!def.delimiters.empty()) return readDelimitedArgs(def, who);
    int i = 0;
    if (def.hasOptional && def.nparams > 0) {
      const auto o = readOptional();
      args[0] = o ? *o : def.optionalDefault;
      i = 1;
    }
    // An undelimited argument is one token in TeX, and that is what a package or the format is written for
    // (`\AddEnumerateCounter\arabic\@arabic{0}` in enumitem); only the input's own macros keep the lenient reading.
    const bool greedy = def.package.empty();
    for (; i < def.nparams; i++) args[static_cast<std::size_t>(i)] = readArg(who, greedy);
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
      args[static_cast<std::size_t>(i) - 1] = delim.empty() ? readArg(who, def.package.empty()) : readUntil(delim, who);
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
    if (t.tok.kind == TokKind::character && t.tok.cp == 0xE000) {  // a backslash \string made
      emitText("\\textbackslash{}", false);
      return;
    }
    emitText(t.text, t.tok.kind == TokKind::controlWord);
  }

  // --- expansion --------------------------------------------------------

  /** `atLetter`: the text was written with `@` a letter (MacroDef::atLetter).
   *  `keepSpan`: it is a package's file (Frame::keepSpan). */
  void pushExpansion(std::string text, const SourceSpan& at, bool atLetter = false,
                     bool keepSpan = false) {
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
    lo.atLetter = atLetter;
    // `at` is already an input position: a token read from an expansion
    // carries the origin of that expansion.
    frames.push_back({buffers.back(), std::make_unique<Lexer>(buffers.back(), lo, quiet, cats), false, at, {},
                      keepSpan, atLetter});
  }

  /** Is `@` a letter now: is what is being defined written under \makeatletter? */
  bool atLetterNow() const { return cats.get('@') == Cat::letter; }

  /** Make `name` mean `def` from here on. It belongs to the package being
   *  read, if one is; local to the group it is made in unless `local` is
   *  false (\gdef, \global). */
  void store(const std::string& name, MacroDef def, bool environment = false, bool local = true) {
    def.package = currentPackage;
    if (local) defineLocally(name, environment);
    (environment ? envs : macros)[name] = std::move(def);
  }

  /** Is a name that `package` defined free for the input to define? It is
   *  when LaTeX would not have it either: the package is not the format
   *  itself, and the input did not ask for it. */
  bool freelyDefinable(const std::string& package) const {
    return !package.empty() && package != kFormat && requested.count(package) == 0;
  }

  /** A name of a package that the input did not ask for: it may define the
   *  name itself, with no complaint that it exists. */
  bool isSoft(const std::string& name) const {
    const MacroDef* def = lookup(name);
    return def != nullptr && freelyDefinable(def->package);
  }

  /** The same for an environment. */
  bool isSoftEnv(const std::string& name) const {
    const MacroDef* def = lookupEnv(name);
    return def != nullptr && freelyDefinable(def->package);
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
    }
    if (def == nullptr) return false;

    // In the arguments of an environment that reads key=value data itself (tabularray's `[outer]{inner}`), TeX
    // gathers the text unexpanded and the package expands what it uses. A macro that takes an argument and is not
    // given a braced one there (`cmd=\underline, fg=red`) is a command the package applies later: left as written.
    if (keyArgs >= 2 && def->nparams > 0 && (def->delimiters.empty() || def->delimiters[0].empty())) {
      ExpToken next = nextNonSpace();
      const bool called = next.tok.isChar(Cat::beginGroup);
      unread(std::move(next));
      if (!called) {
        ExpToken keep = e;
        keep.tok.noexpand = true;
        unread(std::move(keep));
        return true;
      }
    }

    const std::string who = "\\" + name;
    // The macro's text, and what it was given, are read so wherever either was written.
    const bool atLetterText = def->atLetter || e.atLetter;
    if (def->alias) {
      ExpToken target = aliasToken(def->body.front().text, e.tok.span, atLetterText);
      // `\let\mydef\def` makes \mydef the command \def is: one of the
      // expander's own runs here, whatever the name means by now.
      if (target.tok.kind == TokKind::controlWord) {
        ExpToken run = target;
        run.tok.noexpand = false;
        if (const auto done = primitive(run, true); done && *done) return true;
      }
      unread(std::move(target));
      return true;
    }
    std::vector<std::string> args = readArgs(*def, who);
    if (abandoned) {
      abandoned = false;
      return true;
    }
    pushExpansion(substitute(def->body, args, atLetterText), e.tok.span, atLetterText);
    return true;
  }

  /** The one token a \let alias stands for, marked so that it is never
   *  expanded again: it means what its name meant when \let ran. */
  ExpToken aliasToken(const std::string& target, const SourceSpan& at, bool atLetter = false) {
    buffers.push_back(target);
    const std::string& buf = buffers.back();
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    lo.atLetter = atLetter;
    Lexer lx(buf, lo, quiet, cats);
    ExpToken t;
    t.tok = lx.next();
    t.text = std::string_view(buf).substr(t.tok.span.offset, t.tok.span.length);
    t.tok.span = at;
    t.tok.noexpand = true;
    t.atLetter = atLetter;
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
      if (begin && isKeyEnvironment(name)) {
        keyArgs = 4;  // `\begin{name}` passes first
        keyArgsAt = depth;
        keyArgsLeft = 2;
      }
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
      if (opts.recover) openEnvs.push_back({name, e.tok.span, depth - leftOpen});
      const bool atLetterText = def->atLetter || e.atLetter;
      pushExpansion("{" + substitute(def->body, args, atLetterText), e.tok.span, atLetterText);
      ExpToken open = rawUntaped();
      if (!def->package.empty()) {  // a package's or the format's, not the input's
        const std::uint8_t kind = kindsOf(name);
        const bool transparent =
          (def->body.empty() && def->endBody.empty()) || (kind & (EnvironmentKind::block | EnvironmentKind::floating)) != 0;
        open.tok.environment = !transparent                              ? ((kind & EnvironmentKind::display) != 0 ? 3 : 1)
                               : (kind & EnvironmentKind::floating) != 0 ? 4
                               : (kind & EnvironmentKind::center) != 0   ? 5
                               : (kind & EnvironmentKind::flushLeft) != 0 ? 6
                               : (kind & EnvironmentKind::flushRight) != 0 ? 7
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
    const MacroDef* def = lookupEnv(name);
    pushExpansion(substitute(def->endBody, {}, def->atLetter) + "}", at, def->atLetter);
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

  // --- registers: the state (the reading and the commands are below, by \ifnum) ---------------------------------

  /** What a register holds (RegKey: kind and number), and the names \newcount, \newdimen, \newskip and \countdef gave. A
   *  register is local to the group that sets it, as in TeX, unless \global says otherwise. */
  std::map<RegKey, Reg> regs;
  std::unordered_map<std::string, RegKey> regNames;
  int nextRegister = 1000;
  struct RegUndo {
    RegKey key;
    bool had;
    Reg old;
    int depth;
  };
  std::vector<RegUndo> regUndo;
  /** While a number or a length is being read, a register named in it is an operand and never the start of an assignment. */
  int scanDepth = 0;
  /** What \aftergroup saved, with the depth of the group it was said in. */
  std::vector<std::pair<ExpToken, int>> afterGroup;
  /** \right has been read and its delimiter has not. */
  bool rightPending = false;
  /** How many \left groups are open: they are groups for \aftergroup and assignments, but not braces an environment's end can count. */
  int leftOpen = 0;
  void releaseAfterGroup() {
    for (auto it = afterGroup.rbegin(); it != afterGroup.rend() && it->second > depth;) {
      unread(std::move(it->first));
      it = decltype(it)(afterGroup.erase(std::next(it).base()));
    }
  }
  /** While the test of \ifnum, \ifdim or \ifodd is read, and the number of conditionals open then (see conditional()). */
  bool testing_ = false;
  std::size_t testDepth = 0;
  /** Inside an \edef's body (expandBody): only what TeX expands is expanded; a command that does something is kept as it is. */
  int expandOnly = 0;
  /** Set while the digits of a factor are read, so that `2\gap` reaches the reader as a number and a register. */
  bool keepRegisterTokens = false;

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
    while (!regUndo.empty() && regUndo.back().depth > depth) {
      const RegUndo u = regUndo.back();
      regUndo.pop_back();
      if (u.key.first == 'k') {  // a \catcode
        cats.set(static_cast<unsigned char>(u.key.second), static_cast<Cat>(u.old.n));
      } else if (u.had) {
        regs[u.key] = u.old;
      } else {
        regs.erase(u.key);
      }
    }
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

  // --- packages -----------------------------------------------------------

  /** The state a package's file is read in, which reading it leaves again.
   *  It is a world of its own, as LaTeX reads a .sty: its definitions are
   *  global and carry its name, `@` is a letter, and what it leaves in the
   *  output is its own. */
  struct PackageWorld {
    Impl& s;
    std::vector<Undo> undo;
    std::vector<RegUndo> regUndoKept;
    int depth;
    std::string package;
    Cat at;
    std::string out;
    std::string carry;
    bool lastControlWord;
    std::vector<char> conds;

    PackageWorld(Impl& impl, const std::string& name)
        : s(impl),
          undo(std::move(impl.undoStack)),
          regUndoKept(std::move(impl.regUndo)),
          depth(impl.depth),
          package(impl.currentPackage),
          at(impl.cats.get('@')),
          out(std::move(impl.out)),
          carry(std::move(impl.carry)),
          lastControlWord(impl.lastControlWord),
          conds(std::move(impl.conds)) {
      impl.undoStack.clear();
      impl.regUndo.clear();
      impl.conds.clear();
      impl.depth = 0;
      impl.currentPackage = name;
      impl.cats.set('@', Cat::letter);
      impl.out.clear();
      impl.carry.clear();
      impl.lastControlWord = false;
    }

    ~PackageWorld() {
      s.undoStack = std::move(undo);
      s.regUndo = std::move(regUndoKept);
      s.depth = depth;
      s.currentPackage = package;
      s.cats.set('@', at);
      s.out = std::move(out);
      s.carry = std::move(carry);
      s.lastControlWord = lastControlWord;
      s.conds = std::move(conds);
    }
  };

  /** \usepackage[options]{a,b} and \RequirePackage[options]{a,b}[date]: each
   *  package we have is read, once; any other is ignored, as every
   *  \usepackage always was. */
  void usePackages(const ExpToken& e, bool require) {
    const std::string who = "\\" + e.tok.text;
    const std::optional<std::string> options = readOptional();
    const std::string list = readArg(who, false);
    if (require) readOptional();
    const std::vector<std::string> given = options ? splitList(*options) : std::vector<std::string>();
    std::size_t from = 0;
    while (from <= list.size()) {
      std::size_t comma = list.find(',', from);
      if (comma == std::string::npos) comma = list.size();
      const std::string name = trim(list.substr(from, comma - from));
      if (!name.empty()) {
        requested.insert(name);
        const std::string left = loadPackage(name, e.tok.span, given);
        if (currentPackage == kFormat && !trim(left).empty()) {
          // The format's own packages only define.
          diags.warn(e.tok.span, "package " + name + " left text behind: " + trim(left).substr(0, 30));
        } else if (!trim(left).empty()) {
          // What a package does besides define, a \definecolor say, is done
          // where it is asked for.
          pushExpansion(left, e.tok.span, true);
        }
      }
      from = comma + 1;
    }
  }

  /** Read the package `name` into these tables, once: not again, and not at
   *  all when the shared tables were made with it. Returns what it left
   *  behind that is not a definition. */
  std::string loadPackage(const std::string& name, const SourceSpan& at,
                          const std::vector<std::string>& given = {}) {
    if (loaded.count(name) != 0 || (shared != nullptr && shared->loaded.count(name) != 0)) return {};
    const std::string* source = packageSource(name);
    if (source == nullptr) return {};
    loaded.insert(name);
    return runPackage(name, *source, at, given);
  }

  /** Expand a package's file for what it defines, and return the text it
   *  leaves behind: the commands that are not definitions, which the parser
   *  reads. The problems it meets are reported as its own, at its lines. */
  std::string runPackage(const std::string& name, const std::string& source, const SourceSpan& at,
                         const std::vector<std::string>& given) {
    std::string leftover;
    bool endedInside = false;
    {
      const Diagnostics::Package report(diags, name, at);
      PackageWorld world(*this, name);
      const std::size_t base = frames.size();
      // Its options, as \DeclareOption and \ProcessOptions see them; gone with it.
      packageStack.emplace_back();
      packageStack.back().name = name;
      packageStack.back().given = given;
      packageStack.back().frame = base;
      struct Pop {
        std::vector<PackageOptions>& stack;
        ~Pop() { stack.pop_back(); }
      } pop{packageStack};
      // No space after the end: it would be owed to the token that follows.
      pushExpansion(source + "\n\\gmendpackage", at, true, true);
      int braces = 0;
      while (true) {
        ExpToken t = nextExpanded();
        if (t.tok.kind == TokKind::end) break;
        if (frames.size() <= base) {
          // The file ended inside something it began: the input is next.
          unread(std::move(t));
          endedInside = true;
          break;
        }
        if (t.tok.kind == TokKind::controlWord && t.tok.text == "gmendpackage") break;
        if (t.tok.isChar(Cat::beginGroup)) braces++;
        if (t.tok.isChar(Cat::endGroup)) braces--;
        if (t.tok.kind == TokKind::space) {
          // Between commands, a space or a line end is the file's layout; in
          // an argument it is text, and one space is all it is.
          if (braces > 0) emitText(" ", false);
          continue;
        }
        emitText(t.text, t.tok.kind == TokKind::controlWord);
      }
      leftover = std::move(out);
      // What it asked to have run at the start of the document, run now.
      leftover += packageStack.back().atBegin;
    }
    if (endedInside) diags.warn(at, "package " + name + " ends inside a group or a definition");
    return leftover;
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
    def.atLetter = atLetterNow();
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
    if (!(kind == "providecommand" && exists)) store(name, std::move(def));
  }

  /** \MakeUppercase and \MakeLowercase: the argument, expanded, with its letters changed. What LaTeX leaves alone is left
   *  alone: control words, math, and the arguments of \label, \ref, \begin and the like (their text is a name). */
  void changeCase(const ExpToken& e) {
    const bool upper = e.tok.text == "MakeUppercase";
    pushExpansion(caseChanged(expandBody(readArg("\\" + e.tok.text), e.tok.span), upper), e.tok.span);
  }

  static std::string caseChanged(const std::string& s, bool upper) {
    // Letters that LaTeX changes as control words (\ae and \AE, \ss and \SS).
    // (the upper-case form, the lower-case form): \i and \j are the dotless letters, which change to I and J.
    static const std::unordered_map<std::string, std::pair<std::string, std::string>> pairs = {
      {"ae", {"\\AE", "\\ae"}}, {"AE", {"\\AE", "\\ae"}}, {"oe", {"\\OE", "\\oe"}}, {"OE", {"\\OE", "\\oe"}},
      {"o", {"\\O", "\\o"}},    {"O", {"\\O", "\\o"}},    {"l", {"\\L", "\\l"}},    {"L", {"\\L", "\\l"}},
      {"ss", {"\\SS", "\\ss"}}, {"SS", {"\\SS", "\\ss"}}, {"i", {"I", "\\i"}},      {"j", {"J", "\\j"}}};
    static const std::unordered_set<std::string> names = {"label", "ref", "eqref", "pageref", "cite", "citep", "citet",
                                                          "cref", "Cref", "autoref", "begin", "end", "ensuremath",
                                                          "url", "href", "includegraphics", "gmdeclare"};
    const auto letter = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    std::string out;
    for (std::size_t i = 0; i < s.size();) {
      const char c = s[i];
      if (c == '$') {
        // Math is not text: from this $ to the one that closes it (or $$ to $$).
        const std::size_t length = i + 1 < s.size() && s[i + 1] == '$' ? 2 : 1;
        const std::size_t close = s.find(length == 2 ? "$$" : "$", i + length);
        const std::size_t end = close == std::string::npos ? s.size() : close + length;
        out += s.substr(i, end - i);
        i = end;
      } else if (c == '\\' && i + 1 < s.size() && letter(s[i + 1])) {
        std::size_t j = i + 1;
        while (j < s.size() && letter(s[j])) j++;
        const std::string name = s.substr(i + 1, j - i - 1);
        const auto pair = pairs.find(name);
        if (pair != pairs.end()) {
          out += upper ? pair->second.first : pair->second.second;
        } else {
          out += s.substr(i, j - i);
          if (names.count(name) != 0) {
            // its arguments, `[..]` and `{..}`, as written
            while (j < s.size() && (s[j] == '[' || s[j] == '{')) {
              const char open = s[j], close = open == '[' ? ']' : '}';
              int depth = 0;
              std::size_t k = j;
              for (; k < s.size(); k++) {
                if (s[k] == open) depth++;
                if (s[k] == close && --depth == 0) break;
              }
              k = std::min(k + 1, s.size());
              out += s.substr(j, k - j);
              j = k;
            }
          }
        }
        i = j;
      } else if (c == '\\') {
        // A control symbol, and \( ... \) with its math.
        if (i + 1 < s.size() && s[i + 1] == '(') {
          const std::size_t close = s.find("\\)", i + 2);
          const std::size_t end = close == std::string::npos ? s.size() : close + 2;
          out += s.substr(i, end - i);
          i = end;
        } else {
          out += s.substr(i, std::min<std::size_t>(2, s.size() - i));
          i += 2;
        }
      } else if (static_cast<unsigned char>(c) == 0xC3 && i + 1 < s.size()) {
        // Latin-1 letters in UTF-8 (U+00C0 to U+00FE, but not x and the division sign): 0xC3 0x80-0x9E upper, 0xA0-0xBE lower.
        unsigned char next = static_cast<unsigned char>(s[i + 1]);
        if (upper && next == 0x9F) {  // the sharp s is SS in capitals
          out += "SS";
          i += 2;
          continue;
        }
        const bool isUpper = next >= 0x80 && next <= 0x9E && next != 0x97;
        const bool isLower = next >= 0xA0 && next <= 0xBE && next != 0xB7;
        if (upper && isLower) next = static_cast<unsigned char>(next - 0x20);
        if (!upper && isUpper) next = static_cast<unsigned char>(next + 0x20);
        out += c;
        out += static_cast<char>(next);
        i += 2;
      } else {
        out += static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
        i++;
      }
    }
    return out;
  }

  /** fancyvrb's \DefineVerbatimEnvironment{name}{Verbatim}{options} (and \RecustomVerbatimEnvironment): `name` is that
   *  environment with those options from here on, which the parser reads (Expander::verbatimEnvironment()). Pandoc's
   *  Highlighting is one. Only the environments of this engine's own can be made over. */
  void defineVerbatimEnvironment(const ExpToken& e) {
    const std::string who = "\\" + e.tok.text;
    const std::string name = trim(readArg(who));
    const std::string base = trim(readArg(who));
    const std::string options = readArg(who);
    if (name.empty()) return;
    if (base != "Verbatim" && base != "Verbatim*" && base != "alltt") {
      diags.warn(e.tok.span, who + ": `" + base + "' is not an environment that can be made over (Verbatim is)");
      return;
    }
    verbatimEnvironments[name] = {base, options};
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

  /** `\gmenvironmentkind{kind}{a,b}`: what a package says of the environments it defines, which the engine cannot
   *  find out from their code (see EnvironmentKind). A package that does not say it is read as any other. */
  void declareEnvironmentKind(const ExpToken& e) {
    static const std::unordered_map<std::string, std::uint8_t> known = {
      {"keyvalue", EnvironmentKind::keyValue}, {"block", EnvironmentKind::block},
      {"float", EnvironmentKind::floating},    {"display", EnvironmentKind::display},
      {"numbered", EnvironmentKind::numbered}, {"center", EnvironmentKind::center},
      {"flushleft", EnvironmentKind::flushLeft}, {"flushright", EnvironmentKind::flushRight}};
    const auto kind = readGroupText();
    const auto names = readGroupText();
    if (!kind || !names) return;
    const auto it = known.find(trim(*kind));
    if (it == known.end()) {
      diags.warn(e.tok.span, "\\gmenvironmentkind: unknown kind '" + trim(*kind) + "'");
      return;
    }
    std::size_t from = 0;
    while (from <= names->size()) {
      const std::size_t comma = std::min(names->find(',', from), names->size());
      if (const std::string one = trim(names->substr(from, comma - from)); !one.empty()) kinds[one] |= it->second;
      from = comma + 1;
    }
  }

  /** The commands that take xcolor's optional colour model (`\textcolor[rgb]{.2,.4,.6}{x}`, colortbl's `\rowcolor[HTML]{EFEFEF}`). */
  bool isModelColourCommand(const std::string& n) const {
    return colourModel.count(n) != 0 || (shared != nullptr && shared->colourModel.count(n) != 0);
  }

  /** A colour written in xcolor's model as the `#RRGGBB` every colour argument reads. Anything else is `spec` as it was. */
  static std::string colourInModel(const std::string& model, const std::string& spec, std::string& problem) {
    const std::string m = trim(model);
    if (m == "named" || m.empty()) return spec;
    std::string text = trim(spec);
    if (m == "HTML") {
      if (text.size() != 6 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        problem = "HTML colour `" + text + "' is not six hexadecimal digits";
        return spec;
      }
      for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      return "#" + text;
    }
    std::vector<double> v;
    for (std::size_t from = 0; from <= text.size();) {
      const std::size_t cut = text.find_first_of(",:", from);
      const std::string part = trim(text.substr(from, cut == std::string::npos ? std::string::npos : cut - from));
      char* end = nullptr;
      const double x = std::strtod(part.c_str(), &end);
      if (part.empty() || end != part.c_str() + part.size()) {
        problem = "`" + text + "' is not a colour in the " + m + " model";
        return spec;
      }
      v.push_back(x);
      if (cut == std::string::npos) break;
      from = cut + 1;
    }
    double r = 0, g = 0, b = 0;
    if (m == "rgb" && v.size() == 3) {
      r = v[0], g = v[1], b = v[2];
    } else if (m == "RGB" && v.size() == 3) {
      r = v[0] / 255.0, g = v[1] / 255.0, b = v[2] / 255.0;
    } else if (m == "gray" && v.size() == 1) {
      r = g = b = v[0];
    } else if (m == "Gray" && v.size() == 1) {
      r = g = b = v[0] / 15.0;
    } else if (m == "cmyk" && v.size() == 4) {
      r = (1 - v[0]) * (1 - v[3]), g = (1 - v[1]) * (1 - v[3]), b = (1 - v[2]) * (1 - v[3]);
    } else {
      problem = (m == "rgb" || m == "RGB" || m == "gray" || m == "Gray" || m == "cmyk")
                  ? "`" + text + "' has the wrong number of components for the " + m + " model"
                  : "the colour model `" + m + "' is not known";
      return spec;
    }
    const auto byte = [](double x) { return static_cast<int>(std::lround(std::min(1.0, std::max(0.0, x)) * 255.0)); };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", byte(r), byte(g), byte(b));
    return buf;
  }

  /** `\textcolor[model]{spec}{text}` and kin: with a model, the colour is passed on as `#RRGGBB`; without, the command goes
   *  through as it is. */
  std::optional<bool> colourModelCommand(const ExpToken& e) {
    ExpToken t = nextNonSpace();
    const bool bracket = isChar(t, '[');
    unread(std::move(t));
    if (!bracket) return false;
    const std::string model = readOptional().value_or(std::string());
    const std::string name = e.tok.text, who = "\\" + name;
    std::string problem, out = who;
    for (int k = 0; k < (name == "fcolorbox" ? 2 : 1); k++) out += "{" + colourInModel(model, readArg(who), problem) + "}";
    if (!problem.empty()) diags.warn(e.tok.span, who + ": " + problem);
    pushExpansion(out, e.tok.span);
    return true;
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

  /** Every command the expander runs itself (primitive() below). Keep it in
   *  step: \ifx and \ifdefined take these names to mean something. */
  bool isPrimitiveName(const std::string& n) const {
    static const std::unordered_set<std::string> names = {
      "newcommand", "renewcommand", "providecommand", "DeclareRobustCommand",
      "ce", "pu", "MakeUppercase", "MakeLowercase", "newcount", "newdimen", "newskip", "newtoks", "countdef", "dimendef", "skipdef", "toksdef", "toks",
      "chardef", "mathchardef", "catcode", "advance", "multiply", "divide", "the", "number", "newlength", "setlength", "addtolength", "ifdim", "ifodd", "numexpr", "dimexpr", "string", "aftergroup",
      "gmprepend", "DefineVerbatimEnvironment", "RecustomVerbatimEnvironment",
      "def", "gdef", "edef", "xdef", "@ifstar", "@ifnextchar", "global",
      "expandafter", "noexpand", "ignorespaces", "gmenvironmentkind", "gmcolourmodel", "gmdeclare", "gmwarning", "settowidth", "settoheight", "settodepth", "settototalheight", "futurelet", "let", "newenvironment", "renewenvironment",
      "makeatletter", "makeatother", "begingroup", "endgroup", "begin", "end", "usepackage",
      "RequirePackage", "ProvidesPackage", "NeedsTeXFormat", "relax", "iftrue", "iffalse", "ifx", "if", "ifcat",
      "ifdefined", "ifcsname", "ifnum", "unless", "else", "fi", "csname", "newif",
      "DeclareOption", "ProcessOptions", "ExecuteOptions", "AtBeginDocument", "endinput",
      "@ifpackageloaded", "NewDocumentCommand", "RenewDocumentCommand", "ProvideDocumentCommand",
      "DeclareDocumentCommand", "NewDocumentEnvironment", "RenewDocumentEnvironment",
      "ProvideDocumentEnvironment", "DeclareDocumentEnvironment"};
    return names.count(n) != 0 || isSiunitx(n) || isModelColourCommand(n);
  }

  /** The conditionals: each reads a test and then one of two branches. */
  static bool isCondPrimitive(const std::string& n) {
    return n == "iftrue" || n == "iffalse" || n == "ifx" || n == "ifdefined" || n == "ifcsname" ||
           n == "ifnum" || n == "ifdim" || n == "ifodd" || n == "if" || n == "ifcat";
  }

  /** TeX's other conditionals, which this expander does not evaluate. They go
   *  through to the parser as they are, with their \else and \fi --
   *  kableExtra's \resizebox{\ifdim\width>\linewidth ...\else...\fi} is read
   *  there -- and count only as nesting where a branch is skipped. */
  static bool isForeignCond(const std::string& n) {
    static const std::unordered_set<std::string> names = {
      "ifvmode", "ifhmode", "ifmmode", "ifinner", "ifvoid",
      "ifhbox", "ifvbox", "ifeof", "ifcase"};
    return names.count(n) != 0;
  }

  /** What \expandafter expands, besides macros: the commands TeX itself
   *  expands rather than runs. */
  static bool isExpandablePrimitive(const std::string& n) {
    return isCondPrimitive(n) || n == "unless" || n == "else" || n == "fi" || n == "csname" ||
           n == "@ifstar" || n == "@ifnextchar" || n == "@ifpackageloaded" || n == "the" || n == "number" || n == "string";
  }

  /** A subset of siunitx (front/siunitx.h): the command's arguments are
   *  read, formatted, and what they make is read in their place. Its
   *  options are read and not used. */
  void siunitxCommand(const ExpToken& e) {
    const std::string name = e.tok.text;
    const SourceSpan at = e.tok.span;
    const auto given = readOptional();
    siunitx::Options siOptions = siunitx::parseOptions(siSetup);
    if (given) siOptions = siunitx::parseOptions(*given, siOptions);
    std::string problem;
    const auto say = [&] {
      if (!problem.empty()) diags.warn(at, "\\" + name + ": " + problem);
      problem.clear();
    };
    const std::string who = "\\" + name;
    if (name == "sisetup") {
      siSetup += "," + readArg(who);
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
      const std::string out = siunitx::number(s, problem, siOptions);
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
    def.atLetter = atLetterNow();
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
    store(name, std::move(def), false, e.tok.text != "gdef" && e.tok.text != "xdef" && !global);
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
      def.atLetter = atLetterNow();
      def.body = literal(std::string(t.text));
    }
    store(name, std::move(def), false, !global);
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
    // The kernel (latex.ltx, \@ifnextchar) stores both branches with `\def\reserved@a{#2}\def\reserved@b{#3}`, so each `##` in
    // them is one `#` when the branch runs: how a macro made inside a branch gets its `#1` (enumitem, soul).
    const std::string yes = defStored(readArg(who, false)), no = defStored(readArg(who, false));
    ExpToken next = nextNonSpace();
    const bool match = star ? isChar(next, '*') : (next.tok.kind == test.tok.kind && next.text == test.text);
    if (!(match && star)) unread(std::move(next));
    pushExpansion(match ? yes : no, e.tok.span, e.atLetter);
  }

  /** A token list as `\def` stores it: each `##` is one `#`. */
  static std::string defStored(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); i++) {
      out += text[i];
      if (text[i] == '#' && i + 1 < text.size() && text[i + 1] == '#') i++;
    }
    return out;
  }

  /** \expandafter\a\b: `\b` is expanded once, then `\a` is read before what it made. */
  void expandAfter() {
    ExpToken first = raw();
    ExpToken second = raw();
    if (second.tok.kind == TokKind::controlWord && second.tok.text == "expandafter" && !second.tok.noexpand &&
        !macros.count("expandafter")) {
      expandAfter();
    } else if (second.tok.kind == TokKind::end || !second.tok.isControl() || second.tok.noexpand ||
               !expandOnce(second)) {
      unread(std::move(second));
    }
    unread(std::move(first));
  }

  /** Expand the token `t` once, if TeX would: a macro, or one of the commands
   *  that expand (a conditional, \csname, ...). */
  bool expandOnce(const ExpToken& t) {
    if (expandMacro(t)) return true;
    if (t.tok.kind != TokKind::controlWord || !isExpandablePrimitive(t.tok.text) ||
        lookup(t.tok.text) != nullptr) {
      return false;
    }
    const auto done = primitive(t, false);
    return done && *done;
  }

  // --- conditionals and \csname -------------------------------------------

  /** The conditionals begun and not yet at their \fi: 't' while the branch
   *  read is the true one, 'e' while it is the else one. */
  std::vector<char> conds;

  /** The primitive a name stands for, through \let copies of it; empty when
   *  it is a macro or anything else. */
  std::string primitiveOf(std::string name) const {
    for (int i = 0; i < 32; i++) {
      const MacroDef* def = lookup(name);
      if (def == nullptr) return isPrimitiveName(name) ? name : std::string();
      if (!def->alias) return {};
      const std::string& target = def->body.front().text;
      if (target.size() < 2 || target[0] != '\\') return {};
      name = target.substr(1);
    }
    return {};
  }

  /** What a macro is, as one string: equal macros give equal strings. */
  static std::string describe(const MacroDef& def) {
    std::string s = std::to_string(def.nparams) + (def.hasOptional ? "[" + def.optionalDefault + "]" : "");
    for (const auto& delimiter : def.delimiters) {
      s += "|";
      for (const Token& t : delimiter) s += t.text + "\x1f";
    }
    for (const XArg& a : def.xargs) s += "<" + std::string(1, a.type) + a.open.text + a.close.text + a.deflt;
    s += "=";
    for (const BodyPiece& p : def.body) s += p.param == 0 ? "t" + p.text + "\x1e" : "p" + std::to_string(p.param) + "\x1e";
    return s;
  }

  /** What the control sequence `name` means, as one string for \ifx: equal
   *  meanings give equal strings. */
  std::string meaningOf(const std::string& name, int depth = 0) const {
    if (depth > 32) return "loop";
    if (const MacroDef* def = lookup(name)) {
      if (!def->alias) return "macro:" + describe(*def);
      const std::string& target = def->body.front().text;
      if (target.size() >= 2 && target[0] == '\\') return meaningOf(target.substr(1), depth + 1);
      return "char:" + target;
    }
    if (isPrimitiveName(name)) return "primitive:" + name;
    if (regNames.count(name) != 0) return "register:" + name;
    if ((opts.isBuiltinCommand && opts.isBuiltinCommand(name)) || findCommand(name) != nullptr ||
        (opts.isKnownName && opts.isKnownName(name))) {
      return "command:" + name;
    }
    return "undefined";
  }

  std::string meaningOf(const ExpToken& t) const {
    if (t.tok.isControl()) return meaningOf(t.tok.text);
    return t.tok.kind == TokKind::space ? "space" : "char:" + std::string(t.text);
  }

  /** \ifx: the next two tokens, not expanded, mean the same. */
  bool ifx() {
    const ExpToken a = raw();
    const ExpToken b = raw();
    return meaningOf(a) == meaningOf(b);
  }

  /** \if and \ifcat (tex.web 506-507): the next two tokens, expanded, have the same character code or the same category. A control
   *  sequence that does not expand is the code 256 and the category of \relax; a space is code 32 and category 10. */
  bool ifchar(bool byCategory) {
    const auto codeAndCat = [&]() {
      // \relax is a token here (nextExpanded would run it, and so drop it)
      ExpToken t = raw();
      if (!(t.tok.kind == TokKind::controlWord && t.tok.text == "relax" && lookup("relax") == nullptr)) {
        unread(std::move(t));
        t = nextExpanded();
      }
      if (t.tok.isControl() || t.tok.kind == TokKind::end) return std::pair<int, int>{256, 16};
      if (t.tok.kind == TokKind::space) return std::pair<int, int>{32, 10};
      return std::pair<int, int>{static_cast<int>(t.tok.cp), static_cast<int>(t.tok.cat)};
    };
    const auto a = codeAndCat();
    const auto b = codeAndCat();
    return byCategory ? a.second == b.second : a.first == b.first;
  }

  bool ifdefined() {
    const ExpToken t = raw();
    return !t.tok.isControl() || meaningOf(t.tok.text) != "undefined";
  }

  /** The name between \csname and \endcsname, read with expansion. */
  std::string readCsname(const SourceSpan& at) {
    std::string name;
    while (true) {
      ExpToken t = nextExpanded();
      if (t.tok.kind == TokKind::end) {
        diags.warn(at, "missing \\endcsname inserted");
        unread(std::move(t));
        return name;
      }
      if (t.tok.kind == TokKind::controlWord && t.tok.text == "endcsname") return name;
      if (t.tok.isControl()) {
        diags.warn(t.tok.span, "illegal token in \\csname: it is left out");
        continue;
      }
      std::string piece = t.tok.kind == TokKind::space ? std::string(" ") : std::string(t.text);
      for (std::size_t at = piece.find("\xEE\x80\x80"); at != std::string::npos; at = piece.find("\xEE\x80\x80", at)) {
        piece.replace(at, 3, "\\");  // what \string made of a backslash
        at++;
      }
      name += piece;
    }
  }

  /** The token for the control sequence `name`, which need not be one TeX
   *  could write (`my-cmd`). */
  ExpToken controlToken(const std::string& name, const SourceSpan& at, bool atLetter = false) {
    buffers.push_back("\\" + name);
    ExpToken t;
    t.atLetter = atLetter;  // (what its arguments say is read as the \csname was)
    const bool symbol = name.size() == 1 && cats.get(static_cast<unsigned char>(name[0])) != Cat::letter;
    t.tok.kind = symbol ? TokKind::controlSymbol : TokKind::controlWord;
    t.tok.text = name;
    t.tok.span = at;
    t.text = std::string_view(buffers.back());
    return t;
  }

  /** \csname name\endcsname: the control sequence of that name. One that
   *  means nothing is made to mean \relax, as in TeX. */
  void csname(const ExpToken& e) {
    const std::string name = readCsname(e.tok.span);
    if (name.empty()) {
      diags.warn(e.tok.span, "\\csname\\endcsname: the empty name is left out");
      return;
    }
    if (meaningOf(name) == "undefined") {
      MacroDef relax;
      relax.alias = true;
      relax.body = literal("\\relax");
      store(name, std::move(relax));
    }
    unread(controlToken(name, e.tok.span, e.atLetter));
  }

  bool ifcsname(const SourceSpan& at) { return meaningOf(readCsname(at)) != "undefined"; }

  // --- registers: \count \dimen \skip, \advance, \the, \ifnum \ifdim ----------------------------------------------------
  //
  // What TeX does (tex.web 410-479): a number is read with signs and digits (or `"`hex, `'`octal, `` `\c``), a length is a
  // decimal, a unit and one optional space, glue a length with `plus` and `minus` parts. A register token is an operand
  // where a number or length is wanted and the start of an assignment elsewhere (\x=5pt, \x 5pt). The sizes are `Dim`s
  // (registers.h): points, ems and exs kept apart, since the font is the render's. A register the expander cannot give
  // a value (it was set from a length of the lowering's, as \linewidth) is `unknown`: its uses stay the token, for the
  // lowering. The LaTeX commands over registers are \newlength, \setlength and \addtolength; the lowering still sets
  // the lengths of its own (\parindent, \tabcolsep) and is told when a register of ours is `unknown`.

  /** Keeps register tokens read while it lives as operands. */
  struct ScanGuard {
    Impl& s;
    explicit ScanGuard(Impl& impl) : s(impl) { s.scanDepth++; }
    ~ScanGuard() { s.scanDepth--; }
  };

  ExpToken nextNonSpaceExpanded() {
    ExpToken t = nextExpanded();
    while (t.tok.kind == TokKind::space) t = nextExpanded();
    return t;
  }

  /** What was read to find out that something is not there, put back in the order it came. */
  void giveBack(std::vector<ExpToken>& seen) {
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) unread(std::move(*it));
    seen.clear();
  }

  static bool isDigitOf(const ExpToken& t, int base, int& value) {
    if (t.tok.kind != TokKind::character) return false;
    const char32_t c = t.tok.cp;
    if (c >= '0' && c <= '9') value = static_cast<int>(c - '0');
    else if (base == 16 && c >= 'A' && c <= 'F') value = static_cast<int>(c - 'A') + 10;
    else return false;
    return value < base;
  }

  /** tex.web scan_int: an optionally signed number. False, with nothing read, when none is there. */
  bool tryInt(std::int64_t& value) {
    ScanGuard guard(*this);
    std::vector<ExpToken> seen;
    // (register names are kept here, not replaced by what they print: a length is an integer in scaled points, tex.web 413)
    struct Keep {
      bool& flag;
      ~Keep() { flag = false; }
    } keep{keepRegisterTokens};
    keepRegisterTokens = true;
    ExpToken t = nextNonSpaceExpanded();
    bool negative = false;
    while (isChar(t, '+') || isChar(t, '-')) {
      if (isChar(t, '-')) negative = !negative;
      seen.push_back(std::move(t));
      t = nextNonSpaceExpanded();
    }
    keepRegisterTokens = false;
    if (t.tok.kind == TokKind::controlWord && lookup(t.tok.text) == nullptr &&
        (regNames.count(t.tok.text) != 0 || registerKind(t.tok.text) != '\0')) {
      const auto key = registerOf(t);
      if (key && key->first != 't' && !regs[*key].unknown) {
        const std::int64_t n = key->first == 'c' ? regs[*key].n : regs[*key].g.natural.sp();
        value = negative ? -n : n;
        return true;
      }
      if (key) unread(std::move(t));
      giveBack(seen);
      return false;
    }
    if (t.tok.kind == TokKind::controlWord && t.tok.text == "catcode" && lookup("catcode") == nullptr) {
      // \catcode<number>: the category of that character
      std::int64_t code = 0;
      if (tryInt(code) && code >= 0 && code < 128) {
        value = static_cast<std::int64_t>(cats.get(static_cast<unsigned char>(code)));
        return true;
      }
      unread(std::move(t));
      giveBack(seen);
      return false;
    }
    if (t.tok.kind == TokKind::controlWord && t.tok.text == "numexpr" && lookup("numexpr") == nullptr) {
      Val v;
      if (exprValue('i', v)) {
        value = negative ? -v.n : v.n;
        return true;
      }
      unread(std::move(t));
      giveBack(seen);
      return false;
    }
    int base = 10;
    if (isChar(t, '"')) base = 16;
    if (isChar(t, '\'')) base = 8;
    std::int64_t v = 0;
    bool any = false;
    if (isChar(t, '`')) {
      // The character code of the next token: `a, `\a, `\%.
      seen.push_back(std::move(t));
      ExpToken c = raw();
      if (c.tok.kind == TokKind::end) {
        unread(std::move(c));
        giveBack(seen);
        return false;
      }
      v = c.tok.isControl() ? static_cast<std::int64_t>(static_cast<unsigned char>(c.tok.text.empty() ? ' ' : c.tok.text[0]))
                            : static_cast<std::int64_t>(c.tok.cp);
      if (c.tok.kind == TokKind::controlSymbol && c.tok.text == "^") {
        // `\^^A is the character ^^A, code 1: two carets and a character make that character plus or minus 64 (tex.web 355)
        ExpToken second = raw();
        if (second.tok.isChar(Cat::superscript)) {
          ExpToken third = raw();
          if (third.tok.kind == TokKind::character && third.tok.cp < 128) {
            const int x = static_cast<int>(third.tok.cp);
            v = x < 64 ? x + 64 : x - 64;
          } else {
            unread(std::move(third));
            unread(std::move(second));
          }
        } else {
          unread(std::move(second));
        }
      }
      any = true;
      t = nextExpanded();
    } else {
      if (base != 10) {
        seen.push_back(std::move(t));
        t = nextExpanded();
      }
      int d = 0;
      while (isDigitOf(t, base, d)) {
        v = std::min<std::int64_t>(v * base + d, 2147483647LL);
        any = true;
        t = nextExpanded();
      }
    }
    if (!any) {
      unread(std::move(t));
      giveBack(seen);
      return false;
    }
    if (t.tok.kind != TokKind::space) unread(std::move(t));
    value = negative ? -v : v;
    return true;
  }

  /** tex.web scan_keyword: the letters of `word`, either case, after any spaces. Nothing is read when it is not there. */
  bool keyword(const char* word) {
    std::vector<ExpToken> seen;
    ExpToken t = nextExpanded();
    while (t.tok.kind == TokKind::space) {
      seen.push_back(std::move(t));
      t = nextExpanded();
    }
    for (const char* w = word; *w != '\0'; w++) {
      const bool same = t.tok.kind == TokKind::character && t.tok.cp < 0x80 &&
                        std::tolower(static_cast<int>(t.tok.cp)) == *w;
      seen.push_back(std::move(t));
      if (!same) {
        // `seen` holds the token that did not match as its last: it goes back with the rest.
        giveBack(seen);
        return false;
      }
      if (*(w + 1) != '\0') t = nextExpanded();
    }
    return true;  // the matching tokens are used up
  }

  /** The unit that follows a number (`true` before it is ignored, as it is for a grob of one size); empty when none is there.
   *  With `allowFil`, `fil`, `fill` and `filll` are units too, and `order` says which. */
  std::string readUnit(bool allowFil, int& order) {
    order = 0;
    (void)keyword("true");
    if (allowFil && keyword("fil")) {
      order = 1;
      while (order < 3 && keyword("l")) order++;
      return "pt";
    }
    static const char* const kUnits[] = {"pt", "pc", "in", "bp", "cm", "mm", "dd", "cc", "sp", "em", "ex", "mu"};
    for (const char* u : kUnits) {
      if (keyword(u)) return u;
    }
    return {};
  }

  /** tex.web scan_dimen: an optionally signed decimal and a unit, then the one optional space. False, with nothing read,
   *  when there is none (a register of ours is read as the value it prints). An absolute unit is worked out as TeX does
   *  (sections 102 and 458), so that `1cm` is 28.45274pt to the last scaled point. */
  bool tryDim(Dim& value, bool allowFil = false, int* fil = nullptr) {
    ScanGuard guard(*this);
    std::vector<ExpToken> seen;
    ExpToken t = nextNonSpaceExpanded();
    bool negative = false;
    while (isChar(t, '+') || isChar(t, '-')) {
      if (isChar(t, '-')) negative = !negative;
      seen.push_back(std::move(t));
      t = nextNonSpaceExpanded();
    }
    if (t.tok.kind == TokKind::controlWord && t.tok.text == "dimexpr" && lookup("dimexpr") == nullptr) {
      Val v;
      if (exprValue('d', v)) {
        value = negative ? -v.d : v.d;
        return true;
      }
      unread(std::move(t));
      giveBack(seen);
      return false;
    }
    std::int64_t whole = 0;
    std::vector<int> digits;
    bool any = false;
    int d = 0;
    struct Keep {
      bool& flag;
      ~Keep() { flag = false; }
    } keep{keepRegisterTokens};
    while (isDigitOf(t, 10, d)) {
      whole = std::min<std::int64_t>(whole * 10 + d, 1073741823LL);
      any = true;
      keepRegisterTokens = true;
      seen.push_back(std::move(t));
      t = nextExpanded();
    }
    if (isChar(t, '.') || isChar(t, ',')) {
      seen.push_back(std::move(t));
      t = nextExpanded();
      while (isDigitOf(t, 10, d)) {
        if (digits.size() < 17) digits.push_back(d);
        any = true;
        keepRegisterTokens = true;
        seen.push_back(std::move(t));
        t = nextExpanded();
      }
    }
    keepRegisterTokens = true;
    unread(std::move(t));
    if (!any) {
      giveBack(seen);
      return false;
    }
    // the fraction as sixteen bits (tex.web 102, round_decimals)
    std::int64_t fraction = 0;
    {
      std::int64_t a = 0;
      for (std::size_t k = digits.size(); k > 0;) {
        --k;
        a = (a + digits[k] * 131072LL) / 10;
      }
      fraction = (a + 1) / 2;
    }
    const double factor = static_cast<double>(whole) + static_cast<double>(fraction) / 65536.0;
    {
      // a register after the factor is the factor times it: `2\gap`
      ExpToken r = raw();
      if (r.tok.kind == TokKind::controlWord && lookup(r.tok.text) == nullptr &&
          (regNames.count(r.tok.text) != 0 || registerKind(r.tok.text) != '\0')) {
        const auto key = registerOf(r);
        if (key && key->first != 'c' && !regs[*key].unknown) {
          value = regs[*key].g.natural.scaled(negative ? -factor : factor);
          return true;
        }
        diags.warn(r.tok.span, "a register after a number must be a length");
      }
      unread(std::move(r));
    }
    keepRegisterTokens = false;
    int order = 0;
    const std::string unit = readUnit(allowFil, order);
    if (unit.empty()) {
      giveBack(seen);
      return false;
    }
    if (fil != nullptr) *fil = order;
    value = Dim();
    if (unit == "em" || unit == "mu") {
      value.em = (negative ? -factor : factor) / (unit == "mu" ? 18.0 : 1.0);
    } else if (unit == "ex") {
      value.ex = negative ? -factor : factor;
    } else {
      std::int64_t sp = 0;
      if (unit == "sp") {
        sp = whole;
      } else {
        // num/denom of each unit in points (tex.web 458)
        std::int64_t num = 1, denom = 1;
        if (unit == "pc") num = 12;
        else if (unit == "in") num = 7227, denom = 100;
        else if (unit == "bp") num = 7227, denom = 7200;
        else if (unit == "cm") num = 7227, denom = 254;
        else if (unit == "mm") num = 7227, denom = 2540;
        else if (unit == "dd") num = 1238, denom = 1157;
        else if (unit == "cc") num = 14856, denom = 1157;
        std::int64_t ip = whole * num / denom;
        const std::int64_t rem = whole * num % denom;
        std::int64_t f = (num * fraction + 65536 * rem) / denom;
        ip += f / 65536;
        f %= 65536;
        sp = ip * 65536 + f;
      }
      value.pt = static_cast<double>(negative ? -sp : sp) / 65536.0;
    }
    // the one optional space after a unit
    ExpToken sp = raw();
    if (sp.tok.kind != TokKind::space) unread(std::move(sp));
    return true;
  }

  bool tryGlue(Glue& g) {
    ScanGuard guard(*this);
    if (!tryDim(g.natural)) return false;
    g.stretch = g.shrink = Dim();
    g.stretchOrder = g.shrinkOrder = 0;
    if (keyword("plus")) {
      if (!tryDim(g.stretch, true, &g.stretchOrder)) diags.warn({}, "missing number after plus, treated as zero");
    }
    if (keyword("minus")) {
      if (!tryDim(g.shrink, true, &g.shrinkOrder)) diags.warn({}, "missing number after minus, treated as zero");
    }
    return true;
  }

  /** An integer as TeX reads one: signs, digits, and the one space after. */
  long readNumber(const SourceSpan& at) {
    std::int64_t v = 0;
    if (!tryInt(v)) diags.warn(at, "missing number, treated as zero");
    return static_cast<long>(v);
  }

  // --- \numexpr and \dimexpr (e-TeX) ---------------------------------------------------------------------------------
  //
  // An expression is sums and differences of terms, a term is factors joined by `*` and `/`, a factor is a number (a length for
  // \dimexpr), a register or an expression in parentheses; the right-hand side of `*` and `/` is always an integer. Division rounds
  // to the nearest, halves away from zero, and `a*b/c` is worked out in one step so that nothing is lost between the two. A
  // trailing \relax belongs to the expression.

  struct Val {
    std::int64_t n = 0;
    Dim d;
  };

  /** e-TeX's `/`: the nearest integer, halves away from zero. */
  static std::int64_t roundDiv(std::int64_t a, std::int64_t b) {
    if (b == 0) return 0;
    const bool negative = (a < 0) != (b < 0);
    const std::uint64_t x = static_cast<std::uint64_t>(a < 0 ? -a : a), y = static_cast<std::uint64_t>(b < 0 ? -b : b);
    const std::int64_t q = static_cast<std::int64_t>((2 * x + y) / (2 * y));
    return negative ? -q : q;
  }

  /** `v` times `mul` over `div`: scaled points (the part in points is held in them) are rounded once, ems and exs follow. */
  static void scaleVal(char mode, Val& v, std::int64_t mul, std::int64_t div) {
    if (mode == 'i') {
      v.n = roundDiv(v.n * mul, div);
      return;
    }
    const std::int64_t sp = std::llround(v.d.pt * 65536.0);
    v.d.pt = static_cast<double>(roundDiv(sp * mul, div)) / 65536.0;
    const double f = div == 0 ? 0.0 : static_cast<double>(mul) / static_cast<double>(div);
    v.d.em *= f;
    v.d.ex *= f;
  }

  bool exprFactor(char mode, Val& v) {
    std::vector<ExpToken> seen;
    ExpToken t = nextNonSpaceExpanded();
    bool negative = false;
    while (isChar(t, '+') || isChar(t, '-')) {
      if (isChar(t, '-')) negative = !negative;
      seen.push_back(std::move(t));
      t = nextNonSpaceExpanded();
    }
    if (isChar(t, '(')) {
      if (!exprSum(mode, v)) {
        diags.warn(t.tok.span, "missing number in the parentheses, treated as zero");
        v = Val();
      }
      ExpToken close = nextNonSpaceExpanded();
      if (!isChar(close, ')')) {
        diags.warn(close.tok.span, "missing ) inserted for an expression");
        unread(std::move(close));
      }
      if (negative) {
        v.n = -v.n;
        v.d = -v.d;
      }
      return true;
    }
    unread(std::move(t));
    giveBack(seen);
    return mode == 'i' ? tryInt(v.n) : tryDim(v.d);
  }

  bool exprTerm(char mode, Val& v) {
    if (!exprFactor(mode, v)) return false;
    while (true) {
      ExpToken op = nextNonSpaceExpanded();
      if (!isChar(op, '*') && !isChar(op, '/')) {
        unread(std::move(op));
        return true;
      }
      Val r;
      if (!exprFactor('i', r)) {
        diags.warn(op.tok.span, "missing number after an operator, treated as zero");
        return true;
      }
      if (isChar(op, '*')) {
        // `a*b/c` is one step
        ExpToken next = nextNonSpaceExpanded();
        if (isChar(next, '/')) {
          Val c;
          if (!exprFactor('i', c)) c.n = 1;
          scaleVal(mode, v, r.n, c.n);
        } else {
          unread(std::move(next));
          scaleVal(mode, v, r.n, 1);
        }
      } else {
        scaleVal(mode, v, 1, r.n);
      }
    }
  }

  bool exprSum(char mode, Val& v) {
    if (!exprTerm(mode, v)) return false;
    while (true) {
      ExpToken op = nextNonSpaceExpanded();
      if (!isChar(op, '+') && !isChar(op, '-')) {
        unread(std::move(op));
        return true;
      }
      Val r;
      if (!exprTerm(mode, r)) {
        diags.warn(op.tok.span, "missing number after an operator, treated as zero");
        return true;
      }
      const int sign = isChar(op, '-') ? -1 : 1;
      v.n += sign * r.n;
      v.d = sign < 0 ? v.d - r.d : v.d + r.d;
    }
  }

  /** The expression after \numexpr (`mode` 'i') or \dimexpr ('d'), and the \relax that ends it. */
  bool exprValue(char mode, Val& v) {
    ScanGuard guard(*this);
    if (!exprSum(mode, v)) return false;
    ExpToken r = raw();
    if (!(r.tok.kind == TokKind::controlWord && r.tok.text == "relax")) unread(std::move(r));
    return true;
  }

  /** The relation of \ifnum and \ifdim: `<`, `=` or `>`; a missing one is `=` with a warning, as in TeX. */
  char readRelation(const SourceSpan& at, const char* who) {
    ExpToken r = nextNonSpaceExpanded();
    if (isChar(r, '<') || isChar(r, '=') || isChar(r, '>')) return static_cast<char>(r.tok.cp);
    diags.warn(at, std::string("missing = inserted for ") + who);
    unread(std::move(r));
    return '=';
  }

  bool ifnum(const SourceSpan& at) {
    const long a = readNumber(at);
    const char relation = readRelation(at, "\\ifnum");
    const long b = readNumber(at);
    return relation == '<' ? a < b : relation == '>' ? a > b : a == b;
  }

  /** \ifdim. `ok` is false when a side is not a length the expander can read: the conditional is then left for the parser (as
   *  before registers, kableExtra's `\ifdim\width>\linewidth` is read there), and what was read goes back. */
  bool ifdim(const SourceSpan& at, bool& ok) {
    std::vector<ExpToken> taken;
    std::vector<ExpToken>* const outer = tape;
    tape = &taken;
    Dim a, b;
    bool good = tryDim(a);
    char relation = '=';
    if (good) {
      ExpToken r = nextNonSpaceExpanded();
      if (isChar(r, '<') || isChar(r, '=') || isChar(r, '>')) {
        relation = static_cast<char>(r.tok.cp);
        good = tryDim(b);
      } else {
        unread(std::move(r));
        good = false;
      }
    }
    tape = outer;
    ok = good;
    if (!good) {
      for (auto it = taken.rbegin(); it != taken.rend(); ++it) {
        frames.back().pushback.push_back(std::move(*it));
      }
      return false;
    }
    if (outer != nullptr) outer->insert(outer->end(), taken.begin(), taken.end());
    (void)at;
    const std::int64_t x = a.sp(), y = b.sp();
    return relation == '<' ? x < y : relation == '>' ? x > y : x == y;
  }

  bool ifodd(const SourceSpan& at) { return readNumber(at) % 2 != 0; }

  static char registerKind(const std::string& name) {
    return name == "count" ? 'c' : name == "dimen" ? 'd' : name == "skip" ? 's' : name == "toks" ? 't' : '\0';
  }

  /** The register a token names: one \newcount and the like made, or `\count`, `\dimen`, `\skip` with the number after. */
  std::optional<RegKey> registerOf(const ExpToken& t) {
    if (t.tok.kind != TokKind::controlWord) return std::nullopt;
    const std::string& n = t.tok.text;
    if (lookup(n) != nullptr) return std::nullopt;
    const auto named = regNames.find(n);
    if (named != regNames.end()) return named->second;
    const char kind = registerKind(n);
    if (kind == '\0') return std::nullopt;
    std::int64_t number = 0;
    if (!tryInt(number)) diags.warn(t.tok.span, "missing number, treated as zero");
    return RegKey{kind, static_cast<int>(number)};
  }

  void setRegister(const RegKey& key, const Reg& value, bool global = false) {
    if (!global && depth > 0) {
      const auto old = regs.find(key);
      regUndo.push_back({key, old != regs.end(), old != regs.end() ? old->second : Reg(), depth});
    }
    regs[key] = value;
  }

  /** What a register prints: `\the` gives glue whole; where a length is wanted (`full` false) it is the natural size. */
  std::string registerText(const RegKey& key, bool full = false) {
    const Reg& r = regs[key];
    if (key.first == 'c') return std::to_string(r.n);
    if (key.first == 't') return r.text;
    return key.first == 's' && full ? theGlue(r.g) : theDim(r.g.natural);
  }

  /** Read the value `key`'s kind takes into `value`. */
  bool readRegisterValue(const RegKey& key, Reg& value) {
    if (key.first == 'c') return tryInt(value.n);
    if (key.first == 'd') return tryDim(value.g.natural);
    return tryGlue(value.g);
  }

  /** A register token read where TeX would begin an assignment or take an operand. */
  bool registerToken(const ExpToken& e) {
    if (keepRegisterTokens) return false;
    if (expandOnly > 0 && scanDepth == 0) return false;  // an \edef keeps a register's name, as TeX does
    const bool global = globalPrefix;  // (reading the number of \count5 ends it)
    const auto key = registerOf(e);
    if (!key) return false;
    if (key->first == 't') return toksAssignment(e, *key, global);
    bool assignment = false;
    if (scanDepth == 0) {
      // `\x=5pt` and `\x 5pt` set it; `\x` anywhere else is its value.
      std::vector<ExpToken> spaces;
      const ExpToken next = peekNonSpaceKeeping(spaces);
      int digit = 0;
      assignment = isChar(next, '=') || isDigitOf(next, 10, digit) || isChar(next, '+') || isChar(next, '-') ||
                   isChar(next, '.') || isChar(next, ',') || isChar(next, '"') || isChar(next, '\'') || isChar(next, '`');
    }
    if (assignment) {
      ExpToken eq = nextNonSpace();
      if (!isChar(eq, '=')) unread(std::move(eq));
      Reg value = regs[*key];
      value.unknown = false;
      if (!readRegisterValue(*key, value)) {
        diags.warn(e.tok.span, "\\" + e.tok.text + ": the value is not one the expander can read; the register is left as it was");
      } else {
        setRegister(*key, value, global);
      }
      globalPrefix = false;
      return true;
    }
    if (regs[*key].unknown) return false;  // the lowering knows what it is
    pushExpansion(registerText(*key), e.tok.span, e.atLetter);
    return true;
  }

  /** `2\gap`: digits followed by a length register are that many of it (tex.web 448, scan_dimen). The digits are read ahead where
   *  tokens are handed out, as soon as one is seen; True when they and the register were replaced by the size. */
  bool factorBeforeRegister(const ExpToken& first) {
    const auto digitOrDot = [](const ExpToken& t) {
      return t.tok.kind == TokKind::character && t.tok.cat == Cat::other &&
             ((t.tok.cp >= '0' && t.tok.cp <= '9') || t.tok.cp == '.');
    };
    std::vector<ExpToken> run;
    std::string number(first.text);
    ExpToken n = raw();
    while (digitOrDot(n)) {
      number += std::string(n.text);
      run.push_back(std::move(n));
      n = raw();
    }
    if (n.tok.kind == TokKind::controlWord && lookup(n.tok.text) == nullptr) {
      const auto named = regNames.find(n.tok.text);
      const char kind = named != regNames.end() ? named->second.first : registerKind(n.tok.text);
      if (kind == 'd' || kind == 's') {
        const auto key = registerOf(n);
        if (key && !regs[*key].unknown) {
          pushExpansion(theDim(regs[*key].g.natural.scaled(std::atof(number.c_str()))), first.tok.span, first.atLetter);
          return true;
        }
        if (key) unread(std::move(n));
        for (auto it = run.rbegin(); it != run.rend(); ++it) unread(std::move(*it));
        return false;
      }
    }
    unread(std::move(n));
    for (auto it = run.rbegin(); it != run.rend(); ++it) unread(std::move(*it));
    return false;
  }

  /** `\x={tokens}`, `\x=\y`, `\x=\expandafter{...}` for a \toks register (tex.web 1226): the tokens are kept as written. A token
   *  register anywhere else is not an operand (only `\the` gives its tokens), so it stays as it is. */
  bool toksAssignment(const ExpToken& e, const RegKey& key, bool global) {
    if (scanDepth > 0) return false;
    std::vector<ExpToken> spaces;
    const ExpToken peek = peekNonSpaceKeeping(spaces);
    // (TeX expands what follows to find the `{`: `\x\expandafter{\the\x ...}` is an assignment)
    const bool expandable = peek.tok.kind == TokKind::controlWord && !peek.tok.noexpand &&
                            (lookup(peek.tok.text) != nullptr || isExpandablePrimitive(peek.tok.text) || peek.tok.text == "expandafter");
    if (!isChar(peek, '=') && !peek.tok.isChar(Cat::beginGroup) && !expandable) return false;
    ExpToken eq = nextNonSpace();
    if (!isChar(eq, '=')) unread(std::move(eq));
    ExpToken t = nextNonSpaceExpanded();
    Reg r = regs[key];
    if (t.tok.isChar(Cat::beginGroup)) {
      r.text = readGroupContent(t);
      depth--;  // (nextExpanded counted the `{`; the group is read whole, not walked through)
    } else if (const auto other = registerOf(t); other && other->first == 't') {
      r.text = regs[*other].text;
    } else {
      diags.warn(e.tok.span, "\\" + e.tok.text + ": missing { inserted; the register is left as it was");
      unread(std::move(t));
      return true;
    }
    setRegister(key, r, global);
    globalPrefix = false;
    return true;
  }

  /** \newcount\x, \newskip\x, \countdef\x=n: `\x` is a register. */
  void newRegister(const ExpToken& e, char kind, bool numbered) {
    ExpToken n = nextNonSpace();
    if (n.tok.kind != TokKind::controlWord) {
      unread(std::move(n));
      diags.warn(e.tok.span, "\\" + e.tok.text + ": not followed by a control sequence");
      return;
    }
    const std::string name = n.tok.text;
    int number = nextRegister++;
    if (numbered) {
      ExpToken eq = nextNonSpace();
      if (!isChar(eq, '=')) unread(std::move(eq));
      std::int64_t k = 0;
      if (!tryInt(k)) diags.warn(e.tok.span, "missing number, treated as zero");
      number = static_cast<int>(k);
    } else if (meaningOf(name) != "undefined" && meaningOf(name) != meaningOf("relax")) {
      diags.warn(n.tok.span, "\\" + e.tok.text + ": \\" + name + " is already defined");
      return;
    }
    // the name is a register now, whatever a macro of it was
    macros.erase(name);
    regNames[name] = {kind, number};
  }

  /** \chardef\x=n and \mathchardef: `\x` is that number. */
  void numberConstant(const ExpToken& e) {
    ExpToken n = nextNonSpace();
    if (n.tok.kind != TokKind::controlWord) {
      unread(std::move(n));
      diags.warn(e.tok.span, "\\" + e.tok.text + ": not followed by a control sequence");
      return;
    }
    ExpToken eq = nextNonSpace();
    if (!isChar(eq, '=')) unread(std::move(eq));
    std::int64_t k = 0;
    if (!tryInt(k)) diags.warn(e.tok.span, "missing number, treated as zero");
    MacroDef def;
    def.alias = false;
    def.atLetter = atLetterNow();
    def.body = literal(std::to_string(k));
    store(n.tok.text, std::move(def));
  }

  /** \advance\x by v, \multiply\x by n, \divide\x by n (tex.web 1236). */
  void arithmetic(const ExpToken& e) {
    const std::string op = e.tok.text;
    const bool global = globalPrefix;
    ExpToken t = nextNonSpace();
    const auto key = registerOf(t);
    if (!key) {
      unread(std::move(t));
      diags.warn(e.tok.span, "\\" + op + ": not followed by a register");
      return;
    }
    (void)keyword("by");
    Reg r = regs[*key];
    if (op == "advance") {
      Reg v;
      if (!readRegisterValue(*key, v)) {
        diags.warn(e.tok.span, "\\advance: the value is not one the expander can read; the register is left as it was");
        return;
      }
      if (key->first == 'c') {
        r.n += v.n;
      } else {
        r.g.natural = r.g.natural + v.g.natural;
        if (key->first == 's') {
          if (r.g.stretchOrder == v.g.stretchOrder) r.g.stretch = r.g.stretch + v.g.stretch;
          else if (v.g.stretchOrder > r.g.stretchOrder) r.g.stretch = v.g.stretch, r.g.stretchOrder = v.g.stretchOrder;
          if (r.g.shrinkOrder == v.g.shrinkOrder) r.g.shrink = r.g.shrink + v.g.shrink;
          else if (v.g.shrinkOrder > r.g.shrinkOrder) r.g.shrink = v.g.shrink, r.g.shrinkOrder = v.g.shrinkOrder;
        }
      }
    } else {
      std::int64_t n = 0;
      if (!tryInt(n)) {
        diags.warn(e.tok.span, "\\" + op + ": missing number, treated as zero");
      }
      if (op == "divide" && n == 0) {
        diags.warn(e.tok.span, "\\divide: arithmetic overflow (division by zero)");
        return;
      }
      const auto scale = [&](Dim& d) { d = op == "multiply" ? d.scaled(static_cast<double>(n)) : d.scaled(1.0 / static_cast<double>(n)); };
      if (key->first == 'c') {
        r.n = op == "multiply" ? r.n * n : r.n / n;
      } else {
        scale(r.g.natural);
        if (key->first == 's') scale(r.g.stretch), scale(r.g.shrink);
      }
    }
    r.unknown = false;
    setRegister(*key, r, global);
    globalPrefix = false;
  }

  /** \the\x, \the\count5: what the register prints. Anything else after \the is the parser's. */
  bool theCommand(const ExpToken& e) {
    // (macros and \expandafter between \the and the quantity are expanded, as TeX does; register names are kept)
    keepRegisterTokens = true;
    ExpToken t = nextNonSpaceExpanded();
    keepRegisterTokens = false;
    if (t.tok.kind == TokKind::controlWord && t.tok.text == "catcode" && lookup("catcode") == nullptr) {
      std::int64_t code = 0;
      if (tryInt(code) && code >= 0 && code < 128) {
        pushExpansion(std::to_string(static_cast<int>(cats.get(static_cast<unsigned char>(code)))), e.tok.span, e.atLetter);
        return true;
      }
      return true;
    }
    if (t.tok.kind == TokKind::controlWord && lookup(t.tok.text) == nullptr &&
        (t.tok.text == "numexpr" || t.tok.text == "dimexpr")) {
      const bool integer = t.tok.text == "numexpr";
      unread(std::move(t));
      std::int64_t n = 0;
      Dim dim;
      if (integer ? tryInt(n) : tryDim(dim)) {
        pushExpansion(integer ? std::to_string(n) : theDim(dim), e.tok.span, e.atLetter);
      } else {
        diags.warn(e.tok.span, "\\the: the expression is not one the expander can read");
      }
      return true;
    }
    const auto key = registerOf(t);
    if (!key || regs[*key].unknown) {
      if (key) {
        unread(std::move(t));  // an unknown register stays as it is
      } else {
        unread(std::move(t));
      }
      return false;
    }
    pushExpansion(registerText(*key, true), e.tok.span, e.atLetter);
    return true;
  }

  /** The text of a length the lowering reads, worked out as a glue: expanded, then read to its end. False when it is not one. */
  bool evalGlue(const std::string& text, const SourceSpan& at, Glue& out) {
    const std::string expanded = trim(expandBody(text, at, false));
    if (expanded.empty()) return false;
    pushExpansion(expanded + "\\gmedefend", at);
    Glue g;
    bool ok = tryGlue(g);
    if (ok) {
      ExpToken after = nextNonSpaceExpanded();
      ok = after.tok.kind == TokKind::controlWord && after.tok.text == "gmedefend";
      if (!ok) unread(std::move(after));
    }
    if (!ok) {
      // drop what is left of the text
      while (true) {
        ExpToken t = raw();
        if (t.tok.kind == TokKind::end || (t.tok.kind == TokKind::controlWord && t.tok.text == "gmedefend")) break;
      }
      return false;
    }
    out = g;
    return true;
  }

  /** \newlength, \setlength and \addtolength: LaTeX's names for a \skip and its assignments. The lowering keeps lengths of its own
   *  (\parindent and the rest set the text) and must see every command to keep them in step, so each is handed on; this one
   *  keeps a register beside it, to read the values in conditionals and sums. A value it cannot work out (\linewidth, the
   *  page's) makes the register `unknown`: its uses stay the name, and the lowering says what it is. */
  void lengthCommand(const ExpToken& e) {
    const std::string op = e.tok.text;
    const std::string who = "\\" + op;
    const std::string nameText = trim(readArg(who, false));
    const bool isName = nameText.size() > 1 && nameText[0] == '\\';
    const std::string name = isName ? nameText.substr(1) : std::string();
    if (op == "newlength") {
      if (isName && meaningOf(name) == "undefined") regNames[name] = {'s', nextRegister++};
      passThrough(who + "{" + nameText + "}", e.tok.span);
      return;
    }
    const std::string valueText = readArg(who, false);
    const auto named = isName ? regNames.find(name) : regNames.end();
    // what the lowering is given: the value with the registers in it worked out
    const std::string given = trim(expandBody(valueText, e.tok.span, false));
    if (named != regNames.end()) {
      Reg r = regs[named->second];
      Glue g;
      const bool known = !r.unknown || op == "setlength";
      if (known && evalGlue(given, e.tok.span, g)) {
        if (op == "setlength") {
          r.g = g;
        } else {
          r.g.natural = r.g.natural + g.natural;
        }
        r.unknown = false;
      } else {
        r.unknown = true;
      }
      setRegister(named->second, r);
    }
    passThrough(who + "{" + nameText + "}{" + given + "}", e.tok.span);
  }

  /** latex.ltx's \settowidth, \settoheight, \settodepth and \settototalheight: a length is made the size of what is set in a box. The
   *  host's layout measures it (hooks.h), in ems of the body font, so the register holds the size as the lowering will
   *  read it; with no layout to ask, or a name that is not a length, the length is left as it was and the input is told. */
  void setToSize(const ExpToken& e) {
    const std::string who = "\\" + e.tok.text;
    const std::string nameText = trim(readArg(who, false));
    // What is set in the box is typeset, so what it does is done (a \the of a register, an assignment), in a group of its own as in
    // latex.ltx's `\hbox{{#2}}`
    const std::string text = trim(expandBody("{" + readArg(who, false) + "}", e.tok.span, false, e.atLetter));
    const auto named = nameText.size() > 1 && nameText[0] == '\\' ? regNames.find(nameText.substr(1)) : regNames.end();
    if (named == regNames.end() || (named->second.first != 'd' && named->second.first != 's')) {
      diags.warn(e.tok.span, who + ": " + nameText + " is not a length");
      return;
    }
    const auto& measure = boxMeasurer();
    const std::optional<BoxSize> size = measure ? measure(text) : std::nullopt;
    if (!size) {
      diags.warn(e.tok.span, who + ": the text cannot be measured here; the length is left as it was");
      return;
    }
    const double em = e.tok.text == "settowidth"    ? size->width
                      : e.tok.text == "settoheight" ? size->height
                      : e.tok.text == "settodepth"  ? size->depth
                                                    : size->height + size->depth;
    Reg r = regs[named->second];
    // A box's size is a size, not a number of ems of whatever font it is used in (`\hspace{\dimen@}` in a superscript is as wide as the
    // box was): in points of the page the lengths follow, where the body font's em is 10pt.
    r.g.natural = Dim{em * kEmPt, 0, 0};
    r.unknown = false;
    setRegister(named->second, r);
  }

  /** Hand a command and its arguments, already expanded, on to the parser as written: its control sequences are not expanded
   *  again (a register's name in them is a name). */
  void passThrough(const std::string& text, const SourceSpan& at) {
    buffers.push_back(text);
    const std::string_view buf = buffers.back();
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    lo.atLetter = atLetterNow();
    Lexer lx(buffers.back(), lo, quiet, cats);
    std::vector<ExpToken> tokens;
    while (true) {
      Token t = lx.next();
      if (t.kind == TokKind::end) break;
      ExpToken x;
      x.lead = std::string(buf.substr(t.leadStart, t.span.offset - t.leadStart));
      x.text = buf.substr(t.span.offset, t.span.length);
      x.tok = std::move(t);
      x.tok.span = at;
      x.atLetter = lo.atLetter;
      if (x.tok.isControl()) x.tok.noexpand = true;
      tokens.push_back(std::move(x));
    }
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) unread(std::move(*it));
  }

  /** Skip what is not read of a conditional, up to the \fi that ends it --
   *  or, with `stopAtElse`, the \else that begins its other branch --
   *  passing the conditionals nested in it whole. True at an \else, false
   *  at the \fi (or at the end of the input, which is a warning). */
  bool skipBranch(const SourceSpan& at, bool stopAtElse) {
    int nest = 0;
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        diags.warn(at, "missing \\fi: a conditional is still open at the end of the input");
        unread(std::move(t));
        return false;
      }
      if (t.tok.kind != TokKind::controlWord || t.tok.noexpand) continue;
      const std::string p = primitiveOf(t.tok.text);
      if (isCondPrimitive(p) || (p.empty() && isForeignCond(t.tok.text) && lookup(t.tok.text) == nullptr)) {
        nest++;
      } else if (p == "fi") {
        if (nest == 0) return false;
        nest--;
      } else if (p == "else" && nest == 0 && stopAtElse) {
        return true;
      }
    }
  }

  /** Begin the conditional `kind` (a primitive's name): read its test, then
   *  the branch it picks. */
  bool conditional(const std::string& kind, bool negate, const SourceSpan& at) {
    bool result = kind == "iftrue";
    // While a test is read, an \else or \fi of this conditional ends what is read and waits (tex.web 510 inserts a \relax before it):
    // `\ifnum\z@=`}\fi` reads `} as a number, and the \fi after it is the conditional's, not the number's.
    struct Testing {
      std::size_t& depth;
      bool& on;
      std::size_t keptDepth;
      bool keptOn;
      Testing(std::size_t& d, bool& o, std::size_t now) : depth(d), on(o), keptDepth(d), keptOn(o) { d = now; o = true; }
      ~Testing() { depth = keptDepth; on = keptOn; }
    } testing(testDepth, testing_, conds.size());
    if (kind == "ifx") result = ifx();
    if (kind == "if" || kind == "ifcat") result = ifchar(kind == "ifcat");
    if (kind == "ifdefined") result = ifdefined();
    if (kind == "ifcsname") result = ifcsname(at);
    if (kind == "ifnum") result = ifnum(at);
    if (kind == "ifodd") result = ifodd(at);
    if (kind == "ifdim") {
      bool ok = false;
      result = ifdim(at, ok);
      if (!ok) {
        // a length of the lowering's: the parser reads the conditional, with its \else and \fi
        conds.push_back('f');
        return false;
      }
    }
    if (negate) result = !result;
    if (result) {
      conds.push_back('t');
    } else if (skipBranch(at, true)) {
      conds.push_back('e');
    }
    return true;
  }

  /** \else: the branch read ends, and the rest is skipped up to the \fi.
   *  False when it is not ours -- no conditional open, or a foreign one --
   *  and goes through to the parser. */
  bool elseOfConditional(const SourceSpan& at) {
    if (conds.empty() || conds.back() == 'f') return false;
    if (conds.back() == 'e') {
      diags.warn(at, "extra \\else");
      return true;
    }
    skipBranch(at, false);
    conds.pop_back();
    return true;
  }

  bool endOfConditional() {
    if (conds.empty()) return false;
    const bool ours = conds.back() != 'f';
    conds.pop_back();
    return ours;
  }

  /** \newif\iffoo: `\iffoo`, which is false, and `\footrue` and `\foofalse`
   *  to set it. */
  void declareConditional() {
    const ExpToken n = raw();
    const std::string cs = n.tok.isControl() ? n.tok.text : std::string();
    if (cs.size() < 3 || cs.compare(0, 2, "if") != 0) {
      fail("newif", "\\newif: " + std::string(n.text) + " is not of the form \\ifname");
    }
    const std::string base = cs.substr(2);
    const auto define = [&](const std::string& name, const std::string& body, bool alias) {
      MacroDef def;
      def.alias = alias;
      def.atLetter = atLetterNow();
      def.body = literal(body);
      store(name, std::move(def));
    };
    define(cs, "\\iffalse", true);
    define(base + "true", "\\let\\" + cs + "\\iftrue", false);
    define(base + "false", "\\let\\" + cs + "\\iffalse", false);
  }

  // --- xparse's commands ---------------------------------------------------

  /** The text up to `close` at brace depth 0, after the opening delimiter
   *  has been read. */
  std::string readUpTo(const Token& close, const SourceSpan& at, const std::string& who) {
    std::string s;
    int depth = 0;
    while (true) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        diags.warn(at, "runaway argument: " + who + " is missing its closing delimiter");
        unread(std::move(t));
        return s;
      }
      if (depth == 0 && sameToken(t.tok, close)) return s;
      if (t.tok.isChar(Cat::beginGroup)) depth++;
      if (t.tok.isChar(Cat::endGroup)) depth--;
      s += t.lead;
      s.append(t.text);
    }
  }

  /** One argument of a command made by \NewDocumentCommand: an optional one
   *  not there is \NoValue (or its default), a star or token that is there
   *  \BooleanTrue, and one that is not \BooleanFalse. */
  /** The next token that is not a space, and the spaces before it. They are left where they were: an argument that is
   *  not there leaves the spaces to whatever follows (checked against xparse in LuaLaTeX: `\f{x} b` keeps its space
   *  when `\f` ends in an optional argument). */
  ExpToken peekNonSpaceKeeping(std::vector<ExpToken>& spaces) {
    ExpToken t = raw();
    while (t.tok.kind == TokKind::space) {
      spaces.push_back(std::move(t));
      t = raw();
    }
    ExpToken copy = t;
    unread(std::move(t));
    for (auto it = spaces.rbegin(); it != spaces.rend(); ++it) unread(ExpToken(*it));
    return copy;
  }

  /** xparse's `>{\SplitList{x}}` and `>{\TrimSpaces}` (xparse-2018-04-12.sty, \__xparse_split_list:nn): the argument as the
   *  processors leave it. A split argument is its pieces, each in braces, so that it is read as that many arguments. */
  std::string processed(const XArg& a, std::string arg, const std::string& who) {
    for (const std::string& code : a.processors) {
      const std::string c = trim(code);
      if (c == "\\TrimSpaces") {
        arg = trim(arg);
      } else if (c.compare(0, 11, "\\SplitList{") == 0 && c.back() == '}') {
        const std::string sep = c.substr(11, c.size() - 12);
        std::string out;
        int level = 0;
        std::size_t from = 0, i = 0;
        const auto flush = [&](std::size_t to) { out += "{" + arg.substr(from, to - from) + "}"; };
        if (sep.empty()) {
          out = "{" + arg + "}";
        } else {
          for (; i < arg.size(); i++) {
            if (arg[i] == '{') level++;
            if (arg[i] == '}') level--;
            if (level == 0 && arg.compare(i, sep.size(), sep) == 0) {
              flush(i);
              i += sep.size() - 1;
              from = i + 1;
            }
          }
          flush(arg.size());
        }
        arg = out;
      } else {
        diags.warn({}, who + ": the argument processor " + c + " is not supported and is left out");
      }
    }
    return arg;
  }

  std::string readXArg(const XArg& a, const std::string& who) {
    std::vector<ExpToken> spaces;
    switch (a.type) {
      case 'o':
      case 'O': {
        if (!isChar(peekNonSpaceKeeping(spaces), '[')) return a.type == 'o' ? std::string("\\NoValue") : a.deflt;
        const std::optional<std::string> o = readOptional();
        return o ? *o : a.type == 'o' ? std::string("\\NoValue") : a.deflt;
      }
      case 's':
      case 't': {
        const ExpToken next = peekNonSpaceKeeping(spaces);
        const bool there = a.type == 's' ? isChar(next, '*') : sameToken(next.tok, a.open);
        if (there) nextNonSpace();
        return there ? "\\BooleanTrue" : "\\BooleanFalse";
      }
      case 'g':
      case 'G': {
        if (!peekNonSpaceKeeping(spaces).tok.isChar(Cat::beginGroup)) {
          return a.type == 'g' ? std::string("\\NoValue") : a.deflt;
        }
        return readGroupContent(nextNonSpace());
      }
      case 'r':
      case 'R':
      case 'd':
      case 'D': {
        const bool required = a.type == 'r' || a.type == 'R';
        const ExpToken next = peekNonSpaceKeeping(spaces);
        if (sameToken(next.tok, a.open)) {
          const ExpToken open = nextNonSpace();
          return readUpTo(a.close, open.tok.span, who);
        }
        if (required) diags.warn(next.tok.span, who + ": missing required argument, which starts with its opening delimiter");
        return a.type == 'd' || a.type == 'r' ? std::string("\\NoValue") : a.deflt;
      }
      case 'l': {
        // up to the first `{` at brace depth 0, which is not part of it (TeX's `#1#{`)
        std::string s;
        while (true) {
          ExpToken t = raw();
          if (t.tok.kind == TokKind::end || t.tok.kind == TokKind::par || t.tok.isChar(Cat::beginGroup) ||
              t.tok.isChar(Cat::endGroup)) {
            unread(std::move(t));
            return s;
          }
          s += t.lead;
          s.append(t.text);
        }
      }
      case 'u': {
        const std::string s = readUntil(a.delim, who);
        abandoned = false;
        return s;
      }
      default:
        return readArg(who);
    }
  }

  /** The arguments a \NewDocumentCommand spec names: m o O s t r R d D g G l u,
   *  with + and ! read and ignored. `problem` says why, when it cannot be
   *  read. */
  std::vector<XArg> parseSpec(const std::string& spec, std::string& problem) {
    std::vector<XArg> out;
    std::vector<std::string> pending;
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    Lexer lx(spec, lo, quiet, cats);
    const std::string_view src(spec);
    const auto next = [&] {
      Token t = lx.next();
      while (t.kind == TokKind::space) t = lx.next();
      return t;
    };
    // A braced default, the text between the braces.
    const auto group = [&]() -> std::string {
      if (!next().isChar(Cat::beginGroup)) {
        problem = "a default must be given in braces";
        return {};
      }
      std::string text;
      int depth = 1;
      while (true) {
        const Token t = lx.next();
        if (t.kind == TokKind::end) {
          problem = "a default is missing its closing brace";
          return text;
        }
        if (t.isChar(Cat::beginGroup)) depth++;
        if (t.isChar(Cat::endGroup) && --depth == 0) return text;
        text.append(src.substr(t.leadStart, t.span.offset + t.span.length - t.leadStart));
      }
    };
    while (problem.empty()) {
      const Token t = next();
      if (t.kind == TokKind::end) break;
      if (t.kind != TokKind::character || t.cp >= 128) {
        problem = "there is no argument type '" + std::string(src.substr(t.span.offset, t.span.length)) + "'";
        break;
      }
      XArg a;
      a.type = static_cast<char>(t.cp);
      a.processors = pending;
      switch (a.type) {
        case '>':
          // a processor: the code is kept for the argument that follows
          pending.push_back(group());
          continue;
        case '+':
        case '!':
          continue;
        case 'm':
        case 'o':
        case 's':
        case 'g':
          break;
        case 'O':
        case 'G':
          a.deflt = group();
          break;
        case 't':
          a.open = next();
          break;
        case 'r':
        case 'd':
          a.open = next();
          a.close = next();
          break;
        case 'R':
        case 'D':
          a.open = next();
          a.close = next();
          a.deflt = group();
          break;
        case 'l':
          break;
        case 'u': {
          // u{<tokens>}: the argument is what comes before them
          if (!next().isChar(Cat::beginGroup)) {
            problem = "the argument type u must be followed by its delimiter in braces";
            break;
          }
          int open = 1;
          while (true) {
            const Token t = lx.next();
            if (t.kind == TokKind::end) {
              problem = "the delimiter of u is missing its closing brace";
              break;
            }
            if (t.isChar(Cat::beginGroup)) open++;
            if (t.isChar(Cat::endGroup) && --open == 0) break;
            a.delim.push_back(t);
          }
          break;
        }
        case 'e':
        case 'E':
        case 'b':
        case 'v':
          problem = std::string("the argument type ") + a.type + " is not supported";
          break;
        default:
          problem = std::string("unknown argument type '") + a.type + "'";
      }
      const bool bare = (a.type == 't' || a.type == 'r' || a.type == 'R' || a.type == 'd' || a.type == 'D') &&
                        (a.open.kind == TokKind::end || (a.type != 't' && a.close.kind == TokKind::end));
      if (bare && problem.empty()) problem = std::string("the argument type ") + a.type + " is missing its delimiters";
      if (problem.empty()) {
        out.push_back(std::move(a));
        pending.clear();
      }
    }
    if (problem.empty() && out.size() > 9) problem = "a command can have nine arguments at most";
    return out;
  }

  /** \NewDocumentCommand{\cmd}{spec}{body}, and its Renew, Provide and
   *  Declare forms, which follow \newcommand's, \renewcommand's,
   *  \providecommand's rules and replace without complaint. */
  void defineDocumentCommand(const ExpToken& e) {
    const std::string kind = e.tok.text;
    std::string name;
    try {
      name = readCsName(kind);
    } catch (const ex_parse& x) {
      failDefinition(kind, cleanMessage(x.what()), 2);
    }
    const std::string spec = readArg("\\" + kind, false);
    const std::string body = readArg("\\" + kind, false);
    std::string problem;
    std::vector<XArg> xargs = parseSpec(spec, problem);
    if (!problem.empty()) fail(kind, "\\" + name + ": " + problem);
    const bool exists = isDefined(name);
    if (kind == "NewDocumentCommand" && exists) {
      fail(kind, "Command " + name + " already exists! Use \\RenewDocumentCommand instead!");
    }
    if (kind == "RenewDocumentCommand" && !exists && !isSoft(name) && !isSiunitx(name)) {
      if (!opts.recover) fail(kind, "Command " + name + " is not defined! Use \\NewDocumentCommand instead!");
      diags.warn(e.tok.span, "\\" + kind + ": \\" + name + " was not defined; defined now");
    }
    MacroDef def;
    def.atLetter = atLetterNow();
    def.nparams = static_cast<int>(xargs.size());
    def.xargs = std::move(xargs);
    def.body = splitBody(body, def.nparams, "\\" + name, e.tok.span);
    if (!(kind == "ProvideDocumentCommand" && exists)) store(name, std::move(def));
  }

  /** \NewDocumentEnvironment{name}{spec}{begin}{end} and its kin. The end
   *  code does not see the arguments. */
  void defineDocumentEnvironment(const ExpToken& e) {
    const std::string kind = e.tok.text;
    const auto name = readGroupText();
    if (!name || name->empty()) failDefinition(kind, "\\" + kind + ": missing the environment name", 3);
    const std::string spec = readArg("\\" + kind, false);
    const std::string begin = readArg("\\" + kind, false);
    const std::string end = readArg("\\" + kind, false);
    std::string problem;
    std::vector<XArg> xargs = parseSpec(spec, problem);
    if (!problem.empty()) fail(kind, "{" + *name + "}: " + problem);
    const bool builtin = opts.isBuiltinEnvironment && opts.isBuiltinEnvironment(*name);
    const bool exists = lookupEnv(*name) != nullptr || builtin;
    if (kind == "NewDocumentEnvironment" && (builtin || (exists && !isSoftEnv(*name)))) {
      fail(kind, "Environment " + *name + " already exists! Use \\RenewDocumentEnvironment instead!");
    }
    if (kind == "RenewDocumentEnvironment" && !exists) {
      fail(kind, "Environment " + *name + " is not defined! Use \\NewDocumentEnvironment instead!");
    }
    MacroDef def;
    def.atLetter = atLetterNow();
    def.nparams = static_cast<int>(xargs.size());
    def.xargs = std::move(xargs);
    def.body = splitBody(begin, def.nparams, "\\begin{" + *name + "}", e.tok.span);
    def.endBody = splitBody(end, 0, "\\end{" + *name + "}", e.tok.span);
    if (!(kind == "ProvideDocumentEnvironment" && exists)) store(*name, std::move(def), true);
  }

  // --- package options ----------------------------------------------------

  /** What a package being read has declared (\DeclareOption), and the options
   *  it was given. */
  struct PackageOptions {
    std::string name;
    std::vector<std::string> given;
    std::vector<std::pair<std::string, std::string>> declared;
    std::string otherwise;
    bool hasOtherwise = false;
    /** What \AtBeginDocument asked for: done where the package ends. */
    std::string atBegin;
    /** Where the package's own text is, in `frames`. */
    std::size_t frame = 0;
  };
  std::vector<PackageOptions> packageStack;

  /** The code to run for the options `list`, as one text: each declared one
   *  by its code, each other by the default's, with \CurrentOption set. */
  void runOptions(const std::vector<std::string>& list, const SourceSpan& at) {
    if (packageStack.empty()) return;
    const PackageOptions& p = packageStack.back();
    std::string all;
    for (const std::string& option : list) {
      const std::string* code = nullptr;
      for (const auto& d : p.declared) {
        if (d.first == option) code = &d.second;
      }
      if (code == nullptr && p.hasOtherwise) code = &p.otherwise;
      if (code == nullptr) {
        diags.warn(at, "unknown option '" + option + "' for package " + p.name);
        continue;
      }
      all += "\\def\\CurrentOption{" + option + "}" + *code;
    }
    if (!all.empty()) pushExpansion(all, at, true);
  }

  static std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from <= text.size()) {
      std::size_t comma = text.find(',', from);
      if (comma == std::string::npos) comma = text.size();
      const std::string item = trim(text.substr(from, comma - from));
      if (!item.empty()) out.push_back(item);
      from = comma + 1;
    }
    return out;
  }

  /** \endinput in a package: the rest of its file is not read. */
  void endPackageFile() {
    if (packageStack.empty()) return;
    Frame& f = frames[packageStack.back().frame];
    buffers.push_back("\\gmendpackage");
    LexOptions lo = opts.lex;
    lo.startMidLine = true;
    lo.atLetter = true;
    f.buf = buffers.back();
    f.lexer = std::make_unique<Lexer>(buffers.back(), lo, quiet, cats);
    f.pushback.clear();
  }

  /** The text `body` expands to, as \edef makes a macro's: everything that expands
   *  does, and \noexpand's token and the parameters are left alone. */
  std::string expandBody(const std::string& body, const SourceSpan& at, bool onlyWhatExpands = true, bool atLetter = false) {
    struct Only {
      int& n;
      bool on;
      Only(int& count, bool enable) : n(count), on(enable) { if (on) n++; }
      ~Only() { if (on) n--; }
    } only(expandOnly, onlyWhatExpands);
    std::string saved = std::move(out), savedCarry = std::move(carry);
    const bool savedLast = lastControlWord;
    out.clear();
    carry.clear();
    lastControlWord = false;
    // (no space after the name: it would end up as the lead of whatever is read after the expansion)
    pushExpansion(body + "\\gmedefend", at, atLetter);
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
    def.atLetter = atLetterNow();
    def.nparams = readParamCount(readOptional(), kind, 2);
    if (const auto d = readOptional()) {
      def.hasOptional = true;
      def.optionalDefault = *d;
    }
    const std::string begin = readArg("\\" + kind, false);
    const std::string end = readArg("\\" + kind, false);
    def.body = splitBody(begin, def.nparams, "\\begin{" + *name + "}", e.tok.span);
    def.endBody = splitBody(end, 0, "\\end{" + *name + "}", e.tok.span);

    const bool builtin = opts.isBuiltinEnvironment && opts.isBuiltinEnvironment(*name);
    const bool exists = lookupEnv(*name) != nullptr || builtin;
    // One a package gave us, which the input did not ask for, is free to define.
    if (kind == "newenvironment" && (builtin || (exists && !isSoftEnv(*name)))) {
      fail(kind, "Command " + *name + "@env already exists! Use renewcommand instead!");
    }
    if (kind == "renewenvironment" && !exists) {
      fail(kind, "Environment " + *name + " is not defined! Use newenvironment instead!");
    }
    store(*name, std::move(def), true);
  }

  /** Run the control word `e` if it is one of the expander's own commands --
   *  a definition, \let, an environment, \ce, siunitx's -- and say whether
   *  it was consumed (false: it passes through to the parser as well).
   *  Nothing when it is not one, or when a macro of that name is defined in
   *  its place; `force` skips that check, for an alias of the command,
   *  which means what the name meant when \let ran. */
  std::optional<bool> primitive(const ExpToken& e, bool force) {
    const std::string& name = e.tok.text;
    // A macro of the input's, or of a package, stands in place of the command.
    const auto unshadowed = [&] { return force || lookup(name) == nullptr; };
    if (name == "newcommand" || name == "renewcommand" || name == "providecommand" ||
        name == "DeclareRobustCommand") {
      defineCommand(e, name);
      return true;
    }
    if ((name == "ce" || name == "pu") && unshadowed()) {
      chemCommand(e);
      return true;
    }
    if (isSiunitx(name) && unshadowed()) {
      siunitxCommand(e);
      return true;
    }
    if (isModelColourCommand(name) && unshadowed()) return colourModelCommand(e);
    if ((name == "MakeUppercase" || name == "MakeLowercase") && unshadowed()) {
      changeCase(e);
      return true;
    }
    if (name == "gmprepend") {
      // `\gmprepend{env}{text}`: `text`, as written, goes in front of the body of every `env` from here on. A package that
      // keeps statements for its environments (tikz's styles) says it so; nothing is drawn here.
      const auto target = readGroupText();
      const std::string text = readArg("\\gmprepend");
      if (target) prepended[trim(*target)] += text;
      return true;
    }
    if ((name == "DefineVerbatimEnvironment" || name == "RecustomVerbatimEnvironment") && unshadowed()) {
      defineVerbatimEnvironment(e);
      return true;
    }
    if (name == "def" || name == "gdef" || name == "edef" || name == "xdef") {
      defineDef(e);
      return true;
    }
    if ((name == "@ifstar" || name == "@ifnextchar") && unshadowed()) {
      ifNextChar(e, name == "@ifstar");
      return true;
    }
    if (name == "global" && unshadowed()) {
      globalPrefix = true;
      return true;
    }
    if (name == "expandafter" && unshadowed()) {
      expandAfter();
      return true;
    }
    if (name == "noexpand" && unshadowed()) {
      ExpToken t = raw();
      t.tok.noexpand = true;
      unread(std::move(t));
      return true;
    }
    if (name == "gmcolourmodel") {
      // `\gmcolourmodel{a,b}`: these commands take xcolor's optional colour model, `\a[rgb]{.2,.4,.6}`.
      if (const auto names = readGroupText()) {
        std::size_t from = 0;
        while (from <= names->size()) {
          const std::size_t comma = std::min(names->find(',', from), names->size());
          if (const std::string one = trim(names->substr(from, comma - from)); !one.empty()) colourModel.insert(one);
          from = comma + 1;
        }
      }
      return true;
    }
    if (name == "gmenvironmentkind") {
      declareEnvironmentKind(e);
      return true;
    }
    if (name == "gmdeclare") {
      // `\gmdeclare{table}{key}{value}`: data a package gives the lowering (cleveref's names), the last word standing.
      const auto table = readGroupText();
      const auto key = table ? readGroupText() : std::nullopt;
      const auto value = key ? readGroupText() : std::nullopt;
      if (value) declared[trim(*table) + "\x1f" + trim(*key)] = *value;
      return true;
    }
    if ((name == "settowidth" || name == "settoheight" || name == "settodepth" || name == "settototalheight") && unshadowed()) {
      setToSize(e);
      return true;
    }
    if (name == "gmwarning") {
      // `\gmwarning{text}`: what \PackageWarning and \PackageError say (ltkernel.sty), as a warning at the line and column of the call.
      if (const auto text = readGroupText()) {
        std::string said;
        for (const char c : trim(expandBody(*text, e.tok.span, true, e.atLetter))) {
          if (std::isspace(static_cast<unsigned char>(c))) {
            if (!said.empty() && said.back() != ' ') said += ' ';
          } else {
            said += c;
          }
        }
        diags.warn(e.tok.span, said);
      }
      return true;
    }
    if (name == "ignorespaces" && unshadowed()) {
      // TeX expands what follows until a token that is not a space.
      ExpToken t = nextExpanded();
      while (t.tok.kind == TokKind::space) t = nextExpanded();
      if (t.tok.kind != TokKind::end) unread(std::move(t));
      return true;
    }
    if (name == "futurelet" && unshadowed()) {
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
    if ((name == "begingroup" || name == "endgroup") && unshadowed()) {
      // TeX's group without braces, which kableExtra's font_size writes.
      pushExpansion(name == "begingroup" ? "{" : "}", e.tok.span);
      return true;
    }
    if ((name == "begin" || name == "end") && unshadowed()) {
      return environment(e, name == "begin");
    }
    if ((name == "usepackage" || name == "RequirePackage") && unshadowed()) {
      usePackages(e, name == "RequirePackage");
      return true;
    }
    if ((name == "ProvidesPackage" || name == "NeedsTeXFormat") && unshadowed()) {
      // Where a file says what it is; a grob has no use for it.
      readArg("\\" + name, false);
      readOptional();
      return true;
    }
    // The TeX a package file is written in.
    if (name == "relax" && unshadowed()) return true;
    if (unshadowed() && (regNames.count(name) != 0 || registerKind(name) != '\0')) return registerToken(e);
    if ((name == "newcount" || name == "newdimen" || name == "newskip" || name == "newtoks") && unshadowed()) {
      newRegister(e, name == "newcount" ? 'c' : name == "newdimen" ? 'd' : name == "newskip" ? 's' : 't', false);
      return true;
    }
    if ((name == "countdef" || name == "dimendef" || name == "skipdef" || name == "toksdef") && unshadowed()) {
      newRegister(e, name == "countdef" ? 'c' : name == "dimendef" ? 'd' : name == "skipdef" ? 's' : 't', true);
      return true;
    }
    if ((name == "chardef" || name == "mathchardef") && unshadowed()) {
      numberConstant(e);
      return true;
    }
    if (name == "catcode" && unshadowed()) {
      // \catcode<number>=<number>: from here on, in this group
      const bool global = globalPrefix;
      std::int64_t code = 0, cat = 0;
      if (!tryInt(code)) diags.warn(e.tok.span, "missing number, treated as zero");
      ExpToken eq = nextNonSpace();
      if (!isChar(eq, '=')) unread(std::move(eq));
      if (!tryInt(cat)) diags.warn(e.tok.span, "missing number, treated as zero");
      if (code < 0 || code > 127 || cat < 0 || cat > 15) {
        diags.warn(e.tok.span, "\\catcode: the character code must be 0 to 127 and the category 0 to 15");
        return true;
      }
      if (!global && depth > 0) {
        Reg old;
        old.n = static_cast<std::int64_t>(cats.get(static_cast<unsigned char>(code)));
        regUndo.push_back({{'k', static_cast<int>(code)}, true, old, depth});
      }
      cats.set(static_cast<unsigned char>(code), static_cast<Cat>(cat));
      globalPrefix = false;
      return true;
    }
    if ((name == "advance" || name == "multiply" || name == "divide") && unshadowed()) {
      arithmetic(e);
      return true;
    }
    if (name == "aftergroup" && unshadowed()) {
      ExpToken t = raw();
      if (t.tok.kind == TokKind::end) {
        unread(std::move(t));
      } else {
        t.tok.noexpand = false;
        afterGroup.emplace_back(std::move(t), depth);
      }
      return true;
    }
    if (name == "the" && unshadowed()) return theCommand(e);
    if (name == "string" && unshadowed()) {
      // tex.web conv_toks: the characters of the next token, a control sequence's with its escape character. A backslash that is a
      // character cannot be written in text that is read again, so it is U+E000 here, and a name made of it (\csname) or a place
      // it ends up in (the output) turns it into the backslash.
      ExpToken u = raw();
      if (u.tok.kind == TokKind::end) {
        unread(std::move(u));
        return true;
      }
      // the escape character is \escapechar's (none when it is not a character code)
      std::string escape = "\xEE\x80\x80";
      if (const auto ec = regNames.find("escapechar"); ec != regNames.end()) {
        const std::int64_t code = regs[ec->second].n;
        escape = code == 92 ? escape : code >= 0 && code < 128 ? std::string(1, static_cast<char>(code)) : std::string();
      }
      const std::string text = u.tok.isControl() ? escape + u.tok.text : u.tok.kind == TokKind::space
                                                                                          ? std::string(" ")
                                                                                          : std::string(u.text);
      pushExpansion(text, e.tok.span, e.atLetter);
      return true;
    }
    if (name == "number" && unshadowed()) {
      std::int64_t v = 0;
      if (!tryInt(v)) return false;
      pushExpansion(std::to_string(v), e.tok.span, e.atLetter);
      return true;
    }
    if ((name == "newlength" || name == "setlength" || name == "addtolength") && unshadowed()) {
      lengthCommand(e);
      return true;
    }
    if (isCondPrimitive(name) && unshadowed()) return conditional(name, false, e.tok.span);
    if (name == "unless" && unshadowed()) {
      ExpToken t = raw();
      const std::string kind = t.tok.kind == TokKind::controlWord ? primitiveOf(t.tok.text) : std::string();
      if (!isCondPrimitive(kind)) {
        unread(std::move(t));
        fail("unless", "\\unless: not followed by a conditional");
      }
      if (!conditional(kind, true, e.tok.span)) passThrough("\\unless\\" + kind, e.tok.span);
      return true;
    }
    if (isForeignCond(name) && unshadowed()) {
      conds.push_back('f');
      return false;
    }
    if (name == "else" && unshadowed()) return elseOfConditional(e.tok.span);
    if (name == "fi" && unshadowed()) return endOfConditional();
    if (name == "csname" && unshadowed()) {
      csname(e);
      return true;
    }
    if (name == "newif" && unshadowed()) {
      declareConditional();
      return true;
    }
    if (name == "DeclareOption" && unshadowed()) {
      const bool star = takeStar();
      const std::string option = star ? std::string() : trim(readArg("\\DeclareOption", false));
      const std::string code = readArg("\\DeclareOption", false);
      if (packageStack.empty()) {
        diags.warn(e.tok.span, "\\DeclareOption outside a package is left out");
      } else if (star) {
        packageStack.back().otherwise = code;
        packageStack.back().hasOtherwise = true;
      } else {
        packageStack.back().declared.emplace_back(option, code);
      }
      return true;
    }
    if (name == "ProcessOptions" && unshadowed()) {
      takeStar();
      if (!packageStack.empty()) runOptions(packageStack.back().given, e.tok.span);
      return true;
    }
    if (name == "ExecuteOptions" && unshadowed()) {
      runOptions(splitList(readArg("\\ExecuteOptions", false)), e.tok.span);
      return true;
    }
    if (name == "AtBeginDocument" && unshadowed()) {
      const std::string code = readArg("\\AtBeginDocument", false);
      // A grob has no beginning of the document: a package's code runs where
      // the package ends, and the input's at once.
      if (packageStack.empty()) {
        pushExpansion(code, e.tok.span, e.atLetter);
      } else {
        packageStack.back().atBegin += code;
      }
      return true;
    }
    if (name == "endinput" && unshadowed()) {
      endPackageFile();
      return true;
    }
    if ((name == "NewDocumentCommand" || name == "RenewDocumentCommand" ||
         name == "ProvideDocumentCommand" || name == "DeclareDocumentCommand") &&
        unshadowed()) {
      defineDocumentCommand(e);
      return true;
    }
    if ((name == "NewDocumentEnvironment" || name == "RenewDocumentEnvironment" ||
         name == "ProvideDocumentEnvironment" || name == "DeclareDocumentEnvironment") &&
        unshadowed()) {
      defineDocumentEnvironment(e);
      return true;
    }
    if (name == "@ifpackageloaded" && unshadowed()) {
      const std::string who = "\\@ifpackageloaded";
      const std::string package = trim(readArg(who, false));
      const std::string yes = readArg(who, false), no = readArg(who, false);
      const bool isLoaded = loaded.count(package) != 0 || (shared != nullptr && shared->loaded.count(package) != 0);
      pushExpansion(isLoaded ? yes : no, e.tok.span, e.atLetter);
      return true;
    }
    return std::nullopt;
  }

  /** Handle a control sequence that is a definition, an environment or a
   *  macro: true when it was consumed, false when it passes through. What
   *  it consumed puts nothing in the output but its leading whitespace,
   *  which the caller carries on to the next token. */
  bool process(const ExpToken& e) {
    if (e.tok.kind == TokKind::controlWord) {
      // While a number or a length is being read ahead (the `plus` of a skip, the unit of a length), only what TeX expands is
      // expanded: a command that does something (\newdimen, \advance, \def) ends the reading and is run in its turn.
      if (testing_ && conds.size() == testDepth && (e.tok.text == "fi" || e.tok.text == "else") && lookup(e.tok.text) == nullptr) {
        return false;
      }
      if ((scanDepth > 0 || expandOnly > 0) && !isExpandablePrimitive(e.tok.text) && e.tok.text != "the" &&
          e.tok.text != "number" && e.tok.text != "noexpand" && e.tok.text != "expandafter" && e.tok.text != "string" &&
          regNames.count(e.tok.text) == 0 && registerKind(e.tok.text) == '\0' && lookup(e.tok.text) == nullptr &&
          isPrimitiveName(e.tok.text)) {
        return false;
      }
      if (const auto done = primitive(e, false)) return *done;
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

  /** \global is for the next assignment only: any other token read ends it.
   *  \expandafter hands it on to what it brings in front. */
  void endGlobalAt(const ExpToken& e) {
    if (!globalPrefix) return;
    if (e.tok.kind == TokKind::controlWord) {
      const std::string& n = e.tok.text;
      if (n == "global" || n == "def" || n == "gdef" || n == "edef" || n == "xdef" || n == "let" ||
          n == "futurelet" || n == "expandafter" || n == "advance" || n == "multiply" || n == "divide" || n == "count" ||
          n == "dimen" || n == "skip" || regNames.count(n) != 0) {
        return;
      }
    }
    globalPrefix = false;
  }

  /** The next token after expansion. */
  /** Where in the arguments of a key=value environment the tokens that pass are (see `keyArgs`). */
  void followKeyArguments(const ExpToken& e) {
    if (e.tok.kind == TokKind::end || e.tok.kind == TokKind::par) {
      keyArgs = 0;
    } else if (keyArgs == 4) {
      if (e.tok.isChar(Cat::endGroup) && depth == keyArgsAt) keyArgs = 1;  // the environment's name is past
    } else if (keyArgs == 1) {
      if (e.tok.kind == TokKind::space) return;
      if (e.tok.isChar(Cat::other) && e.text == "[") {
        keyArgs = 2;
        keyArgsAt = depth;
      } else if (e.tok.isChar(Cat::beginGroup)) {
        keyArgs = 3;
        keyArgsAt = depth;  // already counted
      } else {
        keyArgs = 0;
      }
    } else if (keyArgs == 2) {
      if (e.tok.isChar(Cat::other) && e.text == "]" && depth == keyArgsAt) keyArgs = 1;
    } else if (e.tok.isChar(Cat::endGroup) && depth < keyArgsAt) {
      keyArgs = --keyArgsLeft > 0 ? 1 : 0;
    }
  }

  ExpToken nextExpanded() {
    while (true) {
      ExpToken e = raw();
      endGlobalAt(e);
      if (inForeach && e.tok.kind != TokKind::end) {
        if (e.tok.isControl()) {
          foreachNames.insert(e.tok.text);
        } else if (e.tok.isChar(Cat::letter)) {
          inForeach = false;
        }
      } else if (e.tok.isCs("foreach")) {
        inForeach = true;
      }
      if (!foreachNames.empty() && e.tok.isControl() && foreachNames.count(e.tok.text) != 0) {
        e.tok.noexpand = true;
      }
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
          depth - leftOpen == openEnvs.back().depth + 1) {
        // This `}` closes the group the environment began in: the
        // environment ends first, and the `}` is read again after it.
        unread(std::move(e));
        closeOpenEnvironment();
        continue;
      }
      // The groups TeX has besides braces: \bgroup and \egroup are braces, and \left ... \right is a group too (tex.web 1191, 1191:
      // math_left_group), which ends with the delimiter that follows \right. A name the input made its own is its own.
      // (a \let alias of the builtin is handed on marked noexpand, and is the builtin even after the name was made a macro:
      // mleftright makes \left its own and still sets the original)
      const bool named = e.tok.kind == TokKind::controlWord && (e.tok.noexpand || lookup(e.tok.text) == nullptr);
      if (rightPending && e.tok.kind != TokKind::space && !e.tok.isChar(Cat::comment)) {
        rightPending = false;  // `e` is the delimiter after \right: the group ends with it
        depth--;
        if (leftOpen > 0) leftOpen--;
        endLocalGroup();
        releaseAfterGroup();
      } else if (named && e.tok.text == "right") {
        rightPending = true;
      }
      if (named && e.tok.text == "left") leftOpen++;
      if (e.tok.isChar(Cat::beginGroup) || (named && (e.tok.text == "bgroup" || e.tok.text == "left"))) depth++;
      if (e.tok.isChar(Cat::endGroup) || (named && e.tok.text == "egroup")) {
        depth--;
        endLocalGroup();
        releaseAfterGroup();  // \aftergroup's tokens come right after the `}` of the group they were saved in (tex.web 1063)
      }
      if (keyArgs != 0) followKeyArguments(e);
      if (scanDepth == 0 && !regNames.empty() && e.tok.kind == TokKind::character && e.tok.cat == Cat::other &&
          ((e.tok.cp >= '0' && e.tok.cp <= '9') || e.tok.cp == '.') && factorBeforeRegister(e)) {
        carry += e.lead;
        continue;
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
  for (const auto& kv : Expander::Impl::snapshot().macros) names.push_back(kv.first);
  return names;
}

std::vector<std::string> preludeEnvironmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : Expander::Impl::snapshot().envs) names.push_back(kv.first);
  return names;
}

std::vector<std::string> preludeProblems() {
  return Expander::Impl::snapshot().problems;
}

std::vector<std::string> checkPackage(const std::string& name) {
  Diagnostics found;
  ExpanderOptions opts;
  opts.prelude = true;
  opts.persistent = false;
  opts.recover = true;
  // A name the engine has already is as much a clash as one the format has.
  opts.isBuiltinCommand = [](const std::string& n) { return findCommand(n) != nullptr; };
  opts.isBuiltinEnvironment = [](const std::string& n) { return findEnvironment(n) != nullptr; };
  Expander(" \\usepackage{" + name + "}", std::move(opts), found).expandToText();
  std::vector<std::string> problems;
  for (const Diagnostic& d : found.items()) problems.push_back(d.message);
  return problems;
}

std::vector<std::string> preludeTransparentEnvironmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : Expander::Impl::snapshot().envs) {
    // The same test that marks the group transparent when one is expanded.
    const auto kind = Expander::Impl::snapshot().kinds.find(kv.first);
    const bool block = kind != Expander::Impl::snapshot().kinds.end() &&
                       (kind->second & (EnvironmentKind::block | EnvironmentKind::floating)) != 0;
    if ((kv.second.body.empty() && kv.second.endBody.empty()) || block) {
      names.push_back(kv.first);
    }
  }
  return names;
}

ExpandedToken Expander::next() {
  ExpToken e = _impl->nextExpanded();
  return {std::move(e.tok), std::move(e.lead), std::string(e.text)};
}

const std::string& Expander::prepended(const std::string& environment) const {
  static const std::string none;
  const auto it = _impl->prepended.find(environment);
  return it == _impl->prepended.end() ? none : it->second;
}

bool Expander::verbatimEnvironment(const std::string& name, std::string& base, std::string& options) const {
  const auto it = _impl->verbatimEnvironments.find(name);
  if (it == _impl->verbatimEnvironments.end()) return false;
  base = it->second.first;
  options = it->second.second;
  return true;
}

bool Expander::keyValueEnvironment(const std::string& name) const {
  return _impl->isKeyEnvironment(name);
}

std::unordered_map<std::string, std::uint8_t> Expander::environmentKinds() const {
  std::unordered_map<std::string, std::uint8_t> all;
  if (_impl->shared != nullptr) all = _impl->shared->kinds;
  for (const auto& [name, kind] : _impl->kinds) all[name] |= kind;
  return all;
}

std::map<std::string, std::string> Expander::declared() const {
  std::map<std::string, std::string> all;
  if (_impl->shared != nullptr) all = _impl->shared->declared;
  for (const auto& [key, value] : _impl->declared) all[key] = value;
  return all;
}

std::shared_ptr<const UserScope> Expander::userScope() const {
  auto scope = std::make_shared<UserScope>();
  scope->macros = _impl->macros;
  scope->envs = _impl->envs;
  return scope;
}

void Expander::endInputAt(std::uint32_t offset) {
  _impl->endInputAt(offset);
}

void Expander::setCatcode(c32 ch, Cat cat) {
  _impl->cats.set(ch, cat);
}

void Expander::pushText(const std::string& text, const SourceSpan& at) {
  _impl->pushExpansion(text, at);
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
