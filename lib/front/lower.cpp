#include "front/lower.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "atom/atom.h"
#include "atom/atom_basic.h"
#include "atom/atom_char.h"
#include "atom/atom_delim.h"
#include "atom/atom_fence.h"
#include "atom/atom_font.h"
#include "atom/atom_frac.h"
#include "atom/atom_matrix.h"
#include "atom/atom_misc.h"
#include "atom/atom_operator.h"
#include "atom/atom_row.h"
#include "atom/atom_scripts.h"
#include "atom/atom_space.h"
#include "atom/atom_text.h"
#include "atom/atom_vrow.h"
#include "atom/font_family_atom.h"
#include "box/box_factory.h"
#include "core/localized_num.h"
#include "env/units.h"
#include "front/front.h"
#include "front/hooks.h"
#include "front/spec.h"
#include "front/tblr.h"
#include "graphic/graphic.h"
#include "macro/macro.h"
#include "macro/macro_args.h"
#include "macro/macro_styles.h"
#include "unimath/uni_font.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

namespace microtex::front {

namespace {

bool isTextFont(const std::string& n) {
  return n == "bf" || n == "it" || n == "rm" || n == "sf" || n == "tt";
}

float sizeFactor(const std::string& n) {
  if (n == "tiny") return 0.5f;
  if (n == "sixptsize") return 0.6f;
  if (n == "scriptsize") return 0.7f;
  if (n == "footnotesize") return 0.8f;
  if (n == "small") return 0.9f;
  if (n == "large") return 1.2f;
  if (n == "Large") return 1.4f;
  if (n == "LARGE") return 1.8f;
  if (n == "huge") return 2.f;
  if (n == "Huge") return 2.5f;
  return 1.f;  // normalsize
}

bool isSize(const std::string& n) {
  return n == "tiny" || n == "sixptsize" || n == "scriptsize" || n == "footnotesize" || n == "small" ||
         n == "normalsize" || n == "large" || n == "Large" || n == "LARGE" || n == "huge" ||
         n == "Huge";
}

/** `a` at a size a declaration (\large) sets: text that still breaks. */
sptr<Atom> sized(const sptr<Atom>& a, float factor) {
  auto s = sptrOf<ScaleAtom>(a, factor);
  s->_declaration = true;
  return s;
}

std::size_t codepoints(const std::string& s) {
  std::size_t n = 0;
  for (std::size_t i = 0; i < s.size();) {
    int len = 1;
    nextUnicode(s, static_cast<int>(i), len);
    i += static_cast<std::size_t>(len > 0 ? len : 1);
    n++;
  }
  return n;
}

/** The directories of the last \graphicspath in the input being lowered.
 *  One input is lowered by several Lowerers -- a list's items and every
 *  fragment (a raw argument, a column's @{...}) get their own -- and all of
 *  them look for images where the input says. */
std::vector<std::string>* graphicsDirs = nullptr;

/** Holds graphicsDirs for as long as it lives, unless an outer one already
 *  does: each Lowerer has one, so the outermost Lowerer's lives for the
 *  whole input and no input sees another's. */
class GraphicsDirs {
public:
  GraphicsDirs() : _outer(graphicsDirs) {
    if (_outer == nullptr) graphicsDirs = &_dirs;
  }
  ~GraphicsDirs() { graphicsDirs = _outer; }
  GraphicsDirs(const GraphicsDirs&) = delete;
  GraphicsDirs& operator=(const GraphicsDirs&) = delete;

private:
  std::vector<std::string>* _outer;
  std::vector<std::string> _dirs;
};

/** `s` without the space at either end. */
std::string trimmed(std::string s) {
  return trim(s);
}

/** The equation numbers and labels of the input being lowered. As with the
 *  image directories, one input's are shared by every Lowerer it takes, so
 *  a display in a list's item is counted with the rest. */
struct Numbering {
  /** The equation counter. */
  int equation = 0;
  /** A theorem counter, and the section it was last stepped in: it starts
   *  again from 1 in the next. */
  struct Counter {
    int value = 0;
    std::string where;
  };
  std::map<std::string, Counter> counters;
  /** What a \label names now, as \@currentlabel: the number of the last
   *  equation or heading. */
  std::string current;
  /** label -> the text a reference to it draws, as LaTeX source. Shared
   *  with the references, which draw only when the whole input has been
   *  read: one may come before its label. */
  std::shared_ptr<std::map<std::string, std::string>> labels =
    std::make_shared<std::map<std::string, std::string>>();
};

Numbering* numbering = nullptr;

/** Where the next input starts counting (setStartNumbering()), and where the
 *  last one ended (lastNumbering()). */
NumberingState& pendingNumbering() {
  static NumberingState state;
  return state;
}

NumberingState& finalNumbering() {
  static NumberingState state;
  return state;
}

class NumberingScope {
public:
  NumberingScope() : _outer(numbering) {
    if (_outer == nullptr) numbering = &_own;
  }
  ~NumberingScope() { numbering = _outer; }
  NumberingScope(const NumberingScope&) = delete;
  NumberingScope& operator=(const NumberingScope&) = delete;

  /** Whether this is the Lowerer that takes the whole input. */
  bool outermost() const { return _outer == nullptr; }

private:
  Numbering* _outer;
  Numbering _own;
};

/** What the input says of its fonts as a whole: the math font a \setmathfont
 *  named, and the fonts of the roles its preamble set (see
 *  atom/font_family_atom.h). As with the numbering, one input's are shared by
 *  every Lowerer it takes, and the outermost one copies them to the Formula. */
struct FontDefaults {
  std::string math;
  int roles[3] = {0, 0, 0};
};

FontDefaults* fontDefaults = nullptr;

class FontDefaultsScope {
public:
  FontDefaultsScope() : _outer(fontDefaults) {
    if (_outer == nullptr) fontDefaults = &_own;
  }
  ~FontDefaultsScope() { fontDefaults = _outer; }
  FontDefaultsScope(const FontDefaultsScope&) = delete;
  FontDefaultsScope& operator=(const FontDefaultsScope&) = delete;

  bool outermost() const { return _outer == nullptr; }
  const FontDefaults& own() const { return _own; }

private:
  FontDefaults* _outer;
  FontDefaults _own;
};

/** \ref and \eqref: the text of the label they name, looked up when the
 *  atom is built into a box -- the whole input has been read by then -- and
 *  a bold ?? where there is none. */
class RefAtom : public Atom {
public:
  RefAtom(std::shared_ptr<std::map<std::string, std::string>> labels, std::string key,
          bool parentheses)
      : _labels(std::move(labels)), _key(std::move(key)), _parentheses(parentheses) {}

  sptr<Box> createBox(Env& env) override {
    const auto it = _labels->find(_key);
    sptr<Atom> text;
    if (it != _labels->end()) text = buildFragment(it->second, false);
    if (text == nullptr) {
      text = sptrOf<FontStyleAtom>(FontStyle::bf, false, sptrOf<TextAtom>(std::string("??"), false));
    }
    if (!_parentheses) return text->createBox(env);
    auto row = sptrOf<RowAtom>();
    row->add(sptrOf<TextAtom>(std::string("("), false));
    row->add(text);
    row->add(sptrOf<TextAtom>(std::string(")"), false));
    return row->createBox(env);
  }

private:
  std::shared_ptr<std::map<std::string, std::string>> _labels;
  std::string _key;
  bool _parentheses;
};

class Lowerer {
public:
  Lowerer(const Ast& ast, Diagnostics& diags) : _ast(ast), _diags(diags) {}

  /** The outermost Lowerer of an input starts from the state a host set
   *  (setStartNumbering()), once, and leaves its own end state. */
  void startNumbering() {
    if (!_numbering.outermost()) return;
    NumberingState start = std::move(pendingNumbering());
    pendingNumbering() = NumberingState();
    nums().equation = start.equation;
    *nums().labels = std::move(start.labels);
  }

  void endNumbering() {
    if (!_numbering.outermost()) return;
    finalNumbering().equation = nums().equation;
    finalNumbering().labels = *nums().labels;
  }

  void run(Formula& f, bool lines, bool paragraphs) {
    if (_ast.root == kNoNode) return;
    if (lines) {
      runLines(f, paragraphs);
    } else {
      lowerList(_ast.root, f, 0);
    }
    if (_numbering.outermost()) reportReferences();
    if (_fontDefaults.outermost()) {
      f._mathFontName = _fontDefaults.own().math;
      for (int i = 0; i < 3; i++) f._fontRoles[i] = _fontDefaults.own().roles[i];
    }
  }

private:
  const Ast& _ast;
  Diagnostics& _diags;
  /** Per node, whether a line break is in it: 0 not known yet, 1 no, 2 yes. */
  std::vector<std::int8_t> _breaks;
  /** The same for a document's block structure (hasBlock). */
  std::vector<std::int8_t> _blocks;
  /** Where images are looked for (graphicsDirs). */
  GraphicsDirs _graphicsDirs;
  /** Equation numbers and labels (Numbering). */
  NumberingScope _numbering;
  Numbering& nums() { return *numbering; }
  /** The math font and the preamble's fonts (FontDefaults). */
  FontDefaultsScope _fontDefaults;
  /** Whether the preamble of a document is being lowered, where a font
   *  role is set for the whole body. */
  bool _inPreamble = false;

  /** What an alignment's row, or a display, says about its own number:
   *  \notag, \tag, and the labels written in it. */
  struct RowNote {
    bool notag = false;
    sptr<Atom> tag;
    /** What a reference to the tagged row draws. */
    std::string tagSource;
    bool star = false;
    std::vector<std::string> labels;
  };
  RowNote _note;
  /** What `gmarraystretch` said for the array that comes next. */
  float _stretch = 1.f;
  /** How many alignment rows or displays are being lowered: a \label in
   *  one names its number, which is known when it ends. */
  int _inRow = 0;
  struct Reference {
    std::string key;
    SourceSpan at;
    bool parentheses;
  };
  /** The references this Lowerer made, which the outermost one checks. */
  std::vector<Reference> _refs;
  /** A text argument that stands for one line's part of it while its
   *  command is built once per line (piecesOf()), and that part. */
  NodeId _linePartOf = kNoNode;
  sptr<Atom> _linePart;

  const Node& node(NodeId id) const { return _ast.node(id); }
  NodeId child(NodeId id, std::uint32_t i) const { return _ast.child(id, i); }
  std::uint32_t count(NodeId id) const { return _ast.childCount(id); }

  // --- the old parser's operations on its formula ------------------------

  static sptr<Atom> popBack(Formula& f) {
    auto a = f._root;
    auto* ra = dynamic_cast<RowAtom*>(a.get());
    if (ra != nullptr) return ra->popBack();
    f._root = nullptr;
    return a;
  }

  static sptr<Atom> popFormulaAtom(Formula& f) {
    auto a = f._root;
    f._root = nullptr;
    return a;
  }

  /** What `{...}` becomes: its content's root -- in a row of its own when
   *  nothing came before it in the formula, which is how the old parser's
   *  getArgument() built it. */
  static sptr<Atom> groupAtom(const sptr<Atom>& inner, const Formula& f) {
    if (f._root == nullptr) {
      auto rm = sptrOf<RowAtom>();
      rm->add(inner);
      return rm;
    }
    return inner;
  }

  // --- lists -------------------------------------------------------------

  /** Replay the items of `list`, from `from` on, into `f`. */
  void lowerList(NodeId list, Formula& f, std::uint32_t from) {
    const std::uint32_t n = count(list);
    for (std::uint32_t i = from; i < n; i++) {
      const NodeId id = child(list, i);
      const Node& x = node(id);
      if (x.kind == NodeKind::command && !x.flag && (x.text == "\\" || x.text == "cr")) {
        if (f.isArrayMode()) {
          endRow(static_cast<ArrayFormula&>(f), id);
          continue;
        }
        // A line break outside an alignment: what came before is the first
        // row, and the rest of the list the rows after it.
        ArrayFormula arr;
        arr.add(f._root);
        endRow(arr, id);
        lowerList(list, arr, i + 1);
        arr.checkDimensions();
        f._root = arr.getAsVRow();
        return;
      }
      lowerItem(id, f);
    }
  }

  /** The row a `\\` ends, with the space its `[len]` asks for below it. */
  void endRow(ArrayFormula& arr, NodeId brk) {
    const std::string gap = count(brk) > 0 ? rawOf(child(brk, 0)) : std::string();
    if (!gap.empty()) arr.addRowGap(Units::getDimen(gap));
    arr.addRow();
  }

  /** A list built in a formula of its own; its root. */
  sptr<Atom> build(NodeId list) {
    Formula g;
    lowerList(list, g, 0);
    return g._root;
  }

