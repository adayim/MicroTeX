#include "front/front.h"

#include <utility>

#include "core/formula.h"
#include "front/diagnostics.h"
#include "front/expander.h"
#include "front/hooks.h"
#include "front/lower.h"
#include "front/parser.h"
#include "front/spec.h"
#include "macro/macro.h"
#include "unimath/uni_symbol.h"
#include "utils/exceptions.h"

namespace microtex::front {

Ast parseLatex(const std::string& latex, Mode mode, Diagnostics& diagnostics, bool lineBreaks,
               bool paragraphs, std::uint32_t bodyStart, std::uint32_t bodyEnd, bool userMacros) {
  ExpanderOptions eo;
  eo.prelude = true;
  eo.persistent = userMacros;
  eo.recover = true;
  eo.lex.blankLineIsPar = paragraphs;
  eo.isBuiltinCommand = [](const std::string& name) { return findCommand(name) != nullptr; };
  eo.isBuiltinEnvironment = [](const std::string& name) {
    return findEnvironment(name) != nullptr;
  };
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
  return ast;
}

namespace {

Diagnostics& lastStore() {
  static Diagnostics last;
  return last;
}

/** A byte range of the input: [first, second). */
using BodyRange = std::pair<std::uint32_t, std::uint32_t>;

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
      if (depth == 0 && !found && s.compare(i, begin.size(), begin) == 0) {
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

void buildModern(const std::string& latex, InputMode mode, Formula& formula) {
  lastStore() = Diagnostics();
  Diagnostics diags;
  const bool mixed = mode == InputMode::mixed;
  const bool document = mode == InputMode::document;
  // Both prose modes start in text and are laid out as rows; they differ in
  // what breaks a row -- a line end in mixed, a blank line in a document.
  const bool prose = mixed || document;
  // A whole LaTeX file: its preamble is read for its definitions and not
  // drawn, as LaTeX does, and what follows \end{document} is not read.
  // Package settings a grob cannot honour say nothing about it.
  const BodyRange body = prose ? documentBody(latex) : BodyRange{0, UINT32_MAX};
  const Ast ast = parseLatex(latex, prose ? Mode::text : Mode::math, diags, mixed, document,
                             body.first, body.second);
  // A capacity ran out: what was read is not what the input means.
  if (const Diagnostic* e = diags.firstError()) throw ex_parse(e->message);
  lowerInto(ast, formula, diags, prose, document);
  lastStore() = std::move(diags);
}

const Diagnostics& lastDiagnostics() {
  return lastStore();
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

}  // namespace microtex::front
