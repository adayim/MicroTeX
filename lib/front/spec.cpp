#include "front/spec.h"

#include <initializer_list>
#include <set>
#include <unordered_map>

namespace microtex::front {

namespace {

/**
 * Arguments in source order, one letter each:
 *   m math   t text   c current mode   r raw   d dimension   l delimiter
 *   u url
 * An upper-case letter is an optional `[...]` argument of that kind.
 *
 * The kinds follow what each engine command does with its argument (see
 * lib/macro/): most parse it as math even inside \text, some in the mode
 * they are used in, and a few as text.
 */
std::vector<ArgSpec> parseArgs(const char* code) {
  std::vector<ArgSpec> args;
  for (const char* p = code; *p != '\0'; ++p) {
    const char c = *p;
    const bool optional = c >= 'A' && c <= 'Z';
    ArgKind kind = ArgKind::raw;
    switch (optional ? static_cast<char>(c - 'A' + 'a') : c) {
      case 'm': kind = ArgKind::math; break;
      case 't': kind = ArgKind::text; break;
      case 'c': kind = ArgKind::current; break;
      case 'r': kind = ArgKind::raw; break;
      case 'd': kind = ArgKind::dimen; break;
      case 'l': kind = ArgKind::delim; break;
      case 'u': kind = ArgKind::url; break;
      default: break;
    }
    args.push_back({kind, optional});
  }
  return args;
}

struct Table {
  std::unordered_map<std::string, CommandSpec> commands;
  std::unordered_map<std::string, EnvSpec> envs;

  void add(std::initializer_list<const char*> names, const char* args,
           Shape shape = Shape::prefix, Bare bare = Bare::none, bool special = false) {
    CommandSpec spec;
    spec.shape = shape;
    spec.args = parseArgs(args);
    spec.bare = bare;
    spec.special = special;
    for (const char* name : names) commands[name] = spec;
  }

  void env(std::initializer_list<const char*> names, const char* args, EnvBody body,
           bool textInText = false) {
    EnvSpec spec;
    spec.args = parseArgs(args);
    spec.body = body;
    spec.textInText = textInText;
    for (const char* name : names) envs[name] = spec;
  }