  void lowerItem(NodeId id, Formula& f) {
    const Node& x = node(id);
    switch (x.kind) {
      case NodeKind::character:
        f.add(charAtom(x));
        return;
      case NodeKind::space:
        spaces(x, f);
        return;
      case NodeKind::group: {
        auto atom = groupAtom(build(child(id, 0)), f);
        if (atom != nullptr) atom->_type = AtomType::ordinary;
        f.add(atom);
        return;
      }
      case NodeKind::command:
        command(id, f);
        return;
      case NodeKind::scripts:
        scripts(id, f);
        return;
      case NodeKind::infix:
        infix(id, f);
        return;
      case NodeKind::declaration:
        declaration(id, f);
        return;
      case NodeKind::leftRight: {
        sptr<Atom> after;
        f.add(leftRight(id, after));
        f.add(after);
        return;
      }
      case NodeKind::environment: {
        auto atom = groupAtom(environment(id), f);
        if (atom != nullptr) atom->_type = AtomType::ordinary;
        f.add(atom);
        return;
      }
      case NodeKind::math:
        f.add(sptrOf<MathAtom>(x.flag ? displayBody(child(id, 0)) : build(child(id, 0)),
                               x.flag ? TexStyle::display : TexStyle::text));
        return;
      case NodeKind::list:
        lowerList(id, f, 0);
        return;
      default:
        return;
    }
  }

  // --- characters and spaces -----------------------------------------------

  static sptr<Atom> singleChar(c32 chr, bool math) {
    const c32 code = convertToRomanNumber(chr);
    if (math) {
      const auto it = Formula::_charToSymbol.find(code);
      if (it != Formula::_charToSymbol.end()) return SymbolAtom::get(it->second);
    }
    return sptrOf<CharAtom>(code, math);
  }

  static sptr<Atom> charAtom(const Node& x) {
    const bool math = x.mode == Mode::math;
    // Several code points that cannot be drawn apart are one run of text.
    if (codepoints(x.text) > 1) return sptrOf<TextAtom>(x.text, math);
    return singleChar(x.cp, math);
  }

  /** A space in text is one space, however much whitespace it was, as in
   *  TeX (the lexer has already dropped what TeX drops: the spaces after a
   *  control word). The old parser drew one more for each line end in the
   *  run, and kept those after a control word. */
  void spaces(const Node& x, Formula& f) {
    if (x.aux == 1) {  // ~
      f.add(sptrOf<SpaceAtom>());
      return;
    }
    if (x.mode == Mode::math) return;
    f.add(sptrOf<SpaceAtom>(false));
    f.add(sptrOf<BreakMarkAtom>());
  }

  // --- commands --------------------------------------------------------------

  static sptr<Atom> unknownAtom(const std::string& name) {
    auto rm =
      sptrOf<FontStyleAtom>(FontStyle::tt, false, fragment("\\mathtt{{\\backslash}" + name + "}", true));
    return sptrOf<ColorAtom>(rm, TRANSPARENT, RED);
  }

  /** An argument's source text, or "" when it was not given. */
  std::string rawOf(NodeId arg) const {
    const Node& a = node(arg);
    return a.flag ? a.raw : std::string();
  }

  /** An argument's text for a handler that reads it as text (a length, a
   *  colour, a name). The recorded text keeps `%` comments -- they are part
   *  of the whitespace before a token -- and a handler would read them as
   *  characters. A comment runs to its line end, which stays. */
  static std::string stripComments(const std::string& raw) {
    if (raw.find('%') == std::string::npos) return raw;
    std::string out;
    for (std::size_t i = 0; i < raw.size(); i++) {
      const char c = raw[i];
      if (c == '\\' && i + 1 < raw.size()) {
        out += c;
        out += raw[++i];
        continue;
      }
      if (c == '%') {
        while (i + 1 < raw.size() && raw[i + 1] != '\n') i++;
        continue;
      }
      out += c;
    }
    return out;
  }

  void command(NodeId id, Formula& f) {
    const Node& x = node(id);
    const CommandSpec* spec = x.flag ? nullptr : findCommand(x.text);
    if (spec != nullptr && spec->shape == Shape::postfix && count(id) == 1) {
      // \limits: the item before it is taken back off, marked, and put back.
      lowerItem(child(id, 0), f);
      auto atom = popBack(f);
      if (atom != nullptr) {
        atom->_limitsType = x.text == "limits"   ? LimitsType::limits
                            : x.text == "nolimits" ? LimitsType::noLimits
                                                   : LimitsType::normal;
      }
      f.add(atom);
      return;
    }
    auto atom = commandAtom(id, f);
    f.add(atom);
    // A rule in an alignment is a row of its own, as the old parser made it.
    if (f.isArrayMode() && dynamic_cast<HlineAtom*>(atom.get()) != nullptr) {
      static_cast<ArrayFormula&>(f).addRow();
    }
  }

  /** The atom a command stands for on its own. */
  /** The atom a command stands for, built for `f`, the formula it goes
   *  into: a rule or \intertext in an alignment works on it. */
  sptr<Atom> commandAtom(NodeId id, Formula& f) {
    const Node& x = node(id);
    const std::string& name = x.text;
    const CommandSpec* spec = x.flag ? nullptr : findCommand(name);
    if (spec == nullptr) {
      // \# \$ \% \& \_ in text are characters of the text, as in TeX, not
      // the math font's symbols.
      if (x.mode == Mode::text && name.size() == 1 && std::string("#$%&_").find(name[0]) != std::string::npos) {
        return singleChar(static_cast<c32>(name[0]), false);
      }
      if (!x.flag) {
        if (Formula::isPredefined(name)) return Formula::get(name)->_root;
        if (auto s = SymbolAtom::get(name)) return s;
      }
      return unknownAtom(name);
    }
    if (name == "kern" || name == "mkern" || name == "hskip" || name == "mskip") {
      const auto [value, unit] = Units::getDimen(rawOf(child(id, 0)));
      return sptrOf<SpaceAtom>(unit, value, 0.f, 0.f);
    }
    if (name == "char") return charCode(rawOf(child(id, 0)), x.mode == Mode::math);
    if (isHeading(name)) {
      auto h = heading(id);
      if (!isHeadingLine(name)) {
        // \paragraph runs into its paragraph: the text follows it on the
        // same line, a quad after it.
        auto row = sptrOf<RowAtom>();
        row->add(h);
        row->add(sptrOf<SpaceAtom>(UnitType::em, 1.f, 0.f, 0.f));
        return row;
      }
      return h;
    }
    // Only the lines of a label have indents to suppress; elsewhere it is
    // what it was when the prelude dropped it.
    if (name == "noindent" || isLineAlignment(name)) return nullptr;
    // Equation numbers, labels and what names them (see Numbering).
    if (name == "tag" || name == "gmtagstar") {
      noteTag(id, name == "gmtagstar");
      return nullptr;
    }
    if (name == "notag" || name == "nonumber") {
      _note.notag = true;
      return nullptr;
    }
    if (name == "label") {
      label(trimmed(rawOf(child(id, 0))));
      return nullptr;
    }
    if (name == "setcounter") {
      if (trimmed(rawOf(child(id, 0))) == "equation") {
        nums().equation = std::atoi(trimmed(rawOf(child(id, 1))).c_str());
      }
      return nullptr;
    }
    if (name == "ref" || name == "eqref") return reference(id, name == "eqref");
    // What LaTeX draws for a reference it cannot resolve: a bold ??. A grob
    // has no pages, nor a bibliography.
    const auto bold = [](const std::string& s) {
      return sptrOf<FontStyleAtom>(FontStyle::bf, false, literalText(s));
    };
    if (name == "pageref") return bold("??");
    if (isCitation(name)) {
      // What LaTeX draws for citations it cannot resolve: a bold ? for each
      // key, then the note, in brackets -- `\cite[p.~3]{a,b}` is [?, ?, p. 3].
      // natbib's take a note before as well, `\citep[see][p.~3]{a}`, or one
      // after alone; \citet names the author it does not have, and
      // \citealp has no brackets.
      const std::string keys = rawOf(child(id, count(id) - 1));
      std::string pre, post = rawOf(child(id, 0));
      if (name != "cite" && node(child(id, 1)).flag) {
        pre = post;
        post = rawOf(child(id, 1));
      }
      auto row = sptrOf<RowAtom>();
      if (name == "citet") row->add(literalText("(author?) "));
      const bool brackets = name != "citealp";
      if (brackets) row->add(literalText("["));
      if (!pre.empty()) {
        row->add(fragment(pre, false));
        row->add(literalText(" "));
      }
      const auto n = std::count(keys.begin(), keys.end(), ',') + 1;
      for (std::ptrdiff_t i = 0; i < n; i++) {
        if (i > 0) row->add(literalText(", "));
        row->add(bold("?"));
      }
      if (!post.empty()) {
        row->add(literalText(", "));
        row->add(fragment(post, false));
      }
      if (brackets) row->add(literalText("]"));
      return row;
    }
    // No page to put the note on, so its text is set where it was written.
    // Its optional number has no note to number.
    if (name == "footnote") {
      const NodeId text = child(id, count(id) - 1);
      return argumentFormula(text, rawOf(text), false);
    }
    // A caption is a line of text, as \text{} sets one; the parser put the
    // break that ends its line after it.
    if (name == "caption") {
      const NodeId text = child(id, count(id) - 1);
      return sptrOf<FontStyleAtom>(FontStyle::rm, false, argumentFormula(text, rawOf(text), false));
    }
    if (name == "url" || name == "href") return link(id, f);
    if (name == "textsc") {
      // Text, as in LaTeX, in math too: upright, with its spaces.
      const sptr<Atom> caps = fragment(smallCaps(rawOf(child(id, 0))), false);
      return node(id).mode == Mode::math ? sptrOf<FontStyleAtom>(FontStyle::rm, false, caps) : caps;
    }
    if (name == "gmtheorem") return fragment(theoremHead(id), false);
    if (name == "verb") {
      const NodeId text = child(id, 0);
      return typewriter(rawOf(text), node(text).star);
    }
    if (name == "includegraphics") return image(id);
    if (name == "gmarraystretch") {
      _stretch = static_cast<float>(std::atof(rawOf(child(id, 0)).c_str()));
      if (!(_stretch > 0.f)) _stretch = 1.f;
      return nullptr;
    }
    if (name == "graphicspath") {
      graphicsPath(rawOf(child(id, 0)));
      return nullptr;
    }
    if (name == "middle") {
      Formula g;
      middle(child(id, 0), g);
      return g._root;
    }
    // At the top of a cell, \color colours the cell rather than what follows.
    if (name == "color" && f.isArrayMode()) {
      return sptrOf<CellForegroundAtom>(ColorAtom::getColor(stripComments(rawOf(child(id, 0)))));
    }
    // A longtable's marker ends its row, and draws nothing.
    if (isLongtableMarker(name)) return nullptr;
    // In an alignment, a rule or \intertext is its handler's, which works
    // on the alignment's rows.
    if (f.isArrayMode() && (isRule(name) || name == "intertext")) return bridge(id, name, spec, f);
    // Rules and \intertext outside an alignment (the parser warned), line
    // breaks (read by the list), and catcode switches (read by the lexer).
    if (spec->special || name == "makeatletter" || name == "makeatother") return nullptr;
    // A declaration or infix command read as a one-token argument has no
    // operand here; the old parser read on past the argument for one.
    if (spec->shape != Shape::prefix) return nullptr;
    return bridge(id, name, spec, f);
  }

  /** `text` as written, in the typewriter face: one run of characters, none
   *  of them read as LaTeX and none a ligature. A space is kept (a `\verb*`
   *  shows it as U+2423). */
  static sptr<Atom> typewriter(const std::string& text, bool visibleSpaces) {
    std::string shown;
    for (const char c : text) {
      if (c == ' ' && visibleSpaces) {
        shown += "\xe2\x90\xa3";
      } else {
        shown += c;
      }
    }
    // Nested, as \texttt is: added to the roman the text around it is set in.
    return sptrOf<FontStyleAtom>(FontStyle::tt, false, literalText(shown), true);
  }

