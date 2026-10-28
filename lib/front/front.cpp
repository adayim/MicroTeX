#include "front/front.h"

#include <cctype>
#include <cstring>
#include <utility>

#include "core/formula.h"
#include "front/diagnostics.h"
#include "front/expander.h"
#include "front/hooks.h"
#include "front/lengths.h"
#include "front/lower.h"
#include "front/parser.h"
#include "front/spec.h"
#include "macro/macro.h"
#include "unimath/uni_symbol.h"
#include "utils/exceptions.h"

namespace microtex::front {

Ast parseLatex(const std::string& latex, Mode mode, Diagnostics& diagnostics, bool lineBreaks,
               bool paragraphs, std::uint32_t bodyStart, std::uint32_t bodyEnd, bool userMacros,
               const std::vector<std::string>& packages, std::shared_ptr<const UserScope> inherit) {
  ExpanderOptions eo;
  eo.inherit = std::move(inherit);
  eo.prelude = true;
  eo.packages = packages;
  eo.persistent = userMacros;
  eo.recover = true;
  eo.lex.blankLineIsPar = paragraphs;
  eo.isBuiltinCommand = [](const std::string& name) { return findCommand(name) != nullptr; };
  eo.isBuiltinEnvironment = [](const std::string& name) {
    return findEnvironment(name) != nullptr;
  };
  eo.isKnownName = [](const std::string& name) {
    return Symbol::get(name.c_str()) != nullptr || Formula::isPredefined(name);
  };
  eo.isLoweringLength = [](const std::string& name) { return Lengths::builtin(name); };
  Expander expander(latex, std::move(eo), diagnostics);
  ParserOptions po;
  po.startMode = mode;
  po.lineEndsBreak = lineBreaks;
  po.parBreaks = paragraphs;
  po.bodyStart = bodyStart;
  po.bodyEnd = bodyEnd;
  po.isKnownName = [](const std::string& name) {
    return Symbol::get(name.c_str()) != nullptr || Formula::isPredefined(name);
  };
  Ast ast;
  Parser(expander, ast, diagnostics, std::move(po)).parse();
  ast.environmentKinds = expander.environmentKinds();
  ast.declared = expander.declared();
  return ast;
}

namespace {

Diagnostics& lastStore() {
  static Diagnostics last;
  return last;
}

std::vector<std::string>& startPackagesStore() {
  static std::vector<std::string> packages;
  return packages;
}

/** A byte range of the input: [first, second). */
using BodyRange = std::pair<std::uint32_t, std::uint32_t>;

// Where the verbatim text that starts at `s[i]` (a backslash) ends: after its
// \end{verbatim}, or after the closing delimiter of a \verb. `i` when none
// starts there. An unclosed one runs to the end of the input; a \verb to the
// end of its line.
std::size_t verbatimEnd(const std::string& s, std::size_t i) {
  // The environments whose body is read as typed (spec.cpp); one whose commands act (alltt) ends a document as any does.
  static const std::vector<std::string> names = [] {
    std::vector<std::string> opens;
    for (const std::string& name : verbatimEnvironmentNames()) opens.push_back("\\begin{" + name + "}");
    return opens;
  }();
  for (const std::string& open : names) {
    if (s.compare(i, open.size(), open) != 0) continue;
    const std::string close = "\\end{" + open.substr(7);
    const std::size_t at = s.find(close, i + open.size());
    return at == std::string::npos ? s.size() : at + close.size();
  }
  // \verb, and fancyvrb's and listings' inline forms (without options before the delimiter).
  std::size_t j = i;
  for (const char* name : {"\\verb", "\\Verb", "\\lstinline"}) {
    const std::size_t n = std::strlen(name);
    if (s.compare(i, n, name) == 0) {
      j = i + n;
      break;
    }
  }
  if (j == i) return i;
  if (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])) != 0) return i;  // \verbatim
  if (j < s.size() && s[j] == '*') j++;
  while (j < s.size() && s[j] == ' ') j++;
  if (j >= s.size() || s[j] == '\n' || s[j] == '\r') return i;
  const char delimiter = s[j++];
  while (j < s.size() && s[j] != delimiter && s[j] != '\n' && s[j] != '\r') j++;
  return j < s.size() && s[j] == delimiter ? j + 1 : j;
}

