#include "front/spec.h"

#include <initializer_list>
#include <set>
#include <unordered_map>

namespace microtex::front {

namespace {

/**
 * Arguments in source order, one letter each:
 *   m math   t text   c current mode   r raw   d dimension   l delimiter
 *   u url   v verb
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
      case 'v': kind = ArgKind::verb; break;
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
           Shape shape = Shape::prefix, Bare bare = Bare::none, bool special = false,
           Role role = Role::none) {
    CommandSpec spec;
    spec.role = role;
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

  /** Sets `member` on environments already registered. */
  void mark(std::initializer_list<const char*> names, bool EnvSpec::*member) {
    for (const char* name : names) envs.at(name).*member = true;
  }

  Table() {
    // --- in the order of lib/macro/macro_def.cpp -------------------------
    add({"rule"}, "Ddd");
    // Two option groups: graphicx's [key=value], and the older
    // [llx,lly][urx,ury] spelling.
    add({"includegraphics"}, "RRu");
    // The directories later images are looked for in; the lowering keeps them.
    add({"graphicspath", "gmarraystretch"}, "r");
    // The rows of the array that comes next: jot (amsmath's, \openup\jot) or
    // sub (\substack's), as the prelude's split and substack say.
    add({"gmrows"}, "r");
    // `\extracolsep{len}` in an @ column, kept for the matrix to read: it widens the gaps between the columns after it.
    add({"gmextracolsep"}, "r");
    // The code of an alltt or a Verbatim with commandchars, as the parser puts it back (Parser::commandLines()): one
    // \gmcodeline a line, and in it the text between the commands as \gmlit{hex}, which is set as typed.
    add({"gmlit"}, "r");
    add({"gmcodeline"}, "t");
    // The cell a tblr's `cmd=` is applied to: `\cmd{\gmcell}`.
    add({"gmcell"}, "");
    add({"cfrac"}, "Rmm");
    add({"xleftarrow", "xrightarrow", "xleftrightarrow", "xRightarrow", "xLeftarrow",
         "xLeftrightarrow", "xhookleftarrow", "xhookrightarrow", "xmapsto",
         "xrightharpoondown", "xrightharpoonup", "xleftharpoondown", "xleftharpoonup",
         "xrightleftharpoons", "xleftrightharpoons", "xtwoheadrightarrow", "xtwoheadleftarrow",
         "xlongequal", "xtofrom"},
        "Cc");
    add({"sqrt"}, "Mm");
    add({"smash"}, "Rm");
    add({"vcenter", "textcircled"}, "c");
    add({"TextOrMath"}, "cc");
    add({"overlinesegment", "underlinesegment", "angl", "phase", "overleftharpoon", "overrightharpoon",
         "Overrightarrow"},
        "m");
    add({"mathchoice"}, "mmmm");
    add({"hdotsfor"}, "Rr");
    add({"stackbin", "stackrel"}, "Mmm");
    add({"rotatebox"}, "Rrm");
    add({"scalebox"}, "rRc");
    add({"raisebox"}, "dDDc");
    add({"mathversion"}, "Rr");
    add({"fatalIfCmdConflict", "breakEverywhere"}, "r");
    add({"makeatletter", "makeatother"}, "");
    // Their content is a cell's: text in a tabular met in text.
    add({"multicolumn"}, "rrc");
    // \multirow{rows}[bigstruts]{width}[vmove]{text}: the two options set
    // where the text sits against its rows, which it is not set to.
    add({"multirow"}, "rRrRc");
    // Rules end the row they are in, as they did in the old parser.
    add({"hline", "thickhline", "hdashline"}, "", Shape::prefix, Bare::none, true, Role::rule);
    add({"cline"}, "r", Shape::prefix, Bare::none, true, Role::rule);
    // booktabs' rule of a given thickness, and the space above and below.
    add({"specialrule"}, "ddd", Shape::prefix, Bare::none, true, Role::rule);
    // booktabs' \addlinespace[len]: space between two rows, as a rule that
    // draws nothing.
    add({"addlinespace"}, "D", Shape::prefix, Bare::none, true, Role::rule);
    // A longtable's markers: each ends the row it is in, as a rule does, and
    // names the group of rows that ends there.
    add({"endhead", "endfirsthead", "endfoot", "endlastfoot"}, "", Shape::prefix, Bare::none,
        true, Role::rule);
    add({"rowcolor", "columncolor", "arrayrulecolor", "cellcolor"}, "r");
    // xcolor's table option: \rowcolors[commands]{first row}{odd colour}{even colour}, for the tables after it.
    add({"rowcolors"}, "Rrrr");
    add({"newcolumntype"}, "rr");
    add({"color"}, "r", Shape::groupDeclaration);
    // fontspec's and unicode-math's: a font for a role -- body, \textsf,
    // \texttt, math -- from here to the end of the group; \fontspec and
    // NFSS's \fontfamily (with its \selectfont, which does nothing) set the
    // font of the rest of the group likewise. As in LaTeX, a font lasts across
    // a `\\`, which a markdown span with a <br> in it needs. The options are
    // fontspec's, either side.
    add({"setmainfont", "setsansfont", "setmonofont", "setmathfont", "fontspec"}, "RrR",
        Shape::groupDeclaration);
    add({"fontfamily"}, "r", Shape::groupDeclaration);
    add({"shoveright", "shoveleft"}, "m");
    add({"DeclareMathSizes"}, "rrrr");
    add({"magnification"}, "r");
    add({"tiny", "sixptsize", "scriptsize", "footnotesize", "small", "normalsize", "large", "Large",
         "LARGE", "huge", "Huge"},
        "", Shape::declaration);
    // relsize's: the size times a factor, to the end of the group
    // (\textscale{f}{text} is the prelude's).
    add({"relscale"}, "r", Shape::declaration);
    // LaTeX's \fontsize{size}{baselineskip}, which \selectfont puts in
    // force: the size, against the 10pt a grob is drawn at; the baseline
    // skip is the line spacing's, which gp sets.
    add({"fontsize"}, "rr", Shape::declaration);
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
    // Small capitals, as the lowering fakes them.
    add({"textsc"}, "t");
    add({"scshape"}, "", Shape::declaration);
    // The date, as LaTeX prints it (`May 16, 2026`), which \date's default is.
    add({"today"}, "");
    add({"href"}, "uc");
    // Its argument is its own delimited text (the parser reads it).
    add({"verb"}, "v");
    // The inline code of listings, minted and fancyvrb: options and the language come first, then the code between
    // a delimiter (or braces), which the parser reads as \verb's.
    add({"lstinline", "Verb"}, "Rv");
    add({"mintinline"}, "Rrv");
    // booktabs' \cmidrule, which the parser reads as \cline.
    add({"cmidrule"}, "r", Shape::prefix, Bare::none, true, Role::rule);
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
         "boldsymbol", "bm", "pmb", "symbf"},
        "c");
    add({"mbox", "text", "textit", "textbf", "textsf", "texttt", "textrm", "textnormal"}, "t");
    // \intertext ends the row it is in, as a rule does.
    add({"intertext"}, "t", Shape::prefix, Bare::none, true);
    add({"^", "'", "\"", "`", "=", ".", "~", "t", "u", "v", "r", "H"}, "m");
    add({"not", "hat", "widehat", "widecheck", "check", "tilde", "widetilde", "acute", "grave", "dot",
         "ddot", "dddot", "ddddot", "breve", "bar", "vec", "mathring", "undertilde", "utilde"},
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
    add({"colorlet"}, "rr");
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
    // TeX's skips, read as \kern is: the glue's stretch and shrink have
    // nothing to act on in a grob, so a `plus` or `minus` part is not read.
    add({"kern", "mkern", "hskip", "mskip"}, "", Shape::prefix, Bare::dimen);
    add({"char"}, "", Shape::prefix, Bare::number);
    add({"roman", "Roman"}, "r");
    add({"surd", "lmoustache", "rmoustache", "-", "nbsp", "joinrel", "underscore",
         "sp@breve", "nokern"},
        "");
    add({"st"}, "c");
    add({"longdiv"}, "rr");
    add({"cancel", "bcancel", "xcancel", "sqrtsign"}, "m");
    // cancel's \cancelto{value}{base}.
    add({"cancelto"}, "mm");
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
    add({"centering", "raggedleft", "raggedright"}, "", Shape::prefix, Bare::none, true, Role::lineAlignment);
    // A float's caption: a line of text, unnumbered, where it is written.
    add({"caption"}, "Rt", Shape::prefix, Bare::none, true);
    // Commands a single grob cannot carry out: there is no bibliography to
    // cite, no counter to refer to and no page to put a note on. Each is
    // read so that it warns and draws what LaTeX draws when it cannot
    // resolve one, instead of coming out red as an unknown command.
    add({"ref", "pageref", "eqref", "subref"}, "r", Shape::prefix, Bare::none, true);
    // cleveref's and hyperref's references, which name the kind of what they refer to.
    add({"cref", "Cref", "autoref"}, "r", Shape::prefix, Bare::none, true, Role::crossReference);
    add({"crefrange", "Crefrange"}, "rr", Shape::prefix, Bare::none, true, Role::crossReference);
    // The floats say what a \caption numbers: the prelude's figure, table, subfigure and subtable.
    add({"gmfloat", "gmsubfloat"}, "r");
    // multicol's \columnbreak: where the columns of a multicols are cut.
    add({"columnbreak"}, "");
    add({"cite"}, "Rr", Shape::prefix, Bare::none, true, Role::citation);
    // natbib's: \citep[post]{keys} or \citep[pre][post]{keys}.
    add({"citep", "citet", "citealp"}, "RRr", Shape::prefix, Bare::none, true, Role::citation);
    // The page notes (the prelude's \footnote and kin): the mark, then the note.
    add({"gmfootnote", "gmfootnotetext"}, "rt", Shape::prefix, Bare::none, true);
    add({"gmfootnotemark"}, "r");
    // Equation numbers: the lowering numbers the displays of an input and
    // keeps the labels, which a reference then names. The starred form of tag is
    // the expander's gmtagstar.
    add({"tag", "gmtagstar"}, "t");
    add({"notag", "nonumber"}, "");
    add({"label"}, "r");
    // thebibliography's \bibitem{key}{label}, which the prelude writes.
    add({"gmbibitem"}, "rr");
    add({"newlength"}, "r");
    add({"setlength", "addtolength"}, "rr");
    add({"setcounter", "addtocounter", "gmnewcounter"}, "rr");
    add({"stepcounter", "gmcurrentlabel", "gmlabelkind", "value", "arabic", "roman", "Roman", "alph", "Alph", "fnsymbol"}, "r");
    // amsmath's subequations, which the prelude writes with these two.
    add({"gmsubeqbegin", "gmsubeqend"}, "");
    add({"hspace", "vspace"}, "d");
    // --- our own (lib/atom/) ----------------------------------------------
    add({"gmfontfamily"}, "rt");
    add({"mark"}, "r");
    add({"gmgraphics"}, "rrr");
    // \begin and \end are read by the parser; the environment decides.
    add({"begin", "end"}, "r", Shape::prefix, Bare::none, true);

    // --- environments the engine builds (the @@env ones in macro_def.cpp) --
    env({"matrix", "smallmatrix", "align", "flalign", "multline", "gather", "eqnarray"}, "",
        EnvBody::alignment);
    // [t|b|c]: LaTeX's vertical position, read and not used (a grob has no
    // baseline of lines around it to align to).
    env({"aligned", "gathered"}, "R", EnvBody::alignment);
    env({"alignat"}, "r", EnvBody::alignment);
    // The cases package's numbered cases: {left side}, then rows of a case
    // and its explanation, each with a number (a lettered one in subnumcases).
    env({"numcases", "subnumcases"}, "r", EnvBody::alignment);
    envs["numcases"].textAfterFirst = true;
    envs["subnumcases"].textAfterFirst = true;
    env({"array", "alignedat"}, "Rr", EnvBody::alignment);
    // As array, but its cells are text, as LaTeX's are.
    env({"tabular"}, "Rr", EnvBody::alignment, true);
    // With a width: tabular* spreads its columns across it, tabularx's X
    // columns share it; longtable is a table too long for a page, which a
    // grob has not got.
    env({"tabular*", "tabularx"}, "rr", EnvBody::alignment, true);
    env({"longtable"}, "Rr", EnvBody::alignment, true);
    // tabularray's tables, as tinytable writes them: [options]{spec}.
    env({"tblr", "talltblr", "longtblr"}, "Rr", EnvBody::alignment, true);
    env({"itemize", "enumerate", "description", "tablenotes"}, "", EnvBody::raw, true);
    env({"verbatim", "verbatim*"}, "", EnvBody::verbatim, true);
    // The code of listings (`[options]`), fancyvrb and minted (`[options]{language}`): set as verbatim is.
    env({"lstlisting", "Verbatim", "Verbatim*"}, "R", EnvBody::verbatim, true);
    env({"minted"}, "Rr", EnvBody::verbatim, true);
    // alltt: verbatim whose backslash and braces keep their meaning, so its commands act.
    env({"alltt"}, "", EnvBody::verbatim, true);
    // The comment package's: the body is skipped.
    env({"comment"}, "", EnvBody::verbatim, true);
    // article's quote, quotation and verse: a body of paragraphs between margins.
    env({"quote", "quotation", "verse"}, "", EnvBody::text);
    // Commutative diagrams: amscd's, and tikz-cd's, which a handler reads
    // whole (its options and arrows are in the body).
    env({"CD", "tikzcd", "tikzpicture"}, "", EnvBody::raw);
    // [position][height][inner position]{width}: a box of paragraphs.
    env({"minipage"}, "RDRd", EnvBody::text);
    // [keys]: tcolorbox's framed box of paragraphs, with the keys that change how it is drawn.
    env({"tcolorbox", "mdframed"}, "R", EnvBody::text);
    // {columns}[header]: multicol's balanced columns.
    env({"multicols"}, "rR", EnvBody::text);

    // --- what the lowering does with them ----------------------------------
    mark({"itemize", "enumerate", "description", "tablenotes", "verbatim", "verbatim*", "lstlisting", "Verbatim",
          "Verbatim*", "minted", "alltt", "tcolorbox", "mdframed", "multicols", "quote", "quotation", "verse"},
         &EnvSpec::apart);
    mark({"comment"}, &EnvSpec::dropped);
    mark({"alltt"}, &EnvSpec::commandsAct);
    mark({"tblr", "talltblr", "longtblr"}, &EnvSpec::tblr);
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

bool isVerbatimEnvironment(const std::string& name) {
  const EnvSpec* spec = findEnvironment(name);
  return spec != nullptr && spec->body == EnvBody::verbatim && !spec->dropped;
}

bool isTextTable(const std::string& name) {
  const EnvSpec* spec = findEnvironment(name);
  return spec != nullptr && spec->body == EnvBody::alignment && spec->textInText && !spec->tblr;
}

bool isSetApart(const std::string& name) {
  const EnvSpec* spec = findEnvironment(name);
  return spec != nullptr && spec->apart;
}

std::vector<std::string> verbatimEnvironmentNames() {
  std::vector<std::string> names;
  for (const auto& kv : table().envs) {
    if (kv.second.body == EnvBody::verbatim && !kv.second.commandsAct) names.push_back(kv.first);
  }
  return names;
}

static bool hasRole(const std::string& name, Role role) {
  const CommandSpec* spec = findCommand(name);
  return spec != nullptr && spec->role == role;
}

bool isCrossReference(const std::string& name) {
  return hasRole(name, Role::crossReference);
}

bool isCitation(const std::string& name) {
  return hasRole(name, Role::citation);
}

bool isRule(const std::string& name) {
  return hasRole(name, Role::rule);
}

bool isLineAlignment(const std::string& name) {
  return hasRole(name, Role::lineAlignment);
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