  /** Small capitals, faked as every device has to: the lowercase letters of
   *  `text` as capitals at 0.8 of the size, the rest as it is. A command's
   *  name and what is in math are left alone. Only ASCII letters change. */
  static std::string smallCaps(const std::string& text) {
    std::string out, run;
    const auto flush = [&] {
      if (!run.empty()) out += "\\textscale{0.8}{" + run + "}";
      run.clear();
    };
    bool math = false;
    for (std::size_t i = 0; i < text.size(); i++) {
      const char c = text[i];
      if (c == '$') {
        flush();
        math = !math;
        out += c;
      } else if (math) {
        out += c;
      } else if (c == '\\') {
        flush();
        out += c;
        // A control word whole, else the one character it escapes.
        const bool word = i + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[i + 1])) != 0;
        do {
          if (++i < text.size()) out += text[i];
        } while (word && i + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[i + 1])) != 0);
      } else if (c >= 'a' && c <= 'z') {
        run += static_cast<char>(c - 'a' + 'A');
      } else {
        flush();
        out += c;
      }
    }
    flush();
    return out;
  }

  /** The head of a theorem: "Theorem 2 (note)." in bold, as amsthm's plain
   *  style sets it, a remark's in italics. `style`, the counter, the
   *  counter it is set within, the title and the note are its arguments
   *  (the expander's \newtheorem writes them). A numbered theorem steps its
   *  counter, which starts again when the section it is within does, and is
   *  what a \label after it names. */
  std::string theoremHead(NodeId id) {
    const std::string style = rawOf(child(id, 0));
    const std::string counter = rawOf(child(id, 1));
    const std::string within = rawOf(child(id, 2));
    const std::string title = rawOf(child(id, 3));
    const std::string note = rawOf(child(id, 4));
    std::string number;
    if (!counter.empty()) {
      std::string where;
      if (within == "section") where = std::to_string(_section[0]);
      if (within == "subsection") where = std::to_string(_section[0]) + "." + std::to_string(_section[1]);
      Numbering::Counter& c = nums().counters[counter];
      if (c.where != where) c.value = 0;
      c.where = where;
      number = (where.empty() ? "" : where + ".") + std::to_string(++c.value);
      nums().current = number;
    }
    const std::string name = title + (number.empty() ? "" : "\\ " + number);
    const bool italic = style == "remark";
    std::string head = (italic ? "\\textit{" : "\\textbf{") + name + "}";
    if (!note.empty()) head += "\\ (" + note + ")";
    return head + (italic ? "." : "\\textbf{.}");
  }

  /** The text of a verbatim environment: a line to a row, flush left. */
  sptr<Atom> verbatimBlock(const Node& x) {
    auto lines = sptrOf<VRowAtom>();
    lines->_halign = Alignment::left;
    std::size_t from = 0;
    while (true) {
      const std::size_t to = x.raw.find('\n', from);
      const std::string line =
        x.raw.substr(from, to == std::string::npos ? std::string::npos : to - from);
      lines->append(typewriter(line.empty() ? " " : line, x.text == "verbatim*"));
      if (to == std::string::npos) break;
      from = to + 1;
    }
    return lines;
  }

  /** \url and \href. A grob has no links, so only their look, LaTeX's
   *  rather than HTML's: hyperref's colorlinks colours the text, and the url
   *  package sets a URL in monospace, its characters as written. */
  sptr<Atom> link(NodeId id, Formula& f) {
    static const std::string colour = "#0969DA";
    const bool math = node(id).mode == Mode::math;
    auto* cm = dynamic_cast<CommandMacro*>(MacroInfo::get("textcolor"));
    if (cm == nullptr) return nullptr;
    if (node(id).text == "href") {
      const NodeId text = child(id, 1);
      TreeArgs a(*this, math, {"textcolor", colour, rawOf(text)}, {kNoNode, kNoNode, text}, &f);
      return cm->call(a);
    }
    return sptrOf<ColorAtom>(typewriter(rawOf(child(id, 0)), false), TRANSPARENT, ColorAtom::getColor(colour));
  }

  /** \includegraphics[options]{path}: the host reads the file and says
   *  what stands for it (front/hooks.h); with nothing from it, the file's
   *  name, so the gap is seen. */
  sptr<Atom> image(NodeId id) {
    std::string options = rawOf(child(id, 0));
    const std::string more = rawOf(child(id, 1));
    if (!more.empty()) options += (options.empty() ? "" : ",") + more;
    std::string path = rawOf(child(id, 2));
    const auto first = path.find_first_not_of(" \t\r\n");
    path = first == std::string::npos ? "" : path.substr(first, path.find_last_not_of(" \t\r\n") - first + 1);
    std::string latex;
    if (const ImageResolver& resolve = imageResolver()) latex = resolve(path, options, *graphicsDirs);
    if (!latex.empty()) return fragment(latex, node(id).mode == Mode::math);
    // The name without its directory, character by character, so that a
    // `_` or `%` in it is itself. Either separator: it may be a Windows path.
    const auto cut = path.find_last_of("/\\");
    return literalText(cut == std::string::npos ? path : path.substr(cut + 1));
  }

  /** \graphicspath{{dir/}{dir/}}: the directories, in place of the last. */
  void graphicsPath(const std::string& raw) {
    std::vector<std::string>& dirs = *graphicsDirs;
    dirs.clear();
    std::size_t i = 0;
    while ((i = raw.find('{', i)) != std::string::npos) {
      const auto j = raw.find('}', i + 1);
      if (j == std::string::npos) break;
      std::string dir = raw.substr(i + 1, j - i - 1);
      const auto a = dir.find_first_not_of(" \t\r\n");
      if (a != std::string::npos) {
        dirs.push_back(dir.substr(a, dir.find_last_not_of(" \t\r\n") - a + 1));
      }
      i = j + 1;
    }
  }

  sptr<Atom> charCode(const std::string& raw, bool math) {
    int radix = 10;
    std::size_t offset = 0;
    if (!raw.empty() && raw[0] == '\'') radix = 8, offset = 1;
    if (!raw.empty() && raw[0] == '"') radix = 16, offset = 1;
    int n = 0;
    str2int(raw.c_str() + offset, raw.length() - offset, n, radix);
    return singleChar(static_cast<c32>(n), math);
  }

  /** A command's arguments as the tree holds them, for a handler written
   *  against CommandArgs: each is lowered here, with the rest of the input,
   *  rather than parsed again from its text. */
  class TreeArgs : public CommandArgs {
  public:
    TreeArgs(Lowerer& lx, bool math, std::vector<std::string> texts, std::vector<NodeId> nodes,
             Formula* here)
        : _lx(lx), _math(math), _texts(std::move(texts)), _nodes(std::move(nodes)), _here(here) {}

    const std::string& text(std::size_t i) const override {
      static const std::string none;
      return i < _texts.size() ? _texts[i] : none;
    }

    sptr<Atom> formula(std::size_t i, bool math, bool) override {
      return _lx.argumentFormula(i < _nodes.size() ? _nodes[i] : kNoNode, text(i), math);
    }

    bool isMathMode() const override { return _math; }

    bool isPartial() const override { return true; }

    sptr<ArrayFormula> alignment(std::size_t i) override {
      return _lx.alignmentOf(i < _nodes.size() ? _nodes[i] : kNoNode);
    }

    sptr<Atom> formulaOf(const std::string& latex, bool math) override {
      return fragment(latex, math);
    }

    sptr<ArrayFormula> alignmentOfText(const std::string& latex) override {
      Diagnostics found;
      const Ast ast = parseLatex("\\begin{matrix}" + latex + "\\end{matrix}", Mode::math, found);
      const NodeId env = ast.root != kNoNode && ast.childCount(ast.root) > 0
                           ? ast.child(ast.root, 0) : kNoNode;
      auto arr = Lowerer(ast, found).alignmentOf(env);
      // The text is the user's (a list's items): its problems are reported
      // where the list begins, as its own positions are not the input's.
      if (!_who.empty()) {
        for (const Diagnostic& d : found.items()) _lx._diags.warn(_at, _who + ": " + d.message);
      }
      return arr;
    }

    sptr<Atom> formulaChecked(const std::string& latex, bool math) override {
      if (latex.empty()) return nullptr;
      Diagnostics found;
      const Ast ast = parseLatex(latex, math ? Mode::math : Mode::text, found);
      if (ast.root == kNoNode) return nullptr;
      Formula g;
      Lowerer(ast, found).run(g, false, false);
      for (const Diagnostic& d : found.items()) warn(d.message);
      return g._root;
    }

    void warn(const std::string& message) override {
      if (!_who.empty()) _lx._diags.warn(_at, _who + ": " + message);
    }

    /** Report what alignmentOfText() finds at `at`, as `who`'s. */
    void reportAt(SourceSpan at, std::string who) {
      _at = at;
      _who = std::move(who);
    }

    ArrayFormula* alignmentHere() override {
      return _here != nullptr && _here->isArrayMode() ? static_cast<ArrayFormula*>(_here) : nullptr;
    }

  private:
    Lowerer& _lx;
    bool _math;
    std::vector<std::string> _texts;
    std::vector<NodeId> _nodes;
    Formula* _here;
    SourceSpan _at;
    std::string _who;
  };

  /** LaTeX not from the input -- a raw argument read as a formula, text a
   *  handler put together -- read by the front end as a piece of input of
   *  its own. Its problems are not reported: they are not the user's. */
  /** A run of characters set as they are written, with nothing in it read
   *  as LaTeX: a file's name, a citation key. */
  static sptr<Atom> literalText(const std::string& s) {
    auto text = sptrOf<TextAtom>(false);
    for (int i = 0, n = static_cast<int>(s.size()); i < n;) {
      int len = 0;
      const c32 c = nextUnicode(s, i, len);
      if (len <= 0) break;
      text->append(c);
      i += len;
    }
    return text;
  }

  static sptr<Atom> fragment(const std::string& latex, bool math) {
    return buildFragment(latex, math);
  }

  /** An argument read as a formula in the mode asked for. The tree has it
   *  when the argument was parsed in that mode; otherwise (a raw argument
   *  that a handler reads as a formula) its text is read now, as a piece of
   *  input of its own. Its problems are not reported: such an argument is
   *  raw because it is not a formula to check -- `\underaccent{\dot}{x}`
   *  gives an accent command without its argument, and means to. */
  sptr<Atom> argumentFormula(NodeId arg, const std::string& text, bool math) {
    if (arg == kNoNode || !node(arg).flag) return nullptr;
    if (arg == _linePartOf) return _linePart;
    const Node& a = node(arg);
    const Mode want = math ? Mode::math : Mode::text;
    if (count(arg) == 1 && a.mode == want) return build(child(arg, 0));
    return fragment(text, math);
  }

  /** Build a command with its engine handler, its arguments laid out as the
   *  old parser laid them out: mandatory ones from 1, optional ones after
   *  them. */
  sptr<Atom> bridge(NodeId id, const std::string& name, const CommandSpec* spec, Formula& f) {
    MacroInfo* mac = MacroInfo::get(name);
    if (mac == nullptr) return unknownAtom(name);
    std::vector<std::string> args(static_cast<std::size_t>(mac->argc) + 12);
    std::vector<NodeId> nodes(args.size(), kNoNode);
    args[0] = name;
    std::size_t mandatory = 1;
    std::size_t optional = static_cast<std::size_t>(mac->argc) + 1;
    const std::uint32_t n = count(id);
    for (std::uint32_t i = 0; i < n && i < spec->args.size(); i++) {
      // A URL's `%` is a character, as the lexer read it there.
      const std::string raw = spec->args[i].kind == ArgKind::url ? rawOf(child(id, i))
                                                                 : stripComments(rawOf(child(id, i)));
      std::size_t& at = spec->args[i].optional ? optional : mandatory;
      if (at < args.size()) {
        nodes[at] = child(id, i);
        args[at++] = raw;
      }
    }
    auto* cm = dynamic_cast<CommandMacro*>(mac);
    // Every handler the front end reaches reads CommandArgs; the rest serve
    // the old parser only (definitions, \over and kin, ...).
    if (cm == nullptr) return unknownAtom(name);
    TreeArgs a(*this, node(id).mode == Mode::math, std::move(args), std::move(nodes), &f);
    try {
      return cm->call(a);
    } catch (const std::exception& e) {
      _diags.warn(node(id).span, "\\" + name + ": " + clean(e.what()) + "; drawn as its name");
      return unknownAtom(name);
    }
  }

  /** The old handlers' error text, less the wrapper the dispatcher adds. */
  static std::string clean(std::string msg) {
    const std::string mark = "caused by: ";
    const auto p = msg.rfind(mark);
    if (p != std::string::npos) msg = msg.substr(p + mark.size());
    for (char& c : msg) {
      if (c == '\n') c = ' ';
    }
    return msg;
  }

  // --- scripts ---------------------------------------------------------------

  void scripts(NodeId id, Formula& f) {
    const Node& x = node(id);
    const NodeId base = child(id, 0);
    const NodeId sub = child(id, 1);
    const NodeId sup = child(id, 2);
    // The base is in the formula first, as it was when the old parser met
    // the scripts; they take it back off.
    if (node(base).kind != NodeKind::list) lowerItem(base, f);
    const std::string& order = x.text;
    // TeX's prime: `'` is `^{\prime}`, and primes with a `^` after them are
    // one superscript -- f''^2 is f^{\prime\prime 2} -- so f' draws as
    // f^{\prime} does.
    if (order.find('\'') != std::string::npos) {
      Formula primed;
      for (const char c : order) {
        if (c == '\'') primed.add(SymbolAtom::get("prime"));
      }
      if (order.find('^') != std::string::npos) appendScript(sup, primed);
      const sptr<Atom> supAtom = groupAtom(primed._root, f);
      const sptr<Atom> subAtom = order.find('_') != std::string::npos ? scriptArgument(sub, f) : nullptr;
      f.add(attach(f, subAtom, supAtom));
      return;
    }
    std::size_t i = 0;
    while (i < order.size()) {
      const char c = order[i];
      sptr<Atom> subAtom, supAtom;
      const bool pair = i + 1 < order.size() && (order[i + 1] == '^' || order[i + 1] == '_') &&
                        order[i + 1] != c;
      if (c == '^') {
        supAtom = scriptArgument(sup, f);
        if (pair) subAtom = scriptArgument(sub, f);
      } else {
        subAtom = scriptArgument(sub, f);
        if (pair) supAtom = scriptArgument(sup, f);
      }
      i += pair ? 2 : 1;
      f.add(attach(f, subAtom, supAtom));
    }
  }

  static sptr<Atom> attach(Formula& f, const sptr<Atom>& sub, const sptr<Atom>& sup) {
    if (f._root == nullptr) return sptrOf<ScriptsAtom>(nullptr, sub, sup);
    sptr<Atom> atom;
    if (auto* rm = dynamic_cast<RowAtom*>(f._root.get())) {
      atom = rm->popBack();
    } else {
      atom = f._root;
      f._root = nullptr;
    }
    if (atom->rightType() == AtomType::bigOperator) return sptrOf<OperatorAtom>(atom, sub, sup);
    return sptrOf<ScriptsAtom>(atom, sub, sup);
  }

  /** A superscript's content added after primes, as TeX's `'` takes it:
   *  a group's braces go, so f'^{ab} is f^{\prime ab}. */
  void appendScript(NodeId list, Formula& g) {
    if (count(list) == 0) return;
    const NodeId item = child(list, 0);
    if (node(item).kind == NodeKind::group) {
      lowerList(child(item, 0), g, 0);
    } else {
      lowerItem(item, g);
    }
  }

  /** A script's argument, as the old parser's getArgument() built it. */
  sptr<Atom> scriptArgument(NodeId list, Formula& f) {
    if (count(list) == 0) return sptrOf<EmptyAtom>();
    const NodeId item = child(list, 0);
    const Node& y = node(item);
    switch (y.kind) {
      case NodeKind::group:
        return groupAtom(build(child(item, 0)), f);
      case NodeKind::character:
        return charAtom(y);
      case NodeKind::command:
        return commandAtom(item, f);
      case NodeKind::leftRight:
        return leftRight(item);
      case NodeKind::environment:
        return groupAtom(environment(item), f);
      default: {
        Formula g;
        lowerItem(item, g);
        return g._root;
      }
    }
  }

  // --- \over and kin -----------------------------------------------------------

  void infix(NodeId id, Formula& f) {
    const Node& x = node(id);
    const std::string& name = x.text;
    // The numerator is what was in the formula before the command.
    lowerList(child(id, 0), f, 0);
    auto num = popFormulaAtom(f);
    std::vector<std::string> args;
    for (std::uint32_t i = 2; i < count(id); i++) args.push_back(rawOf(child(id, i)));
    auto den = build(child(id, 1));
    if (num == nullptr || den == nullptr) {
      _diags.warn(x.span, "\\" + name + ": a fraction needs both a numerator and a denominator");
      f.add(num != nullptr ? num : den);
      return;
    }
    const auto delims = [&](bool rule, const Dimen* thickness) -> sptr<Atom> {
      auto frac = thickness != nullptr ? sptrOf<FracAtom>(num, den, rule, *thickness)
                                       : sptrOf<FracAtom>(num, den, rule);
      try {
        return sptrOf<FencedAtom>(frac, args.size() > 0 ? args[0] : "",
                                  args.size() > 1 ? args[1] : "");
      } catch (const std::exception& e) {
        // TeX puts a null delimiter in place of one that is not.
        _diags.warn(x.span, "\\" + name + ": " + clean(e.what()) + "; left out");
        return frac;
      }
    };
    sptr<Atom> out;
    if (name == "over") {
      out = sptrOf<FracAtom>(num, den, true);
    } else if (name == "atop") {
      out = sptrOf<FracAtom>(num, den, false);
    } else if (name == "above") {
      const Dimen thick = Units::getDimen(args.empty() ? "" : args.back());
      out = sptrOf<FracAtom>(num, den, true, thick);
    } else if (name == "choose" || name == "brack" || name == "bangle" || name == "brace") {
      const char* l = name == "choose" ? "lparen" : name == "brack" ? "lbrack"
                      : name == "bangle" ? "langle" : "lbrace";
      const char* r = name == "choose" ? "rparen" : name == "brack" ? "rbrack"
                      : name == "bangle" ? "rangle" : "rbrace";
      out = sptrOf<FencedAtom>(sptrOf<FracAtom>(num, den, false), l, r);
    } else if (name == "overwithdelims") {
      out = delims(true, nullptr);
    } else if (name == "atopwithdelims") {
      out = delims(false, nullptr);
    } else {  // abovewithdelims
      const Dimen thick = Units::getDimen(args.size() > 2 ? args[2] : "");
      out = delims(true, &thick);
    }
    f.add(out);
  }

  // --- declarations ----------------------------------------------------------

  void declaration(NodeId id, Formula& f) {
    const NodeId body = child(id, count(id) - 1);
    f.add(declarationAtom(id, [&] { return build(body); }));
  }

  /** A declaration applied to its body, which `body` builds; null when it
   *  fails, with a warning. */
  sptr<Atom> declarationAtom(NodeId id, const std::function<sptr<Atom>()>& body) {
    const Node& x = node(id);
    const std::string& name = x.text;
    const bool math = x.mode == Mode::math;
    try {
      if (isTextFont(name)) {
        const auto atom = body();
        return sptrOf<FontStyleAtom>(FontContext::mainFontStyleOf(name), math, atom);
      }
      if (name == "scshape") return fragment(smallCaps(x.raw), math);
      if (name == "boldmath") {
        // A math style: text in its reach keeps its own.
        return sptrOf<FontStyleAtom>(FontStyle::bf, true, body());
      }
      if (name == "cal" || name == "frak") {
        // TeX's old switches for \mathcal and \mathfrak.
        const auto style = FontContext::mathFontStyleOf(name == "cal" ? "mathcal" : "mathfrak");
        return sptrOf<FontStyleAtom>(style, true, body());
      }
      if (isSize(name)) {
        auto a = body();
        return sized(a == nullptr ? sptrOf<EmptyAtom>() : a, sizeFactor(name));
      }
      if (name == "color") {
        const color c = ColorAtom::getColor(rawOf(child(id, 0)));
        return sptrOf<ColorAtom>(body(), TRANSPARENT, c);
      }
      if (name == "fontsize") {
        // The size over the 10pt of \normalsize, which a grob's own size
        // stands for, as \large's 1.2 does.
        const std::string raw = rawOf(child(id, 0));
        char* end = nullptr;
        const float points = std::strtof(raw.c_str(), &end);
        auto a = body();
        if (a == nullptr) a = sptrOf<EmptyAtom>();
        if (end == raw.c_str() || !std::isfinite(points) || points <= 0) {
          _diags.warn(x.span, "\\fontsize: `" + raw + "' is not a positive size; the size is kept");
          return a;
        }
        return sized(a, std::min(points / 10.f, 16384.f));
      }
      if (name == "relscale") {
        // relsize's: a size relative to the one around it, which still
        // breaks with its text, as \large's does.
        const std::string raw = rawOf(child(id, 0));
        char* end = nullptr;
        const float factor = std::strtof(raw.c_str(), &end);
        auto a = body();
        if (a == nullptr) a = sptrOf<EmptyAtom>();
        if (end == raw.c_str() || !std::isfinite(factor) || factor <= 0) {
          _diags.warn(x.span, "\\relscale: `" + raw + "' is not a positive number; the size is kept");
          return a;
        }
        // As \scalebox's: at most TeX's largest dimension, as a factor.
        return sized(a, std::min(factor, 16384.f));
      }
      if (name == "setmainfont" || name == "setsansfont" || name == "setmonofont") {
        auto a = body();
        if (a == nullptr) a = sptrOf<EmptyAtom>();
        const int role =
          name == "setmainfont" ? roleMain : name == "setsansfont" ? roleSans : roleMono;
        const int index = fontIndex(id, 1);
        if (index == 0) return a;
        // In a preamble, for the whole body: the atom here is thrown away.
        if (_inPreamble) fontDefaults->roles[role] = index;
        return sptrOf<FontRoleAtom>(role, index, a);
      }
      if (name == "setmathfont") {
        setMathFont(id);
        auto a = body();
        return a == nullptr ? sptrOf<EmptyAtom>() : a;
      }
      if (name == "fontspec" || name == "fontfamily") {
        auto a = body();
        if (a == nullptr) a = sptrOf<EmptyAtom>();
        const int index = fontIndex(id, name == "fontspec" ? 1 : 0);
        return index == 0 ? a : sptr<Atom>(new FontFamilyAtom(index, a));
      }
      // \displaystyle and kin
      auto g = body();
      return sptrOf<StyleAtom>(texStyleOf(name), g == nullptr ? sptrOf<EmptyAtom>() : g);
    } catch (const std::exception& e) {
      _diags.warn(x.span, "\\" + name + ": " + clean(e.what()));
      return nullptr;
    }
  }

  // --- fonts: \setmainfont, \fontspec, \fontfamily, \setmathfont --------------

  /** fontspec's options, `key=value,key,...`, from the optional arguments
   *  either side of the font's name. The engine reads Path, Extension and
   *  the four faces; any other is warned and left out. */
  std::vector<std::pair<std::string, std::string>> fontOptions(NodeId id, const std::string& who) {
    std::vector<std::pair<std::string, std::string>> out;
    if (node(id).text == "fontfamily") return out;
    static const std::set<std::string> known = {"Path",       "Extension",  "UprightFont",
                                                "BoldFont",   "ItalicFont", "BoldItalicFont"};
    for (const std::uint32_t at : {0u, 2u}) {
      const std::string raw = stripComments(rawOf(child(id, at)));
      int depth = 0;
      std::size_t start = 0;
      for (std::size_t i = 0; i <= raw.size(); i++) {
        const char c = i < raw.size() ? raw[i] : ',';
        if (c == '{') depth++;
        if (c == '}') depth--;
        if (c != ',' || depth > 0) continue;
        const std::string item = raw.substr(start, i - start);
        start = i + 1;
        const auto eq = item.find('=');
        const std::string key = trimmed(item.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trimmed(item.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '{' && value.back() == '}') {
          value = trimmed(value.substr(1, value.size() - 2));
        }
        if (key.empty()) continue;
        if (known.count(key) != 0) {
          out.emplace_back(key, value);
        } else {
          _diags.warn(node(id).span,
                      who + ": the option `" + key + "' is not supported and is ignored");
        }
      }
    }
    return out;
  }

  /** What the host makes of the font a font command names (front/hooks.h):
   *  its own name for it, or "" with a warning when there is none. */
  std::string resolveFont(NodeId id, std::uint32_t nameArg) {
    const std::string& command = node(id).text;
    const std::string who = "\\" + command;
    const std::string name = trimmed(stripComments(rawOf(child(id, nameArg))));
    if (name.empty()) {
      _diags.warn(node(id).span, who + ": no font name; the font is kept");
      return "";
    }
    const std::string role = command == "setmainfont"   ? "main"
                             : command == "setsansfont" ? "sans"
                             : command == "setmonofont" ? "mono"
                             : command == "setmathfont" ? "math"
                             : command == "fontfamily"  ? "nfss"
                                                        : "font";
    const auto options = fontOptions(id, who);
    std::string found = name;
    if (const FontResolver& resolve = fontResolver()) found = resolve(name, options, role);
    if (found.empty()) {
      _diags.warn(node(id).span,
                  role == "math"
                    ? who + ": `" + name + "' is not a loaded math font (load it with load_font()); "
                          "the default is used"
                    : who + ": font `" + name + "' not found; the default is used");
    }
    return found;
  }

  /** The family index of a font command's font, 0 when it has none. */
  int fontIndex(NodeId id, std::uint32_t nameArg) {
    const std::string family = resolveFont(id, nameArg);
    return family.empty() ? 0 : register_font_family(family);
  }

  /** \setmathfont: a formula has one math font, so it is the input's, wherever
   *  the command stands, and a second, different one is not used. */
  void setMathFont(NodeId id) {
    const std::string font = resolveFont(id, 1);
    if (font.empty()) return;
    std::string& math = fontDefaults->math;
    if (math.empty() || math == font) {
      math = font;
    } else {
      _diags.warn(node(id).span, "\\setmathfont: a formula has one math font; `" + font +
                                   "' is ignored and `" + math + "' is used");
    }
  }

  // --- \left \middle \right ----------------------------------------------------

  sptr<Atom> delimiter(const std::string& raw) {
    auto atom = fragment(raw, true);
    if (auto* big = dynamic_cast<BigSymbolAtom*>(atom.get())) atom = big->_delim;
    return atom;
  }

  /** Whether `atom` names a delimiter ("" and "." are none, and are fine). */
  static bool isDelimiter(const sptr<Atom>& atom) {
    const auto sym = std::dynamic_pointer_cast<CharSymbol>(atom);
    if (sym == nullptr) return false;
    const std::string name = sym->name();
    return name.empty() || name == "." || delimiterSymbol(name) != nullptr;
  }

  /** The symbol name of a \left, \middle or \right delimiter. One that is
   *  not a delimiter is TeX's "Missing delimiter": a null delimiter goes in
   *  its place, with a warning, and the token is read again as itself --
   *  `put` says where. `\middle\vert` used to hand the engine the text
   *  "\vert" and fail at layout. */
  std::string delimiterName(const sptr<Atom>& atom, NodeId arg, const std::string& who,
                            const char* put) {
    if (isDelimiter(atom)) return std::dynamic_pointer_cast<CharSymbol>(atom)->name();
    if (!rawOf(arg).empty()) {
      _diags.warn(node(arg).span, who + ": " + rawOf(arg) + " is not a delimiter; drawn " + put);
    }
    return ".";
  }

  /** A \middle, and what it put back when its delimiter is not one. */
  void middle(NodeId arg, Formula& f) {
    auto atom = delimiter(rawOf(arg));
    f.add(sptrOf<MiddleAtom>(delimiterName(atom, arg, "\\middle", "after it")));
    if (!isDelimiter(atom)) f.add(atom);
  }

  /** \left...\right. A \left whose delimiter is not one puts it back inside
   *  the fence; a \right's goes after the fence, into `after`. */
  sptr<Atom> leftRight(NodeId id, sptr<Atom>& after) {
    const std::uint32_t n = count(id);
    const NodeId leftArg = child(id, 0), rightArg = child(id, n - 1);
    auto left = delimiter(rawOf(leftArg));
    auto right = delimiter(rawOf(rightArg));
    auto sl = std::dynamic_pointer_cast<CharSymbol>(left);
    auto sr = std::dynamic_pointer_cast<CharSymbol>(right);
    // A delimiter the parser had to insert (nothing was there) is TeX's null
    // delimiter, as `\right.` is: `\left(\frac{a}{b}` stretches its `(` just
    // as `\left(\frac{a}{b}\right.` does. Only a delimiter that is there and
    // is not a symbol at all (`\left\frac12`) falls back to a plain row.
    const bool fenced = (sl != nullptr || rawOf(leftArg).empty()) &&
                        (sr != nullptr || rawOf(rightArg).empty());
    Formula tf;
    std::string leftName, rightName;
    if (fenced) {
      leftName = delimiterName(left, leftArg, "\\left", "inside the fence");
      rightName = delimiterName(right, rightArg, "\\right", "after the fence");
      if (left != nullptr && !isDelimiter(left)) tf.add(left);
      if (right != nullptr && !isDelimiter(right)) after = right;
    }
    for (std::uint32_t i = 1; i + 1 < n; i++) {
      const NodeId c = child(id, i);
      if (node(c).kind == NodeKind::list) {
        lowerList(c, tf, 0);
      } else {
        middle(c, tf);
      }
    }
    if (fenced) return sptrOf<FencedAtom>(tf._root, leftName, rightName, tf.middle());
    auto ra = sptrOf<RowAtom>();
    ra->add(left);
    ra->add(tf._root);
    ra->add(right);
    return ra;
  }

  /** \left...\right as one atom, with what a \right put back after it. */
  sptr<Atom> leftRight(NodeId id) {
    sptr<Atom> after;
    auto fence = leftRight(id, after);
    if (after == nullptr) return fence;
    auto ra = sptrOf<RowAtom>();
    ra->add(fence);
    ra->add(after);
    return ra;
  }

  // --- mixed mode: a label's lines ------------------------------------------

  static bool isBreakNode(const Node& x) {
    return x.kind == NodeKind::command && !x.flag && (x.text == "\\" || x.text == "cr");
  }

  /** Whether a line break is in `id`, where it ends a line of the label:
   *  in prose, through groups, text arguments and declarations, but not in
   *  math or an environment. */
  bool hasBreak(NodeId id) {
    if (_breaks.empty()) _breaks.assign(_ast.size(), 0);
    if (_breaks[id] != 0) return _breaks[id] == 2;
    const Node& x = node(id);
    bool found = false;
    if (isBreakNode(x)) {
      found = true;
    } else if (x.kind == NodeKind::list) {
      for (std::uint32_t i = 0; i < count(id) && !found; i++) found = hasBreak(child(id, i));
    } else if (x.kind == NodeKind::group) {
      found = hasBreak(child(id, 0));
    } else if (x.kind == NodeKind::declaration) {
      const NodeId body = child(id, count(id) - 1);
      found = node(body).mode == Mode::text && hasBreak(body);
    } else if (x.kind == NodeKind::command) {
      found = textArgumentWithBreak(id) != kNoNode;
    }
    _breaks[id] = found ? 2 : 1;
    return found;
  }

  /** The text argument of command `id` that holds a line break, if any. A
   *  heading line is one heading, however many lines its title takes: it
   *  is not built once per line, which would number it once per line. */
  NodeId textArgumentWithBreak(NodeId id) {
    if (isHeadingLine(node(id).text)) return kNoNode;
    for (std::uint32_t i = 0; i < count(id); i++) {
      const NodeId arg = child(id, i);
      const Node& a = node(arg);
      if (a.kind == NodeKind::argument && a.flag && a.mode == Mode::text && count(arg) == 1 &&
          hasBreak(child(arg, 0))) {
        return arg;
      }
    }
    return kNoNode;
  }

  /** One line's part of something a line break runs through: a group's
   *  content (made a group where it goes), or an atom. */
  struct Piece {
    sptr<Atom> atom;
    bool group = false;
  };

  void put(const Piece& p, Formula& f) {
    if (!p.group) {
      f.add(p.atom);
      return;
    }
    auto atom = groupAtom(p.atom, f);
    if (atom != nullptr) atom->_type = AtomType::ordinary;
    f.add(atom);
  }

  /** Something with a line break in it, once per line it runs over, as
   *  TeX would carry it across the break: `\textbf{a\\b}` is
   *  `\textbf{a}` on one line and `\textbf{b}` on the next, and a group
   *  or a declaration likewise. A null atom is a line it has nothing on. */
  std::vector<Piece> piecesOf(NodeId id) {
    const Node& x = node(id);
    std::vector<Piece> out;
    if (x.kind == NodeKind::group) {
      for (const auto& line : linesOf(child(id, 0))) out.push_back({line->_root, true});
      return out;
    }
    if (x.kind == NodeKind::declaration) {
      for (const auto& line : linesOf(child(id, count(id) - 1))) {
        const sptr<Atom> body = line->_root;
        out.push_back({body == nullptr ? nullptr : declarationAtom(id, [&] { return body; })});
      }
      return out;
    }
    // The command as it is built anywhere, its argument standing for each
    // line's part in turn: \footnote, \caption and \paragraph are the
    // lowering's own, and a handler's rebuild knew only handlers.
    const NodeId arg = textArgumentWithBreak(id);
    for (const auto& line : linesOf(child(arg, 0))) {
      if (line->_root == nullptr) {
        out.push_back({});
        continue;
      }
      struct Restore {
        Lowerer& lx;
        NodeId of;
        sptr<Atom> part;
        ~Restore() {
          lx._linePartOf = of;
          lx._linePart = part;
        }
      } restore{*this, _linePartOf, _linePart};
      _linePartOf = arg;
      _linePart = line->_root;
      Formula here;
      out.push_back({commandAtom(id, here)});
    }
    return out;
  }

  /** A text list cut at its line breaks, each line's part in a formula of
   *  its own. Spaces either side of a break go with it, and a run of
   *  breaks is one; a break at either end leaves an empty part there, so
   *  the list's owner still ends its line. */
  std::vector<std::unique_ptr<Formula>> linesOf(NodeId list) {
    std::vector<std::unique_ptr<Formula>> lines;
    lines.push_back(std::make_unique<Formula>());
    bool broken = false;
    std::vector<NodeId> spaces;
    const auto content = [&] {
      if (broken) {
        lines.push_back(std::make_unique<Formula>());
        broken = false;
      }
      for (const NodeId s : spaces) lowerItem(s, *lines.back());
      spaces.clear();
    };
    for (std::uint32_t i = 0; i < count(list); i++) {
      const NodeId id = child(list, i);
      const Node& x = node(id);
      if (isBreakNode(x)) {
        spaces.clear();
        broken = true;
      } else if (isSpace(x)) {
        if (!broken) spaces.push_back(id);
      } else if (!hasBreak(id)) {
        content();
        lowerItem(id, *lines.back());
      } else {
        const auto pieces = piecesOf(id);
        for (std::size_t k = 0; k < pieces.size(); k++) {
          if (k > 0) {
            spaces.clear();
            broken = true;
          }
          if (pieces[k].atom == nullptr) continue;
          content();
          put(pieces[k], *lines.back());
        }
      }
    }
    if (broken) {
      lines.push_back(std::make_unique<Formula>());
    } else {
      for (const NodeId s : spaces) lowerItem(s, *lines.back());
    }
    return lines;
  }

  /** The label being built: its lines, the current one's prose. */
  struct Label {
    explicit Label(Formula& f) : top(&f), line(&f) {}
    Formula* top;
    /** Where the current line goes: `top` until a second line starts. */
    Formula* line;
    std::unique_ptr<ArrayFormula> rows;
    /** The current stretch of prose, one \text{} when it ends. */
    std::unique_ptr<Formula> prose;
    /** Spaces not placed yet: they go with a break that follows them. */
    std::vector<NodeId> spaces;
    /** A line break since the last thing drawn. */
    bool broken = false;
    /** The `[len]` of that break: space below the line it ends. */
    std::string gap;
    /** Nothing placed since a line break: a space here goes with it. */
    bool afterBreak = false;
    /** Anything drawn at all: a break before that is none. */
    bool drawn = false;
    /** A paragraph starts here: its first line is indented, as in TeX. */
    bool indentNext = false;
    /** Paragraphs are indented at all: a minipage sets \parindent to 0. */
    bool indents = true;
    /** Nothing has been set since a heading. LaTeX leaves the paragraph
     *  that follows one unindented, whether it comes straight after the
     *  heading or after a blank line. */
    bool afterHeading = false;
    /** A document, not a label: its displays go on lines of their own. */
    bool document = false;
    /** How lines set now are aligned, in a document: centred (center,
     *  \centering), to the right (flushright, \raggedleft), or as usual. */
    Alignment align = Alignment::left;
    /** How the current line was set: aligned so when it ends. */
    Alignment lineAlign = Alignment::left;
    /** Declarations (\small, \color) whose body a document reads line by
     *  line (blockGroup): each part of it set on a line is set under them. */
    std::vector<NodeId> decls;
  };

  /** TeX's \parindent: 15pt at a 10pt font, so 1.5em, which follows the
   *  size a grob is drawn at. There is no \parskip: LaTeX separates
   *  paragraphs by this indent, not by space between them. */
  static sptr<Atom> parIndent() {
    return sptrOf<SpaceAtom>(UnitType::em, 1.5f, 0.f, 0.f);
  }

  // --- headings --------------------------------------------------------------

  /** The counters behind \thesection: one per numbered level. */
  int _section[3] = {0, 0, 0};

  /** LaTeX's own sizes: \Large for \section, \large for \subsection, and
   *  the body size below that. */
  static float headingSize(int level) {
    if (level == 0) return sizeFactor("Large");
    if (level == 1) return sizeFactor("large");
    return 1.f;
  }

  /** "1.2.3" for the heading being set, and nothing for a starred one or
   *  for \paragraph: article numbers three levels (secnumdepth 3). */
  std::string headingNumber(int level, bool starred) {
    if (starred || level > 2) return "";
    _section[level]++;
    for (int i = level + 1; i < 3; i++) _section[i] = 0;
    std::string s;
    for (int i = 0; i <= level; i++) {
      if (i > 0) s += '.';
      s += std::to_string(_section[i]);
    }
    return s;
  }

  /** A heading: its number, a quad, then its title, bold and sized. */
  sptr<Atom> heading(NodeId id) {
    const Node& x = node(id);
    const int level = headingLevel(x.text);
    const std::string number = headingNumber(level, x.star);
    if (!number.empty()) nums().current = number;
    // The number and the quad after it, and the title: the last argument
    // (the first is the short one). Each is made once -- the title's
    // warnings are said once -- and shared by both settings below.
    std::vector<sptr<Atom>> lead;
    if (!number.empty()) {
      lead = {sptrOf<TextAtom>(number, false), sptrOf<SpaceAtom>(UnitType::em, 1.f, 0.f, 0.f)};
    }
    const NodeId title = child(id, count(id) - 1);
    const sptr<Atom> text = argumentFormula(title, rawOf(title), false);
    const auto rowOf = [](const std::vector<sptr<Atom>>& atoms) {
      auto row = sptrOf<RowAtom>();
      for (const auto& a : atoms) row->add(a);
      return row;
    };
    std::vector<sptr<Atom>> whole = lead;
    whole.push_back(text);
    // The heading's font and size, around any part of it.
    const float size = headingSize(level);
    const auto styled = [&](const sptr<Atom>& a) -> sptr<Atom> {
      auto bold = sptrOf<FontStyleAtom>(FontStyle::bf, false, a);
      if (size == 1.f) return bold;
      return sized(bold, size);
    };
    // A heading on a line of its own hangs its number, as LaTeX does, if
    // the title has to be broken. \paragraph runs into its text instead.
    if (number.empty() || !isHeadingLine(x.text) || text == nullptr) return styled(rowOf(whole));
    return sptrOf<HangingAtom>(styled(rowOf(whole)), styled(rowOf(lead)), styled(text));
  }

  /** The indent a paragraph opens with, once something goes on its line.
   *  A centred or right-aligned line has none. */
  void indentIfNeeded(Label& l) {
    if (!l.indentNext) return;
    l.indentNext = false;
    // \centering and \raggedleft set \parindent to 0, as LaTeX's do.
    if (l.align == Alignment::left && l.indents) l.line->add(parIndent());
  }

  /** The current line is done: centred or put to the right, if it was set
   *  so. */
  void finishLine(Label& l) {
    if (l.lineAlign != Alignment::left && l.line->_root != nullptr) {
      l.line->_root = sptrOf<DisplayAtom>(l.line->_root, l.lineAlign);
    }
    l.lineAlign = Alignment::left;
  }

  Formula& prose(Label& l) {
    if (l.prose == nullptr) l.prose = std::make_unique<Formula>();
    return *l.prose;
  }

  /** The line a break asked for, now that something goes on it. A line
   *  with nothing drawn on it -- only \newcolumntype, say -- is none. */
  void startLine(Label& l) {
    if (!l.broken) return;
    l.broken = false;
    finishLine(l);
    if (l.rows == nullptr) {
      // As a `\\` in a formula makes it: the first line is one row.
      l.rows = std::make_unique<ArrayFormula>();
      l.rows->add(l.top->_root);
      l.line = l.rows.get();
    }
    if (!l.gap.empty()) l.rows->addRowGap(Units::getDimen(l.gap));
    l.gap.clear();
    l.rows->addRow();
  }

  /** The prose so far, as one \text{} on its line. */
  void endProse(Label& l) {
    if (l.prose != nullptr && l.prose->_root != nullptr) {
      startLine(l);
      indentIfNeeded(l);
      // A font switch in force sits inside the \rm that sets the text, or
      // the \rm would undo it; the rest of what is in force is around it.
      const sptr<Atom> styled = wrap(l, l.prose->_root, true, true);
      l.line->add(wrap(l, sptrOf<FontStyleAtom>(FontStyle::rm, false, styled), true, false));
      l.drawn = true;
      l.afterHeading = false;
      if (l.align != Alignment::left) l.lineAlign = l.align;
    }
    l.prose.reset();
  }

  /** Before prose: the spaces before it join it. */
  void beforeProse(Label& l) {
    l.afterBreak = false;
    for (const NodeId s : l.spaces) lowerItem(s, prose(l));
    l.spaces.clear();
  }

  /** Before math or an environment, which go into the line itself. */
  void beforeLine(Label& l) {
    beforeProse(l);
    endProse(l);
    startLine(l);
    indentIfNeeded(l);
    l.drawn = true;
    l.afterHeading = false;
    if (l.align != Alignment::left) l.lineAlign = l.align;
  }

  void lineBreak(Label& l, NodeId brk = kNoNode) {
    l.spaces.clear();
    endProse(l);
    if (l.drawn) {
      l.broken = true;
      const std::string gap = brk != kNoNode && count(brk) > 0 ? rawOf(child(brk, 0)) : std::string();
      if (!gap.empty()) l.gap = gap;
    }
    // A paragraph indents its first line, whether or not the break before
    // it drew anything (the first paragraph of all opens one too) -- except
    // the one that follows a heading, which LaTeX leaves flush.
    if (brk != kNoNode && node(brk).aux == 1) {
      l.indentNext = !l.afterHeading;
      l.afterHeading = false;
    }
    l.afterBreak = true;
  }

  /** The expansion of an environment the prelude or the user defines
   *  (tabular, pmatrix, cases, ...), which the expander wraps in a group:
   *  an environment all the same, not prose. */
  bool isEnvironmentGroup(NodeId id) const {
    return node(id).kind == NodeKind::group && (node(id).aux == 1 || node(id).aux == 3);
  }

  /** `$$...$$`, `\[...\]`, or an environment LaTeX sets as a display
   *  (equation, align, ...). */
  bool isDisplay(NodeId id) const {
    const Node& x = node(id);
    if (x.kind == NodeKind::math) return x.flag;
    if (x.kind == NodeKind::environment) return isDisplayEnvironment(x.text);
    return x.kind == NodeKind::group && x.aux == 3;
  }

  /** The expansion of document, table, figure or center: nothing of their
   *  own, so a label sets their content as if they were not there. */
  bool isTransparentGroup(NodeId id) const {
    const Node& x = node(id);
    return x.kind == NodeKind::group && (x.aux == 2 || (x.aux >= 4 && x.aux <= 7));
  }

  /** A list or verbatim text, which a document sets apart from its text. */
  bool isList(NodeId id) const {
    const Node& x = node(id);
    return x.kind == NodeKind::environment &&
           (x.text == "itemize" || x.text == "enumerate" || x.text == "description" ||
            x.text == "verbatim" || x.text == "verbatim*");
  }

  /** A space a break takes with it: not `~`, which TeX never drops. */
  bool isSpace(const Node& x) const { return x.kind == NodeKind::space && x.aux != 1; }

  /** Mixed mode: a label of prose with math in it, broken into lines at
   *  `\\` and at line ends in the prose (the parser made those `\\`
   *  too). Built as the R wrapper that used to turn a label into a formula
   *  built it: each stretch of prose on a line is one \text{}, inline
   *  math goes into the line itself, and the lines are the rows of the
   *  formula. A break before everything or after it is none. */
  void runLines(Formula& f, bool paragraphs) {
    Label l(f);
    l.document = paragraphs;
    // A document opens a paragraph, so its first line is indented too. It
    // starts, as TeX does, between paragraphs, where a space is nothing.
    l.indentNext = paragraphs;
    l.afterBreak = paragraphs;
    feedBody(l);
    finishLabel(l, f);
  }

  /** Everything fed: the label's last line set, and its lines made the
   *  rows of `f`. Spaces at the very end are kept in a label, as they were
   *  in the \text{}; a document's last paragraph drops them, as \par does. */
  void finishLabel(Label& l, Formula& f) {
    if (l.document) l.spaces.clear();
    beforeProse(l);
    endProse(l);
    finishLine(l);
    if (l.rows != nullptr) {
      l.rows->checkDimensions();
      f._root = l.rows->getAsVRow();
    }
  }

  /** A heading on a line of its own, with the space LaTeX leaves around
   *  it: \section's 3.5ex above and 2.3ex below, in em (1ex is about half
   *  an em). The paragraph after a heading is not indented, as in LaTeX. */
  void headingLine(Label& l, NodeId id) {
    lineBreak(l);
    if (l.drawn) l.gap = "1.75em";
    // A heading is never indented, whatever paragraph it interrupts.
    l.indentNext = false;
    beforeLine(l);
    // A heading has its own size, whatever size the text around it is.
    l.line->add(wrap(l, heading(id), false));
    lineBreak(l);
    l.gap = "1.15em";
    l.indentNext = false;
    l.afterHeading = true;
  }

  /** Something a document sets on a line of its own, `skip` above and
   *  below it: a display, centred, with TeX's 10pt \abovedisplayskip and
   *  \belowdisplayskip (1em at a 10pt font); or a list, with \topsep's
   *  8pt. It interrupts a paragraph without ending it, so what follows
   *  goes on unindented, as in TeX. */
  void blockLine(Label& l, NodeId id, bool display, const char* skip) {
    lineBreak(l);
    if (l.drawn) l.gap = skip;
    l.indentNext = false;
    beforeLine(l);
    Formula g;
    if (node(id).kind == NodeKind::math) {
      auto m = displayBody(child(id, 0));
      g.add(sptrOf<StyleAtom>(TexStyle::display, m == nullptr ? sptrOf<EmptyAtom>() : m));
    } else {
      lowerItem(id, g);
    }
    auto atom = g._root == nullptr ? sptrOf<EmptyAtom>() : g._root;
    // An equation or align is in display style, as `\[...\]` is.
    if (display && node(id).kind != NodeKind::math) atom = sptrOf<StyleAtom>(TexStyle::display, atom);
    atom = wrap(l, atom);
    l.line->add(display ? sptrOf<DisplayAtom>(atom) : atom);
    lineBreak(l);
    l.gap = skip;
    l.indentNext = false;
  }

  /** The content of a group a document sets apart from its paragraphs,
   *  `skip` above and below: a float (table, figure) where it is written,
   *  as LaTeX's [h] placement sets one, with \intextsep's 12pt; or center,
   *  flushleft or flushright, their lines aligned so, with \topsep's 8pt.
   *  A \centering in any of them lasts to its end. */
  void blockLines(Label& l, NodeId id, Alignment align, const char* skip) {
    lineBreak(l);
    if (l.drawn) l.gap = skip;
    l.indentNext = false;
    const Alignment was = l.align;
    l.align = align;
    feedLines(l, child(id, 0));
    lineBreak(l);
    l.align = was;
    l.gap = skip;
    l.indentNext = false;
  }

  /** `atom` under the declarations whose bodies a document is reading line
   *  by line, innermost first; `sizes` false leaves out \small and kin. */
  sptr<Atom> wrap(const Label& l, sptr<Atom> atom, bool sizes = true,
                  std::optional<bool> fonts = std::nullopt) {
    for (auto it = l.decls.rbegin(); it != l.decls.rend(); ++it) {
      if (!sizes && isSize(node(*it).text)) continue;
      // Only the font switches, or none of them.
      if (fonts && *fonts != isTextFont(node(*it).text)) continue;
      const sptr<Atom> inner = atom;
      auto wrapped = declarationAtom(*it, [&] { return inner; });
      if (wrapped != nullptr) atom = wrapped;
    }
    return atom;
  }

  /** Math or an environment, into the line itself. */
  void placeOnLine(Label& l, NodeId id) {
    beforeLine(l);
    if (l.decls.empty()) {
      lowerItem(id, *l.line);
      return;
    }
    Formula g;
    lowerItem(id, g);
    if (g._root != nullptr) l.line->add(wrap(l, g._root));
  }

  /** Whether `id` holds what only a document's lines can set: a paragraph
   *  break, a heading, a display, a list, a float, \centering, \noindent. */
  bool hasBlock(NodeId id) {
    if (_blocks.empty()) _blocks.assign(_ast.size(), 0);
    if (_blocks[id] != 0) return _blocks[id] == 2;
    const Node& x = node(id);
    bool found = false;
    switch (x.kind) {
      case NodeKind::list:
        for (std::uint32_t i = 0; i < count(id) && !found; i++) found = hasBlock(child(id, i));
        break;
      case NodeKind::group:
        // A unit (tabular, pmatrix) sets nothing of the kind inside it.
        found = x.aux != 1 && (x.aux != 0 || hasBlock(child(id, 0)));
        break;
      case NodeKind::declaration:
        found = hasBlock(child(id, count(id) - 1));
        break;
      case NodeKind::command:
        found = !x.flag && ((isBreakNode(x) && x.aux == 1) || isHeading(x.text) ||
                            x.text == "noindent" || isLineAlignment(x.text));
        break;
      case NodeKind::environment:
        found = isList(id) || isDisplayEnvironment(x.text);
        break;
      case NodeKind::math:
        found = x.flag;
        break;
      default:
        break;
    }
    _blocks[id] = found ? 2 : 1;
    return found;
  }

  /** A group or a declaration with a document's block structure inside it
   *  (`{\small one\n\ntwo}`, `\color{red}\section{A}`): its content goes
   *  onto the lines as anything else does, a declaration applied to each
   *  part of it set there, as TeX carries a font change across the
   *  paragraphs of its group. \centering ends with the group. */
  void blockGroup(Label& l, NodeId id) {
    const Node& x = node(id);
    const bool declaration = x.kind == NodeKind::declaration;
    // What came before is set without the declaration.
    beforeProse(l);
    endProse(l);
    const Alignment was = l.align;
    if (declaration) l.decls.push_back(id);
    feedLines(l, declaration ? child(id, count(id) - 1) : child(id, 0));
    endProse(l);
    if (declaration) l.decls.pop_back();
    l.align = was;
  }

  /** The items of `list` onto the label's lines. */
  void feedLines(Label& l, NodeId list) {
    for (std::uint32_t i = 0; i < count(list); i++) feedItem(l, child(list, i));
  }

  /** A whole input onto the label's lines, but for its preamble, which
   *  LaTeX does not draw (what follows \end{document} was never read). The
   *  preamble is still lowered, quietly, for what its commands do
   *  (\definecolor, \newcolumntype, \graphicspath), into lines that are
   *  thrown away. */
  void feedBody(Label& l) {
    const NodeId root = _ast.root;
    for (std::uint32_t i = 0; i < count(root); i++) {
      const NodeId id = child(root, i);
      if (i >= _ast.preamble) {
        feedItem(l, id);
        continue;
      }
      Formula scratch;
      Label preamble(scratch);
      preamble.document = l.document;
      const Diagnostics::Quiet quiet(_diags);
      const bool was = _inPreamble;
      _inPreamble = true;
      feedItem(preamble, id);
      _inPreamble = was;
    }
  }

  /** One item of a list onto the label's lines. */
  void feedItem(Label& l, NodeId id) {
    const Node& x = node(id);
    {
      if (l.document && x.kind == NodeKind::group && x.aux >= 4 && x.aux <= 7) {
        // A float (4), center (5), flushleft (6) or flushright (7).
        const Alignment align = x.aux == 5 ? Alignment::center
                                : x.aux == 7 ? Alignment::right
                                             : Alignment::left;
        blockLines(l, id, align, x.aux == 4 ? "1.2em" : "0.8em");
      } else if (isTransparentGroup(id)) {
        // \centering lasts to the end of the group it is in.
        const Alignment was = l.align;
        feedLines(l, child(id, 0));
        l.align = was;
      } else if (l.document && (x.kind == NodeKind::declaration ||
                                (x.kind == NodeKind::group && x.aux == 0)) && hasBlock(id)) {
        blockGroup(l, id);
      } else if (l.document && isDisplay(id)) {
        blockLine(l, id, true, "1em");
      } else if (l.document && isList(id)) {
        blockLine(l, id, false, "0.8em");
      } else if (l.document && x.kind == NodeKind::command && !x.flag && x.text == "caption") {
        // On a line of its own, unindented; the parser ended its line.
        lineBreak(l);
        l.indentNext = false;
        beforeProse(l);
        lowerItem(id, prose(l));
      } else if (l.document && x.kind == NodeKind::command && !x.flag && x.text == "paragraph") {
        // \paragraph starts a paragraph of its own, flush left with LaTeX's
        // 3.25ex above it, and runs into it.
        lineBreak(l);
        if (l.drawn) l.gap = "1.625em";
        l.indentNext = false;
        beforeProse(l);
        lowerItem(id, prose(l));
      } else if (isBreakNode(x)) {
        lineBreak(l, id);
      } else if (x.kind == NodeKind::command && !x.flag && x.text == "noindent") {
        // LaTeX's one way to say this paragraph is not indented.
        l.indentNext = false;
      } else if (x.kind == NodeKind::command && !x.flag && isLineAlignment(x.text)) {
        // Aligns the paragraph it is in and those after it, to the end of
        // the group; a label is a grob's own business, so only a document's.
        if (l.document) {
          l.align = x.text == "centering"    ? Alignment::center
                    : x.text == "raggedleft" ? Alignment::right
                                             : Alignment::left;
          if (!l.broken) l.lineAlign = l.align;
        }
      } else if (x.kind == NodeKind::command && !x.flag && isHeadingLine(x.text)) {
        headingLine(l, id);
      } else if (isSpace(x)) {
        if (!l.afterBreak) l.spaces.push_back(id);
      } else if (x.kind == NodeKind::math) {
        const NodeId list = child(id, 0);
        if (x.flag) {
          // Display style for the display math alone.
          beforeLine(l);
          auto g = displayBody(list);
          l.line->add(sptrOf<StyleAtom>(TexStyle::display, g == nullptr ? sptrOf<EmptyAtom>() : g));
          return;
        }
        // A span of math is a list of its own, as in TeX: \over, \limits
        // and a script take what is before them in the span, never the
        // prose, and a sign that opens it is unary. To the prose it is an
        // ordinary atom, with no math spacing against the text.
        auto g = std::make_unique<Formula>();
        const auto place = [&]() {
          if (g->_root != nullptr) {
            beforeLine(l);
            l.line->add(wrap(l, sptrOf<TypedAtom>(AtomType::ordinary, AtomType::ordinary, g->_root)));
          }
          g = std::make_unique<Formula>();
        };
        for (std::uint32_t j = 0; j < count(list); j++) {
          const NodeId m = child(list, j);
          if (isBreakNode(node(m))) {
            place();
            lineBreak(l, m);
            continue;
          }
          lowerItem(m, *g);
        }
        place();
      } else if (x.kind == NodeKind::environment || isEnvironmentGroup(id)) {
        placeOnLine(l, id);
      } else if (!hasBreak(id)) {
        beforeProse(l);
        lowerItem(id, prose(l));
      } else {
        const auto pieces = piecesOf(id);
        for (std::size_t k = 0; k < pieces.size(); k++) {
          if (k > 0) lineBreak(l);
          if (pieces[k].atom == nullptr) continue;
          beforeProse(l);
          put(pieces[k], prose(l));
        }
      }
    }
  }

  // --- environments ----------------------------------------------------------

  /** `list` set as a document of its own -- paragraphs, displays and lists
   *  apart, \centering's lines centred -- with no paragraph indent, as
   *  LaTeX's minipage sets \parindent to 0. It starts between paragraphs,
   *  as a document does, so the line end after `\begin{minipage}{..}` is
   *  nothing, and its last paragraph drops the spaces that end it. */
  sptr<Atom> subDocument(NodeId list) {
    Formula f;
    Label l(f);
    l.document = true;
    l.indents = false;
    l.afterBreak = true;
    feedLines(l, list);
    finishLabel(l, f);
    return f._root;
  }

  /** \begin{minipage}[position][height][inner position]{width}: its
   *  body set to its width, and to its height when that is taller. The
   *  inner position is the position unless given, as in LaTeX. */
  sptr<Atom> minipage(NodeId id) {
    // The first letter of an optional argument, or `fallback`.
    const auto letter = [&](NodeId arg, const char* allowed, char fallback) {
      const std::string s = rawOf(arg);
      const auto at = s.find_first_not_of(" \t\r\n");
      if (at == std::string::npos || std::strchr(allowed, s[at]) == nullptr) return fallback;
      return s[at];
    };
    const char p = letter(child(id, 0), "tb", 'c');
    const char inner = letter(child(id, 2), "tbcs", p);
    const Dimen height = Units::getDimen(rawOf(child(id, 1)));
    const Dimen width = Units::getDimen(rawOf(child(id, 3)));
    return sptrOf<MinipageAtom>(subDocument(child(id, count(id) - 1)), width, p, height, inner);
  }

  /** Built by the engine's environment handler from its source text, as
   *  the old parser's `\name@@env{...}{body}` did. */
  sptr<Atom> environment(NodeId id) {
    const Node& x = node(id);
    if (x.text == "minipage") return minipage(id);
    if (x.text == "verbatim" || x.text == "verbatim*") return verbatimBlock(x);
    const EnvSpec* spec = findEnvironment(x.text);
    const std::string macName = (spec != nullptr ? x.text : std::string("matrix")) + "@@env";
    MacroInfo* mac = MacroInfo::get(macName);
    if (mac == nullptr) return unknownAtom("begin");
    std::vector<std::string> args(static_cast<std::size_t>(mac->argc) + 12);
    args[0] = macName;
    std::size_t k = 1;
    for (std::uint32_t i = 0; i < count(id); i++) {
      const NodeId c = child(id, i);
      if (node(c).kind != NodeKind::argument) break;
      // An optional argument is LaTeX's position ([t]), which no builder
      // takes.
      if (spec != nullptr && i < spec->args.size() && spec->args[i].optional) continue;
      if (k < args.size()) args[k++] = rawOf(c);
    }
    // The old template put the body between spaces.
    if (k < args.size()) args[k] = " " + stripComments(x.raw) + " ";
    Node at = x;
    at.text = "begin{" + x.text + "}";
    auto* cm = dynamic_cast<CommandMacro*>(mac);
    if (cm == nullptr) return unknownAtom(at.text);
    std::vector<NodeId> nodes(args.size(), kNoNode);
    if (k < nodes.size()) nodes[k] = id;
    // Math, unless its cells or items are text: a list's handler asks.
    TreeArgs a(*this, !x.flag, std::move(args), std::move(nodes), nullptr);
    a.reportAt(x.span, "\\" + at.text);
    try {
      return cm->call(a);
    } catch (const std::exception& e) {
      _diags.warn(at.span, "\\" + at.text + ": " + clean(e.what()) + "; drawn as its name");
      return unknownAtom(at.text);
    }
  }

  /** An alignment's body, from its rows and cells, in the order the old
   *  parser filled its formula: `&` starts a cell, `\\` and \cr end a row,
   *  and a rule or \intertext ends one itself (command()). */
  sptr<ArrayFormula> alignmentOf(NodeId env) {
    auto arr = sptrOf<ArrayFormula>();
    arr->_stretch = _stretch;
    _stretch = 1.f;
    if (env == kNoNode || node(env).kind != NodeKind::environment) return arr;
    if (isTblrEnvironment(node(env).text)) return tblrAlignmentOf(env);
    const bool text = node(env).flag;
    // The rows of align, gather, ... are numbered, unless the environment is
    // starred; a multline has one number, on its last line.
    const bool family = isNumberedEnvironment(node(env).text);
    const bool automatic = family && !node(env).star;
    const bool once = family && node(env).text == "multline";
    // What the row of an enclosing display was told is set aside: this
    // alignment's rows are told their own.
    RowNote outer = std::move(_note);
    _note = RowNote();
    _inRow++;
    int lastRow = -1;
    std::vector<NodeId> rows;
    for (std::uint32_t r = 0; r < count(env); r++) {
      if (node(child(env, r)).kind == NodeKind::row) rows.push_back(child(env, r));
    }
    // A longtable's head and foot rows are set once: first and last.
    const bool longtable = node(env).text == "longtable";
    if (longtable) rows = longtableOrder(rows);
    for (std::size_t k = 0; k < rows.size(); k++) {
      const NodeId row = rows[k];
      if (longtable && k == 0 && captionRow(env, row, *arr)) {
        arr->addRow();
        continue;
      }
      for (std::uint32_t c = 0; c < count(row); c++) {
        if (c > 0) arr->addCol();
        if (text) {
          textCell(child(child(row, c), 0), *arr);
        } else {
          lowerList(child(child(row, c), 0), *arr, 0);
        }
      }
      const std::string& end = node(row).text;
      // A rule or \intertext ends its row too, and is not an equation.
      const bool marker = isLongtableMarker(end);
      const bool equation = end.empty() || end == "\\" || end == "cr" || marker;
      const bool blank = isBlankRow(row);
      if (equation && !blank) lastRow = arr->rows();
      if (family && equation && !once) finishRow(*arr, arr->rows(), automatic && !blank);
      if (!node(row).raw.empty()) arr->addRowGap(Units::getDimen(node(row).raw));
      // A row moved from the end of the table has no end of its own.
      if (end == "\\" || end == "cr" || marker || (end.empty() && k + 1 < rows.size())) {
        arr->addRow();
      }
    }
    if (family && once && lastRow >= 0) finishRow(*arr, lastRow, automatic);
    _inRow--;
    RowNote inner = std::move(_note);
    _note = std::move(outer);
    if (!family) {
      // A split or aligned in an equation: its \label, \tag and \notag are
      // the equation's.
      if (inner.tag != nullptr && _note.tag == nullptr) {
        _note.tag = inner.tag;
        _note.tagSource = inner.tagSource;
        _note.star = inner.star;
      }
      _note.notag = _note.notag || inner.notag;
      _note.labels.insert(_note.labels.end(), inner.labels.begin(), inner.labels.end());
    }
    return arr;
  }

  static bool isLongtableMarker(const std::string& name) {
    return name == "endhead" || name == "endfirsthead" || name == "endfoot" ||
           name == "endlastfoot";
  }

  /** A longtable's rows, in the order a single page sets them: the first
   *  head (the head, where there is no first one), the rows, and the last
   *  foot (the foot). A group ends at the row that has its marker. */
  std::vector<NodeId> longtableOrder(const std::vector<NodeId>& rows) const {
    std::vector<NodeId> firstHead, head, foot, lastFoot, group;
    for (const NodeId row : rows) {
      group.push_back(row);
      const std::string& end = node(row).text;
      std::vector<NodeId>* to = end == "endfirsthead"  ? &firstHead
                                : end == "endhead"     ? &head
                                : end == "endfoot"     ? &foot
                                : end == "endlastfoot" ? &lastFoot
                                                       : nullptr;
      if (to == nullptr) continue;
      to->insert(to->end(), group.begin(), group.end());
      group.clear();
    }
    std::vector<NodeId> order = firstHead.empty() ? head : firstHead;
    order.insert(order.end(), group.begin(), group.end());
    const std::vector<NodeId>& tail = lastFoot.empty() ? foot : lastFoot;
    order.insert(order.end(), tail.begin(), tail.end());
    return order;
  }

  /** How many columns a column specification sets: its letters, with what
   *  is in braces after p, m, b, @, !, > and < left out, and *{n}{cols}
   *  counted n times. */
  static int columnCount(const std::string& spec, int depth = 0) {
    int n = 0;
    // The index of the `}` that closes the `{` at or after `from`.
    const auto close = [&](std::size_t from) {
      std::size_t j = from;
      for (int open = 0; j < spec.size(); j++) {
        if (spec[j] == '{') open++;
        if (spec[j] == '}' && --open == 0) break;
      }
      return j;
    };
    for (std::size_t i = 0; i < spec.size(); i++) {
      const char c = spec[i];
      const bool braced = i + 1 < spec.size() && spec[i + 1] == '{';
      if (c == '@' || c == '!' || c == '>' || c == '<') {
        i = close(i + 1);
      } else if (c == 'p' || c == 'm' || c == 'b' || c == 'X') {
        n++;
        if (braced) i = close(i + 1);
      } else if (c == '*' && depth < 8) {
        const std::size_t first = close(i + 1);
        const int times = std::atoi(spec.substr(i + 2, first - i - 2).c_str());
        const std::size_t last = close(first + 1);
        n += std::min(std::max(times, 0), 1000) *
             columnCount(spec.substr(first + 2, last - first - 2), depth + 1);
        i = last;
      } else if (c == 'l' || c == 'c' || c == 'r' || c == 'S' || c == 'Q') {
        n++;
        if (i + 1 < spec.size() && spec[i + 1] == '[') {
          const std::size_t end = spec.find(']', i + 1);
          i = end == std::string::npos ? spec.size() : end;
        }
      }
    }
    return n;
  }

  /** A longtable's first row, when it holds only its caption: set as a
   *  line centred across the table, as LaTeX sets one above it. */
  bool captionRow(NodeId env, NodeId row, ArrayFormula& arr) {
    if (count(row) != 1) return false;
    const NodeId list = child(child(row, 0), 0);
    std::uint32_t i = 0;
    while (i < count(list) && isSpace(node(child(list, i)))) i++;
    if (i >= count(list)) return false;
    const NodeId cap = child(list, i);
    if (node(cap).kind != NodeKind::command || node(cap).flag || node(cap).text != "caption") {
      return false;
    }
    // The environment's arguments are [position]{columns}: the last one
    // that was given is the columns.
    int columns = 1;
    for (std::uint32_t a = 0; a < count(env); a++) {
      const Node& arg = node(child(env, a));
      if (arg.kind == NodeKind::argument && arg.flag && !arg.raw.empty()) {
        columns = columnCount(arg.raw);
      }
    }
    columns = std::min(std::max(columns, 1), 1 << 14);
    arr.add(sptrOf<MulticolumnAtom>(columns, "c", commandAtom(cap, arr)));
    if (columns > 1) arr.addCol(columns);
    return true;
  }

  /** The rules of a tblr at one position (above row `at`), those that
   *  meet joined: one row of the table draws one rule. */
  static std::vector<TblrRule> rulesAt(const TblrSpec& spec, int at, int rows, int columns) {
    std::vector<TblrRule> found;
    for (TblrRule r : spec.horizontal) {
      if (!r.at.matches(at, rows + 1)) continue;
      if (r.from == 0) r.from = 1;
      if (r.to == 0 || r.to > columns) r.to = columns;
      found.push_back(r);
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const TblrRule& a, const TblrRule& b) { return a.from < b.from; });
    std::vector<TblrRule> merged;
    for (const TblrRule& r : found) {
      if (!merged.empty() && r.from <= merged.back().to + 1) {
        merged.back().to = std::max(merged.back().to, r.to);
      } else {
        merged.push_back(r);
      }
    }
    return merged;
  }

  /** A tblr (tabularray's table, tinytable's) as an alignment: the rules
   *  its spec puts between rows, and the spans, colours, fonts and
   *  alignments it sets for cells. */
  sptr<ArrayFormula> tblrAlignmentOf(NodeId env) {
    auto arr = sptrOf<ArrayFormula>();
    const bool text = node(env).flag;
    const NodeId outerArg = count(env) > 0 ? child(env, 0) : kNoNode;
    const NodeId innerArg = count(env) > 1 ? child(env, 1) : kNoNode;
    TblrSpec spec = parseTblr(outerArg == kNoNode ? std::string() : rawOf(outerArg),
                              innerArg == kNoNode ? std::string() : rawOf(innerArg));
    std::vector<NodeId> rows;
    for (std::uint32_t r = 0; r < count(env); r++) {
      if (node(child(env, r)).kind == NodeKind::row) rows.push_back(child(env, r));
    }
    // The empty row after the last \\ is no row of the table.
    while (!rows.empty() && isBlankRow(rows.back()) && node(rows.back()).text.empty()) {
      rows.pop_back();
    }
    int columns = 0;
    for (const NodeId row : rows) columns = std::max(columns, static_cast<int>(count(row)));
    const int n = static_cast<int>(rows.size());

    // A caption, as a longtable's: a line across the table, above it.
    if (!spec.caption.empty() && columns > 0) {
      auto caption = sptrOf<FontStyleAtom>(FontStyle::rm, false, fragment(spec.caption, false));
      arr->add(sptrOf<MulticolumnAtom>(columns, "c", caption));
      if (columns > 1) arr->addCol(columns);
      arr->addRow();
    }

    const auto rule = [&](int at) {
      for (const TblrRule& r : rulesAt(spec, at, n, columns)) {
        auto line = sptrOf<HlineAtom>();
        if (r.thickness.isValid()) line->setThickness(r.thickness.val, r.thickness.unit);
        if (r.from > 1 || r.to < columns) line->setColumnRange(r.from - 1, r.to - 1);
        arr->add(line);
        arr->addRow();
      }
    };

    bool noted = false;
    for (int k = 0; k < n; k++) {
      const NodeId row = rows[static_cast<std::size_t>(k)];
      rule(k + 1);
      const std::uint32_t cells = count(row);
      for (std::uint32_t c = 0; c < cells;) {
        if (c > 0) arr->addCol();
        const NodeId list = child(child(row, c), 0);
        if (text) {
          textCell(list, *arr);
        } else {
          lowerList(list, *arr, 0);
        }
        const TblrSetting s = tblrCell(spec, k + 1, static_cast<int>(c) + 1, n, columns);
        if (s.rowspan > 1 && !noted) {
          noted = true;
          _diags.warn(node(env).span, "tabularray: a row span (r=) is not supported: left out");
        }
        if (!s.font.empty()) {
          arr->_root = sptrOf<FontStyleAtom>(FontContext::mainFontStyleOf(s.font), false, arr->_root,
                                              true);
        }
        if (!s.background.empty()) {
          arr->addCellSpecifier(sptrOf<CellColorAtom>(ColorAtom::getColor(s.background)));
        }
        if (!s.foreground.empty()) {
          arr->addCellSpecifier(sptrOf<CellForegroundAtom>(ColorAtom::getColor(s.foreground)));
        }
        const int span = std::min(s.colspan, static_cast<int>(cells - c));
        if (span > 1 || !s.halign.empty()) {
          arr->_root = sptrOf<MulticolumnAtom>(std::max(span, 1), s.halign.empty() ? "l" : s.halign,
                                               arr->_root);
          if (span > 1) arr->addCol(span);
        }
        // What it spans was written as cells of its own, which are empty.
        c += static_cast<std::uint32_t>(std::max(span, 1));
      }
      if (!node(row).raw.empty()) arr->addRowGap(Units::getDimen(node(row).raw));
      arr->addRow();
    }
    rule(n + 1);
    for (const std::string& key : spec.unsupported) {
      _diags.warn(node(env).span, "tabularray: `" + key + "' is not supported: left out");
    }
    return arr;
  }

  /** A row with nothing in it: the one a final \\ starts. */
  bool isBlankRow(NodeId row) const {
    for (std::uint32_t c = 0; c < count(row); c++) {
      const NodeId list = child(child(row, c), 0);
      for (std::uint32_t i = 0; i < count(list); i++) {
        if (!isSpace(node(child(list, i)))) return false;
      }
    }
    return true;
  }

  // --- equation numbers, \tag, \label and \ref ------------------------------

  /** `(text)` in the upright font, as LaTeX sets an equation's number. */
  static sptr<Atom> numberOf(const std::string& text, bool parentheses) {
    return sptrOf<FontStyleAtom>(FontStyle::rm, false,
                                 literalText(parentheses ? "(" + text + ")" : text));
  }

  /** \tag{text} and \tag*{text}, of the row or display being lowered. */
  void noteTag(NodeId id, bool star) {
    const NodeId text = child(id, 0);
    const auto body = argumentFormula(text, rawOf(text), false);
    if (body == nullptr) return;
    _note.tag = sptrOf<FontStyleAtom>(FontStyle::rm, false, body);
    _note.tagSource = trimmed(rawOf(text));
    _note.star = star;
  }

  /** A row, or a display, is done: its number or tag, and its labels. */
  void finishRow(ArrayFormula& arr, int row, bool numbered) {
    const RowNote n = std::move(_note);
    _note = RowNote();
    sptr<Atom> tag;
    std::string text;
    if (n.tag != nullptr) {
      // \tag takes the parentheses itself, which \tag* leaves out.
      tag = n.star ? n.tag : sptrOf<RowAtom>();
      if (!n.star) {
        auto* parts = static_cast<RowAtom*>(tag.get());
        parts->add(numberOf("(", false));
        parts->add(n.tag);
        parts->add(numberOf(")", false));
      }
      text = n.tagSource;
    } else if (numbered && !n.notag) {
      text = std::to_string(++nums().equation);
      tag = numberOf(text, true);
    }
    if (tag != nullptr) {
      arr._rowTags[row] = tag;
      nums().current = text;
    }
    for (const auto& key : n.labels) (*nums().labels)[key] = nums().current;
  }

  /** The content of a display, `$$...$$` or `\[...\]`: its list, with a
   *  \tag it holds set at the right of it. */
  sptr<Atom> displayBody(NodeId list) {
    RowNote outer = std::move(_note);
    _note = RowNote();
    _inRow++;
    const sptr<Atom> body = build(list);
    _inRow--;
    const bool tagged = _note.tag != nullptr;
    auto arr = sptrOf<ArrayFormula>();
    if (body != nullptr) arr->add(body);
    finishRow(*arr, 0, false);
    _note = std::move(outer);
    if (!tagged) return body;
    // One centred line, which already knows where its tag goes.
    arr->checkDimensions();
    return sptrOf<MultlineAtom>(false, arr, MultiLineType::gather);
  }

  /** \label: what it names is the number of the row or display it is in,
   *  known when that ends; elsewhere, the last number set. */
  void label(const std::string& key) {
    if (_inRow > 0) {
      _note.labels.push_back(key);
    } else {
      (*nums().labels)[key] = nums().current;
    }
  }

  /** \ref and \eqref, drawn when the whole input has been read. */
  sptr<Atom> reference(NodeId id, bool parentheses) {
    const std::string key = trimmed(rawOf(child(id, 0)));
    if (_numbering.outermost()) _refs.push_back({key, node(id).span, parentheses});
    return sptrOf<RefAtom>(nums().labels, key, parentheses);
  }

  /** What the input leaves undefined, said once all of it is read: a
   *  reference may come before its label. */
  void reportReferences() {
    for (const auto& ref : _refs) {
      if (nums().labels->count(ref.key) > 0) continue;
      // `?\?)` so that `??)` is not read as a trigraph.
      _diags.warn(ref.at, "reference `" + ref.key + "' is undefined: drawn as " +
                            (ref.parentheses ? "(?\?)" : "??"));
    }
  }

  /** A command that works on the alignment it is in, not on the cell's
   *  content: a rule, \intertext, \multicolumn, \cellcolor ... */
  static bool actsOnAlignment(const Node& x) {
    static const std::set<std::string> names = {"intertext", "multicolumn", "multirow",
                                                "hdotsfor", "cellcolor", "rowcolor"};
    return x.kind == NodeKind::command && !x.flag && (isRule(x.text) || names.count(x.text) > 0);
  }

  /** A cell of a table met in text: text, as LaTeX sets a tabular's, less
   *  the spaces at either end (LaTeX's column template drops them). What
   *  works on the alignment itself goes into it as in any cell, and the
   *  text either side of it is one \text{} each. */
  void textCell(NodeId list, ArrayFormula& arr) {
    std::uint32_t from = 0, to = count(list);
    while (from < to && isSpace(node(child(list, from)))) from++;
    while (to > from && isSpace(node(child(list, to - 1)))) to--;
    std::unique_ptr<Formula> text;
    const auto flush = [&] {
      if (text != nullptr && text->_root != nullptr) {
        // Nested, so the font a column or a group around the table sets stays.
        arr.add(sptrOf<FontStyleAtom>(FontStyle::rm, false, text->_root, true));
      }
      text.reset();
    };
    for (std::uint32_t i = from; i < to; i++) {
      const NodeId id = child(list, i);
      if (actsOnAlignment(node(id))) {
        flush();
        lowerItem(id, arr);
        continue;
      }
      if (text == nullptr) text = std::make_unique<Formula>();
      lowerItem(id, *text);
    }
    flush();
  }
};

}  // namespace

void lowerInto(const Ast& ast, Formula& formula, Diagnostics& diagnostics, bool lines,
               bool paragraphs) {
  Lowerer lowerer(ast, diagnostics);
  lowerer.startNumbering();
  lowerer.run(formula, lines, paragraphs);
  lowerer.endNumbering();
}

void setStartNumbering(NumberingState start) {
  pendingNumbering() = std::move(start);
}

const NumberingState& lastNumbering() {
  return finalNumbering();
}

sptr<Atom> buildFragment(const std::string& latex, bool math) {
  if (latex.empty()) return nullptr;
  // A fragment is parsed afresh, its depth from 0, so a fragment inside a
  // fragment (a raw argument in a raw argument) is counted here.
  static int depth = 0;
  if (depth >= kMaxDepth) throw ex_parse("Input nested too deeply");
  struct Deeper {
    Deeper() { depth++; }
    ~Deeper() { depth--; }
  } deeper;
  Diagnostics unreported;
  const Ast ast = parseLatex(latex, math ? Mode::math : Mode::text, unreported);
  if (ast.root == kNoNode) return nullptr;
  Formula g;
  Lowerer(ast, unreported).run(g, false, false);
  return g._root;
}

}  // namespace microtex::front