  Table() {
    // --- in the order of lib/macro/macro_def.cpp -------------------------
    add({"rule"}, "Ddd");
    // Two option groups: graphicx's [key=value], and the older
    // [llx,lly][urx,ury] spelling.
    add({"includegraphics"}, "RRu");
    // The directories later images are looked for in; the lowering keeps them.
    add({"graphicspath"}, "r");
    add({"cfrac"}, "Rmm");
    add({"xleftarrow", "xrightarrow", "xleftrightarrow", "xRightarrow", "xLeftarrow",
         "xLeftrightarrow", "xhookleftarrow", "xhookrightarrow", "xmapsto",
         "xrightharpoondown", "xrightharpoonup", "xleftharpoondown", "xleftharpoonup",
         "xrightleftharpoons", "xleftrightharpoons"},
        "Cc");
    add({"sqrt"}, "Mm");
    add({"smash"}, "Rm");
    add({"hdotsfor"}, "Rr");
    add({"stackbin", "stackrel"}, "Mmm");
    add({"rotatebox"}, "Rrm");
    add({"scalebox"}, "rRc");
    add({"raisebox"}, "dDDc");
    add({"mathversion"}, "Rr");
    add({"fatalIfCmdConflict", "breakEverywhere"}, "r");
    add({"makeatletter", "makeatother"}, "");
    // Their content is a cell's: text in a tabular met in text.
    add({"multicolumn", "multirow"}, "rrc");
    // Rules end the row they are in, as they did in the old parser.
    add({"hline", "thickhline"}, "", Shape::prefix, Bare::none, true);
    add({"cline"}, "r", Shape::prefix, Bare::none, true);
    // booktabs' rule of a given thickness, and the space above and below.
    add({"specialrule"}, "ddd", Shape::prefix, Bare::none, true);
    add({"rowcolor", "columncolor", "arrayrulecolor", "cellcolor"}, "r");
    add({"newcolumntype"}, "rr");
    add({"color"}, "r", Shape::groupDeclaration);
    add({"shoveright", "shoveleft"}, "m");
    add({"DeclareMathSizes"}, "rrrr");
    add({"magnification"}, "r");
    add({"tiny", "scriptsize", "footnotesize", "small", "normalsize", "large", "Large",
         "LARGE", "huge", "Huge"},
        "", Shape::declaration);
    // relsize's: the size times a factor, to the end of the group
    // (\textscale{f}{text} is the prelude's).
    add({"relscale"}, "r", Shape::declaration);
    add({"big", "Big", "bigg", "Bigg", "bigl", "Bigl", "biggl", "Biggl", "bigr", "Bigr",
         "biggr", "Biggr"},
        "l");
    add({"frac", "binom"}, "mm");
    add({"genfrac"}, "rrrrmm");
    add({"over", "atop", "choose", "brace", "brack", "bangle"}, "", Shape::infix);
    add({"above"}, "", Shape::infix, Bare::dimen);
    add({"overwithdelims", "atopwithdelims"}, "ll", Shape::infix);
    add({"abovewithdelims"}, "ll", Shape::infix, Bare::dimen);
    add({"sideset", "prescript"}, "mmm");
    add({"overrightarrow", "overleftarrow", "overleftrightarrow", "underrightarrow",
         "underleftarrow", "underleftrightarrow", "overbrace", "overbracket", "overparen",
         "underbrace", "underbracket", "underparen", "overline", "Braket", "Set"},
        "m");
    // A box in both modes, as LaTeX's: text in text, math in math.
    add({"underline"}, "c");
    // \( and \[ open math; \left, \middle and \right delimit a group.
    add({"(", "["}, "", Shape::prefix, Bare::none, true);
    add({"ensuremath"}, "m", Shape::prefix, Bare::none, true);
    // Links: the look of one, as hyperref's colorlinks and the url package
    // set it (the lowering builds it). A URL reads its specials as text.
    add({"url"}, "u");
    add({"href"}, "uc");
    // booktabs' \cmidrule, which the parser reads as \cline.
    add({"cmidrule"}, "r", Shape::prefix, Bare::none, true);
    add({"left", "middle", "right"}, "l", Shape::prefix, Bare::none, true);
    add({"mathop", "mathpunct", "mathord", "mathrel", "mathinner", "mathbin", "mathopen",
         "mathclose"},
        "m");
    add({"bf", "it", "rm", "sf", "tt"}, "", Shape::declaration);
    // Bold math for the rest of the group; text in it is left as it is.
    add({"boldmath"}, "", Shape::declaration);
    // TeX's old font switches: \cal and \frak are \mathcal and \mathfrak
    // for the rest of the group.
    add({"cal", "frak"}, "", Shape::declaration);
    add({"oldstylenums"}, "t");
    add({"mathnormal", "mathrm", "mathbf", "mathit", "mathcal", "mathscr", "mathfrak",
         "mathbb", "mathsf", "mathtt", "mathbfit", "mathbfcal", "mathbffrak", "mathsfbf",
         "mathbfsf", "mathsfit", "mathsfbfit", "mathbfsfit", "Bbb", "mathds", "bold",
         "boldsymbol", "bm", "pmb"},
        "c");
    add({"mbox", "text", "textit", "textbf", "textsf", "texttt", "textrm"}, "t");
    // \intertext ends the row it is in, as a rule does.
    add({"intertext"}, "t", Shape::prefix, Bare::none, true);
    add({"^", "'", "\"", "`", "=", ".", "~", "t", "u", "v", "r"}, "m");
    add({"not", "hat", "widehat", "check", "tilde", "widetilde", "acute", "grave", "dot",
         "ddot", "dddot", "ddddot", "breve", "bar", "vec", "mathring", "undertilde"},
        "m");
    // The first argument names an accent (`\underaccent{\dot}{x}`): a
    // command used without its argument, so it is kept as text.
    add({"accentset", "underaccent"}, "rm");
    add({"overset", "underset"}, "mm");
    add({"displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle"}, "",
        Shape::declaration);
    for (const char* style : {"displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle"}) {
      commands[style].body = ArgKind::math;
    }
    add({"everymath"}, "r");
    add({"dnomstyle", "numstyle", "substyle", "supstyle"}, "m");
    add({"definecolor"}, "rrr");
    add({"fgcolor", "bgcolor", "textcolor", "colorbox"}, "rc");
    add({"fcolorbox"}, "rrc");
    add({",", ":", ";", "thinspace", "medspace", "thickspace", "!", "negthinspace",
         "negmedspace", "negthickspace", "quad"},
        "");
    add({"reflectbox", "shadowbox", "ovalbox", "doublebox", "fbox", "boxed"}, "m");
    add({"resizebox"}, "rrm");
    add({"cornersize"}, "r");
    add({"llap", "rlap", "clap", "mathllap", "mathrlap", "mathclap"}, "m");
    add({"nolimits", "limits", "normal"}, "", Shape::postfix);
    add({"kern"}, "", Shape::prefix, Bare::dimen);
    add({"char"}, "", Shape::prefix, Bare::number);
    add({"roman", "Roman"}, "r");
    add({"surd", "lmoustache", "rmoustache", "-", "nbsp", "joinrel", "underscore",
         "sp@breve", "nokern"},
        "");
    add({"st"}, "c");
    add({"longdiv"}, "rr");
    add({"cancel", "bcancel", "xcancel", "sqrtsign"}, "m");
    // ulem's strike-out and underline, which break with their text, and the
    // phantoms: text in text, math in math, as in LaTeX.
    add({"sout", "uline", "phantom", "hphantom", "vphantom"}, "c");
    add({"stackinset"}, "rdrdmm");
    // Row ends, and a line break outside an array.
    add({"cr", "\\"}, "", Shape::prefix, Bare::none, true);
    // A paragraph in document mode, a line break anywhere else, as the
    // prelude used to define it. The parser reads it: only it knows the mode.
    add({"par"}, "", Shape::prefix, Bare::none, true);
    // Headings, and the one command that says a paragraph is not indented.
    // The parser reads the headings: each takes a `*` that turns off its
    // number, which the old parser would have taken for an argument.
    add({"section", "subsection", "subsubsection", "paragraph"}, "Rt", Shape::prefix, Bare::none,
        true);
    add({"noindent"}, "", Shape::prefix, Bare::none, true);
    // Centres the lines of a document until the end of its group.
    add({"centering", "raggedleft", "raggedright"}, "", Shape::prefix, Bare::none, true);
    // A float's caption: a line of text, unnumbered, where it is written.
    add({"caption"}, "Rt", Shape::prefix, Bare::none, true);
    // Commands a single grob cannot carry out: there is no bibliography to
    // cite, no counter to refer to and no page to put a note on. Each is
    // read so that it warns and draws what LaTeX draws when it cannot
    // resolve one, instead of coming out red as an unknown command.
    add({"ref", "pageref", "eqref"}, "r", Shape::prefix, Bare::none, true);
    add({"cite"}, "Rr", Shape::prefix, Bare::none, true);
    // natbib's: \citep[post]{keys} or \citep[pre][post]{keys}.
    add({"citep", "citet", "citealp"}, "RRr", Shape::prefix, Bare::none, true);
    add({"footnote"}, "Rt", Shape::prefix, Bare::none, true);
    add({"hspace", "vspace"}, "d");
    // --- our own (lib/atom/) ----------------------------------------------
    add({"gmfontfamily"}, "rt");
    add({"mark"}, "r");
    add({"gmgraphics"}, "rrr");
    // \begin and \end are read by the parser; the environment decides.
    add({"begin", "end"}, "r", Shape::prefix, Bare::none, true);

    // --- environments the engine builds (the @@env ones in macro_def.cpp) --
    env({"matrix", "smallmatrix", "align", "flalign", "aligned", "multline", "gather",
         "gathered"},
        "", EnvBody::alignment);
    env({"array", "alignat", "alignedat"}, "r", EnvBody::alignment);
    // As array, but its cells are text, as LaTeX's are.
    env({"tabular"}, "r", EnvBody::alignment, true);
    env({"itemize", "enumerate"}, "", EnvBody::raw, true);
    // [position][height][inner position]{width}: a box of paragraphs.
    env({"minipage"}, "RDRd", EnvBody::text);
  }
};

const Table& table() {
  static const Table t;
  return t;
}

}  // namespace

int headingLevel(const std::string& name) {
  if (name == "section") return 0;
  if (name == "subsection") return 1;
  if (name == "subsubsection") return 2;
  if (name == "paragraph") return 3;
  return -1;
}

bool isHeading(const std::string& name) {
  return headingLevel(name) >= 0;
}

bool isHeadingLine(const std::string& name) {
  const int level = headingLevel(name);
  return level >= 0 && level < 3;
}

bool isFloatEnvironment(const std::string& name) {
  return name == "table" || name == "table*" || name == "figure" || name == "figure*";
}

bool isBlockEnvironment(const std::string& name) {
  return name == "abstract" || name == "thebibliography";
}

bool isCitation(const std::string& name) {
  return name == "cite" || name == "citep" || name == "citet" || name == "citealp";
}

bool isRule(const std::string& name) {
  return name == "hline" || name == "thickhline" || name == "cline" || name == "specialrule";
}

bool isLineAlignment(const std::string& name) {
  return name == "centering" || name == "raggedleft" || name == "raggedright";
}

bool isDisplayEnvironment(const std::string& name) {
  static const std::set<std::string> display = {
    // Built by the engine.
    "align", "alignat", "flalign", "gather", "multline",
    // Written in LaTeX by the prelude.
    "displaymath", "equation", "eqnarray"};
  const bool starred = name.size() > 1 && name.back() == '*';
  return display.count(starred ? name.substr(0, name.size() - 1) : name) > 0;
}

const CommandSpec* findCommand(const std::string& name) {
  const auto& c = table().commands;
  const auto it = c.find(name);
  return it == c.end() ? nullptr : &it->second;
}

const EnvSpec* findEnvironment(const std::string& name) {
  const auto& e = table().envs;
  const auto it = e.find(name);
  return it == e.end() ? nullptr : &it->second;
}

std::vector<std::string> commandNames() {
  std::vector<std::string> names;
  for (const auto& kv : table().commands) names.push_back(kv.first);
  return names;
}

std::vector<std::string> environmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : table().envs) names.push_back(kv.first);
  return names;
}

}  // namespace microtex::front