// The part of `latex` that a LaTeX file draws: from its \begin{document}
// to the end of its \end{document}. Found as the lexer would find them --
// outside `%` comments, and at the top level, outside braces. The whole
// input when it has no \begin{document}.
BodyRange documentBody(const std::string& s) {
  static const std::string begin = "\\begin{document}";
  static const std::string end = "\\end{document}";
  BodyRange body{0, UINT32_MAX};
  bool found = false;
  int depth = 0;
  for (std::size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (c == '%') {
      while (i < s.size() && s[i] != '\n') i++;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
    } else if (c == '\\') {
      // Verbatim text is no LaTeX: a \end{document} in it is its own.
      const std::size_t skip = verbatimEnd(s, i);
      if (skip != i) {
        i = skip - 1;
      } else if (depth == 0 && !found && s.compare(i, begin.size(), begin) == 0) {
        body.first = static_cast<std::uint32_t>(i);
        found = true;
        i += begin.size() - 1;
      } else if (depth == 0 && found && s.compare(i, end.size(), end) == 0) {
        body.second = static_cast<std::uint32_t>(i + end.size());
        break;
      } else {
        i++;  // what a backslash escapes: `\%`, `\{`, `\\`
      }
    }
  }
  return found ? body : BodyRange{0, UINT32_MAX};
}

}  // namespace

namespace {

// Where warnLater() puts what it is told: the parse's own diagnostics while it runs (boxes made during the lowering), the
// last parse's after.
Diagnostics*& laterSink() {
  static Diagnostics* sink = nullptr;
  return sink;
}

}  // namespace

void warnLater(const SourceSpan& at, const std::string& message) {
  if (Diagnostics* d = laterSink() != nullptr ? laterSink() : &lastStore()) d->warnOnce(at, message);
}

void buildModern(const std::string& latex, InputMode mode, Formula& formula) {
  lastStore() = Diagnostics();
  Diagnostics diags;
  struct Sink {
    explicit Sink(Diagnostics* d) { laterSink() = d; }
    ~Sink() { laterSink() = nullptr; }
  } sink(&diags);
  const bool mixed = mode == InputMode::mixed;
  const bool document = mode == InputMode::document;
  // Both prose modes start in text and are laid out as rows; they differ in
  // what breaks a row -- a line end in mixed, a blank line in a document.
  const bool prose = mixed || document;
  // A whole LaTeX file: its preamble is read for its definitions and not
  // drawn, as LaTeX does, and what follows \end{document} is not read.
  // Package settings a grob cannot honour say nothing about it.
  const BodyRange body = prose ? documentBody(latex) : BodyRange{0, UINT32_MAX};
  std::vector<std::string> packages;
  packages.swap(startPackagesStore());
  const Ast ast = parseLatex(latex, prose ? Mode::text : Mode::math, diags, mixed, document,
                             body.first, body.second, true, packages);
  // A capacity ran out: what was read is not what the input means.
  if (const Diagnostic* e = diags.firstError()) throw ex_parse(e->message);
  lowerInto(ast, formula, diags, prose, document);
  lastStore() = std::move(diags);
}

const Diagnostics& lastDiagnostics() {
  return lastStore();
}

void setStartPackages(std::vector<std::string> packages) {
  startPackagesStore() = std::move(packages);
}

namespace {

ImageResolver& resolverStore() {
  static ImageResolver resolver;
  return resolver;
}

}  // namespace

void setImageResolver(ImageResolver resolver) {
  resolverStore() = std::move(resolver);
}

const ImageResolver& imageResolver() {
  return resolverStore();
}

namespace {

FontResolver& fontResolverStore() {
  static FontResolver resolver;
  return resolver;
}

}  // namespace

void setFontResolver(FontResolver resolver) {
  fontResolverStore() = std::move(resolver);
}

const FontResolver& fontResolver() {
  return fontResolverStore();
}

namespace {

BoxMeasurer& boxMeasurerStore() {
  static BoxMeasurer measurer;
  return measurer;
}

}  // namespace

void setBoxMeasurer(BoxMeasurer measurer) {
  boxMeasurerStore() = std::move(measurer);
}

const BoxMeasurer& boxMeasurer() {
  return boxMeasurerStore();
}

}  // namespace microtex::front
