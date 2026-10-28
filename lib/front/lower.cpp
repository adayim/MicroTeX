#include "front/lower.h"

#include "front/siunitx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
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
#include "front/colspec.h"
#include "front/front.h"
#include "front/hooks.h"
#include "front/lengths.h"
#include "front/spec.h"
#include "front/tblr.h"
#include "graphic/graphic.h"
#include "macro/macro.h"
#include "macro/macro_args.h"
#include "macro/macro_styles.h"
#include "unimath/uni_font.h"
#include "utils/counter_format.h"
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

/** TeX points in one of a length unit that does not depend on the font, or 0 for one that does (`em`, `ex`, `mu`). */
double pointsPer(UnitType u) {
  switch (u) {
    case UnitType::pt: return 1.0;
    case UnitType::point: return 72.27 / 72.0;
    case UnitType::pica: return 12.0;
    case UnitType::cm: return 72.27 / 2.54;
    case UnitType::mm: return 72.27 / 25.4;
    case UnitType::in: return 72.27;
    default: return 0.0;
  }
}

/** The cell `\gmcell` stands for while a tblr applies a `cmd=` to it: the fragment that holds the command is read
 *  as input of its own, which can only be handed this by a variable. */
thread_local sptr<Atom> gTblrCell;

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
  /** Inside `subequations`, the equation counter stays at the number the
   *  environment took, and each numbered row after it is that number and a
   *  letter: 3a, 3b. */
  bool sub = false;
  int subIndex = 0;
  /** A counter: a theorem's, with the section it was last stepped in (it
   *  starts again from 1 in the next), or one \newcounter made, with the
   *  counter it is `parent` of (\newcounter{c}[parent]), which sets it to
   *  zero when it steps. */
  struct Counter {
    int value = 0;
    std::string parent;
  };
  std::map<std::string, Counter> counters{{"footnote", Counter()}, {"figure", Counter()}, {"table", Counter()}};
  /** The page notes so far, (mark, text as written): a document sets them after its body (finishNotes()). */
  std::vector<std::pair<std::string, std::string>> notes;
  /** What a \label names now, as \@currentlabel: the number of the last
   *  equation or heading. */
  std::string current;
  /** What kind of thing `current` numbers, for \cref and \autoref: equation, figure, table, section, subsection,
   *  subsubsection, paragraph, `thm:` and the environment's title, or "" for a number of no kind. */
  std::string currentKind;
  /** label -> the kind of what it names. Shared with the references, as `labels` is. */
  std::shared_ptr<std::map<std::string, std::string>> kinds =
    std::make_shared<std::map<std::string, std::string>>();
  /** \cref, \Cref, \autoref and the range forms of the whole input, checked once it is read. */
  struct CrossRef {
    std::vector<std::string> keys;
    SourceSpan at;
  };
  std::vector<CrossRef> crossrefs;
  /** label -> the text a reference to it draws, as LaTeX source. Shared
   *  with the references, which draw only when the whole input has been
   *  read: one may come before its label. */
  std::shared_ptr<std::map<std::string, std::string>> labels =
    std::make_shared<std::map<std::string, std::string>>();
  /** What `\subref` draws for a label: the letter of its subfigure or subtable, "" where it is neither. */
  std::shared_ptr<std::map<std::string, std::string>> sublabels =
    std::make_shared<std::map<std::string, std::string>>();
  std::string subCurrent;
  /** thebibliography's items: key -> what \cite draws for it, its label or
   *  its number. Shared, as `labels` is, with the citations, which draw only
   *  when the whole input has been read. */
  std::shared_ptr<std::map<std::string, std::string>> bibitems =
    std::make_shared<std::map<std::string, std::string>>();
  /** The unlabelled items so far, which number them. */
  int bibcount = 0;
  /** The citations of the whole input, checked once it is read. A table cell
   *  or a list item is lowered by a Lowerer of its own, so they are kept here
   *  and not in the Lowerer. */
  struct Citation {
    std::vector<std::string> keys;
    SourceSpan at;
    std::string form;
  };
  std::vector<Citation> cites;
  /** \parindent, \tabcolsep and the lengths the input makes. */
  Lengths lengths;
};

Numbering* numbering = nullptr;

/** What Units::getDimen() asks for a length named in a dimension. */
bool resolveLength(const std::string& name, Dimen& out) {
  return numbering != nullptr && numbering->lengths.get(name, out);
}

/** Lengths set inside a group are what they were once it ends. */
class LengthScope {
public:
  LengthScope() : _mark(numbering != nullptr ? numbering->lengths.mark() : 0) {}
  ~LengthScope() {
    if (numbering != nullptr) numbering->lengths.release(_mark);
  }
  LengthScope(const LengthScope&) = delete;
  LengthScope& operator=(const LengthScope&) = delete;

private:
  std::size_t _mark;
};

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
    if (_outer == nullptr) {
      numbering = &_own;
      Units::setLengthResolver(&resolveLength);
    }
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

/** A float's caption with `\abovecaptionskip` over it: TeX puts the glue in the vertical list, so no baselineskip
 *  follows it and the distance from the line above is its depth, the skip and the caption's height. The box takes the
 *  skip as height, less the line skip the stacking adds when the two are too close for the baselineskip. */
class SkipAboveAtom : public Atom {
public:
  /** `ems` of the text around: LaTeX's 10pt and 6pt are 1em and 0.6em of its 10pt class, and a grob's size varies. */
  SkipAboveAtom(sptr<Atom> base, float ems) : _base(std::move(base)), _ems(ems) {}

  sptr<Box> createBox(Env& env) override {
    sptr<Box> box = _base->createBox(env);
    const float skip = _ems * env.em() - env.baselineSkip() / 12.f;
    if (skip > 0.f) box->_height += skip;
    return box;
  }

private:
  sptr<Atom> _base;
  float _ems;
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

/** \cref, \Cref, \autoref and \crefrange: the kind of each label named and its number, as cleveref and hyperref set
 *  them -- "eq. (1)", "Figure 2", "eqs. (1) to (3) and (5)", "eq. (1), fig. 1, and table 2" -- looked up when the atom
 *  is built into a box, the labels being known by then, and a bold ?? for one that is not. Read against pdflatex's
 *  output (scratchpad/col/rc.tex, c.tex): the names are cleveref's defaults in English (abbreviated by \cref, full
 *  and capitalised by \Cref), the labels of one kind are joined by "," and "and" and a run of three or more
 *  consecutive numbers is "a to c", and groups of different kinds are joined by "and", or "," and ", and". */
class CrossRefAtom : public Atom {
public:
  enum class Form { cref, Cref, autoref };

  CrossRefAtom(std::shared_ptr<std::map<std::string, std::string>> labels,
               std::shared_ptr<std::map<std::string, std::string>> kinds, std::vector<std::string> keys, Form form,
               bool range, std::shared_ptr<const std::map<std::string, std::string>> declared)
      : _labels(std::move(labels)), _kinds(std::move(kinds)), _keys(std::move(keys)), _form(form), _range(range),
        _declared(std::move(declared)) {}

  sptr<Box> createBox(Env& env) override {
    sptr<Atom> text = buildFragment(compose(), false);
    if (text == nullptr) text = sptrOf<EmptyAtom>();
    return text->createBox(env);
  }

private:
  struct Item {
    std::string key, kind, number;
    bool defined = false;
  };

  /** How a kind is named, as the packages declared it (`\gmdeclare`: cleveref.sty's \crefname, \Crefname and
   *  \creflabelformat, and hyperref.sty's autoref names): abbreviations for \cref, full names for \Cref, hyperref's
   *  for \autoref. A theorem's is its title (`thm:Title`). */
  struct Names {
    std::string one, many, format;
    /** The number as cleveref's label format sets it: `(#2#1#3)` is the number in parentheses (#2 and #3 are the link). */
    std::string show(const std::string& number) const {
      if (format.empty()) return number;
      std::string out;
      for (std::size_t i = 0; i < format.size(); i++) {
        if (format[i] == '#' && i + 1 < format.size() && format[i + 1] >= '1' && format[i + 1] <= '3') {
          if (format[i + 1] == '1') out += number;
          i++;
        } else {
          out += format[i];
        }
      }
      return out;
    }
  };

  std::string declared(const char* table, const std::string& kind) const {
    const auto it = _declared->find(std::string(table) + "\x1f" + kind);
    return it == _declared->end() ? std::string() : it->second;
  }

  Names namesOf(const std::string& kind) const {
    Names n;
    if (_form == Form::autoref) {
      n.one = n.many = declared("autoref", kind);  // hyperref prints "Equation 1", cleveref "equation (1)"
    } else {
      n.one = declared(_form == Form::cref ? "cref" : "Cref", kind);
      n.many = declared(_form == Form::cref ? "crefs" : "Crefs", kind);
      n.format = declared("reflabel", kind);
    }
    return n;
  }

  /** The digits at the end of a number and what stands before them: `2.3` is `2.` and 3. */
  static bool split(const std::string& number, std::string& prefix, long& last) {
    std::size_t i = number.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(number[i - 1])) != 0) i--;
    if (i == number.size() || number.size() - i > 9) return false;
    prefix = number.substr(0, i);
    last = std::stol(number.substr(i));
    return true;
  }

  static bool follows(const Item& a, const Item& b) {
    std::string pa, pb;
    long la = 0, lb = 0;
    return split(a.number, pa, la) && split(b.number, pb, lb) && pa == pb && lb == la + 1;
  }

  /** Numbers in the order they are numbered: by their parts, numerically where both are numbers. */
  static bool before(const Item& a, const Item& b) {
    std::size_t i = 0, j = 0;
    while (i < a.number.size() && j < b.number.size()) {
      if (std::isdigit(static_cast<unsigned char>(a.number[i])) != 0 &&
          std::isdigit(static_cast<unsigned char>(b.number[j])) != 0) {
        std::size_t ie = i, je = j;
        while (ie < a.number.size() && std::isdigit(static_cast<unsigned char>(a.number[ie])) != 0) ie++;
        while (je < b.number.size() && std::isdigit(static_cast<unsigned char>(b.number[je])) != 0) je++;
        const long x = std::atol(a.number.substr(i, ie - i).c_str()), y = std::atol(b.number.substr(j, je - j).c_str());
        if (x != y) return x < y;
        i = ie;
        j = je;
      } else {
        if (a.number[i] != b.number[j]) return a.number[i] < b.number[j];
        i++;
        j++;
      }
    }
    return a.number.size() < b.number.size();
  }

  static std::string join(const std::vector<std::string>& parts, const std::string& last) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); i++) {
      if (i > 0) out += i + 1 == parts.size() ? last : ", ";
      out += parts[i];
    }
    return out;
  }

  std::string compose() const {
    std::vector<Item> items;
    for (const std::string& key : _keys) {
      Item it;
      it.key = key;
      const auto n = _labels->find(key);
      const auto k = _kinds->find(key);
      it.defined = n != _labels->end();
      if (it.defined) it.number = n->second;
      if (k != _kinds->end()) it.kind = k->second;
      items.push_back(std::move(it));
    }
    // \crefrange{a}{b}: "eqs. (1) to (5)", the name of the first.
    if (_range && items.size() == 2 && items[0].defined && items[1].defined &&
        namesOf(items[0].kind).one == namesOf(items[1].kind).one) {
      const Names names = namesOf(items[0].kind);
      const auto shown = [&](const Item& i) { return names.show(i.number); };
      std::string out = names.many.empty() ? "" : names.many + "~";
      return out + shown(items[0]) + " to " + shown(items[1]);
    }
    // Groups: consecutive labels of one kind.
    std::vector<std::string> groups;
    for (std::size_t from = 0; from < items.size();) {
      std::size_t to = from + 1;
      // One group while the name is the same: cleveref calls a subsection "section" too, so they are "sections 1 and 1.1".
      while (to < items.size() && items[from].defined && items[to].defined &&
             namesOf(items[to].kind).one == namesOf(items[from].kind).one) {
        to++;
      }
      std::vector<Item> group(items.begin() + static_cast<std::ptrdiff_t>(from),
                              items.begin() + static_cast<std::ptrdiff_t>(to));
      from = to;
      if (!group[0].defined) {
        groups.push_back("\\textbf{??}");
        continue;
      }
      std::sort(group.begin(), group.end(), before);
      const Names names = namesOf(group[0].kind);
      const auto shown = [&](const Item& i) { return names.show(i.number); };
      // Three or more in a row are "a to c".
      std::vector<std::string> parts;
      for (std::size_t i = 0; i < group.size();) {
        std::size_t j = i;
        while (j + 1 < group.size() && follows(group[j], group[j + 1])) j++;
        if (j - i >= 2) {
          parts.push_back(shown(group[i]) + " to " + shown(group[j]));
        } else {
          for (std::size_t k = i; k <= j; k++) parts.push_back(shown(group[k]));
        }
        i = j + 1;
      }
      const std::string name = group.size() > 1 ? names.many : names.one;
      groups.push_back((name.empty() ? "" : name + "~") + join(parts, " and "));
    }
    return join(groups, groups.size() <= 2 ? " and " : ", and ");
  }

  std::shared_ptr<std::map<std::string, std::string>> _labels, _kinds;
  std::vector<std::string> _keys;
  Form _form;
  bool _range;
  std::shared_ptr<const std::map<std::string, std::string>> _declared;
};

/** \cite and natbib's kin: each key's \bibitem label, looked up when the atom
 *  is built into a box -- the bibliography is usually after the citation --
 *  and a bold ? where there is none. */
class CitationAtom : public Atom {
public:
  CitationAtom(std::shared_ptr<std::map<std::string, std::string>> items, std::vector<std::string> keys,
               std::string form, sptr<Atom> pre, sptr<Atom> post)
      : _items(std::move(items)),
        _keys(std::move(keys)),
        _form(std::move(form)),
        _pre(std::move(pre)),
        _post(std::move(post)) {}

  sptr<Box> createBox(Env& env) override {
    const auto text = [](const std::string& s) { return sptrOf<TextAtom>(s, false); };
    auto row = sptrOf<RowAtom>();
    // natbib: \citet names the author it does not have, \citealp has no brackets.
    if (_form == "citet") row->add(text("(author?) "));
    const bool brackets = _form != "citealp";
    if (brackets) row->add(text("["));
    if (_pre != nullptr) {
      row->add(_pre);
      row->add(text(" "));
    }
    for (std::size_t i = 0; i < _keys.size(); i++) {
      if (i > 0) row->add(text(", "));
      const auto it = _items->find(_keys[i]);
      sptr<Atom> label = it == _items->end() ? nullptr : buildFragment(it->second, false);
      row->add(label != nullptr ? label : sptrOf<FontStyleAtom>(FontStyle::bf, false, text("?")));
    }
    if (_post != nullptr) {
      row->add(text(", "));
      row->add(_post);
    }
    if (brackets) row->add(text("]"));
    return row->createBox(env);
  }

private:
  std::shared_ptr<std::map<std::string, std::string>> _items;
  std::vector<std::string> _keys;
  std::string _form;
  sptr<Atom> _pre, _post;
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
  /** The document of the whole input is being lowered, so a \footnote has a note to leave for its foot. */
  bool _pageNotes = false;
  /** The float being lowered, `figure` or `table`, and the sub-float in it, `subfigure` or `subtable` (the prelude's
   *  environments say so with \gmfloat and \gmsubfloat): what a \caption numbers, and by which counter. */
  std::string _floatKind, _subKind;
  /** A document's caption is a line of its own, which is what \abovecaptionskip is a skip above; in a label it shares one. */
  bool _captionOwnLine = false;

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
  /** What `gmrows` said for it. */
  std::string _rows;
  /** How many alignment rows or displays are being lowered: a \label in
   *  one names its number, which is known when it ends. */
  int _inRow = 0;
  /** Rows or displays whose number a abel waits for (align, gather, equation, a display): inside one, labels are kept. */
  int _inFamilyRow = 0;
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
        // TeX drops the space that follows a break: it would be glue at the start of the next line.
        std::uint32_t next = i + 1;
        while (next < n && isSpace(node(child(list, next)))) next++;
        lowerList(list, arr, next);
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
        LengthScope scope;
        auto atom = groupAtom(build(child(id, 0)), f);
        if (atom != nullptr) atom->_type = AtomType::ordinary;
        // tex.web 1186: `{...}` in math is one Ord atom whatever it holds, so `a{-b}` has no space after a and `a{\to b}` none
        // before the arrow; a row that is only the group's content would take the type of its first and last atoms.
        if (atom != nullptr && x.mode == Mode::math && dynamic_cast<CharSymbol*>(atom.get()) == nullptr) {
          atom = sptrOf<TypedAtom>(AtomType::ordinary, AtomType::ordinary, atom);
        }
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
        LengthScope scope;
        auto atom = groupAtom(environment(id), f);
        if (atom != nullptr) atom->_type = AtomType::ordinary;
        f.add(atom);
        return;
      }
      case NodeKind::math:
        // (a lone character has the italic correction TeX sets after it: `$f$` is wider than the glyph)
        f.add(sptrOf<MathAtom>(RowAtom::withItalic(x.flag ? displayBody(child(id, 0)) : build(child(id, 0))),
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
    if (name == "gmsubeqbegin") {
      beginSubequations();
      return nullptr;
    }
    if (name == "gmsubeqend") {
      nums().sub = false;
      return nullptr;
    }
    // thebibliography's \bibitem, which the prelude writes as an \item and
    // this: the key's label, or the next number when it has none (LaTeX's
    // \@lbibitem does not step the counter), which \cite draws.
    if (name == "gmbibitem") {
      std::string label = trimmed(rawOf(child(id, 1)));
      if (label.empty()) label = std::to_string(++nums().bibcount);
      (*nums().bibitems)[trimmed(rawOf(child(id, 0)))] = label;
      return nullptr;
    }
    if (name == "gmnewcounter") {
      const std::string key = trimmed(rawOf(child(id, 0)));
      if (counterIsKnown(key)) {
        _diags.warn(node(id).span, "counter `" + key + "' is already defined");
        return nullptr;
      }
      Numbering::Counter c;
      c.parent = trimmed(rawOf(child(id, 1)));
      nums().counters[key] = c;
      return nullptr;
    }
    if (name == "setcounter" || name == "addtocounter") {
      int* v = counterOf(id, trimmed(rawOf(child(id, 0))));
      int n = 0;
      if (v != nullptr && counterNumber(id, rawOf(child(id, 1)), n)) *v = name == "setcounter" ? n : *v + n;
      return nullptr;
    }
    if (name == "newlength") {
      const std::string key = lengthName(id, 0, false);
      if (key.empty()) return nullptr;
      // LaTeX's \newlength is \newskip's refusal of a name that is taken ("Command \x already defined"): the length keeps its value.
      if (nums().lengths.known(key)) {
        _diags.warn(node(id).span, "\\newlength: \\" + key + " is already defined");
      } else {
        nums().lengths.define(key);
      }
      return nullptr;
    }
    if (name == "setlength" || name == "addtolength") {
      const std::string key = lengthName(id, 0, true);
      if (key.empty()) return nullptr;
      const std::string given = trimmed(rawOf(child(id, 1)));
      std::string text = given;
      // TeX's glue: the natural length, less `plus` and `minus`.
      for (const char* word : {" plus", " minus"}) {
        const auto cut = text.find(word);
        if (cut != std::string::npos) text.erase(cut);
      }
      Dimen v = Units::getDimen(text);
      if (!v.isValid() || !isLengthText(text)) {
        _diags.warn(node(id).span, "`" + given + "' is not a length");
        return nullptr;
      }
      if (name == "addtolength") {
        Dimen now;
        nums().lengths.get(key, now);
        if (now.val != 0.f && now.unit != v.unit) {
          _diags.warn(node(id).span, "cannot add " + given + " to " + describe(now) +
                                       ": a length is one unit; \\setlength it instead");
          return nullptr;
        }
        v.val += now.val;
      }
      nums().lengths.set(key, v);
      return nullptr;
    }
    if (name == "stepcounter") {
      const std::string key = trimmed(rawOf(child(id, 0)));
      if (counterOf(id, key) != nullptr) stepCounter(key);
      return nullptr;
    }
    // \refstepcounter{c} in the prelude is \stepcounter{c}\gmcurrentlabel{\thec}.
    if (name == "gmcurrentlabel") {
      nums().current = withCounterValues(id, rawOf(child(id, 0)));
      nums().currentKind.clear();
      return nullptr;
    }
    // What a \label after it names (cleveref's kind: the counter, or the environment of a theorem).
    if (name == "gmlabelkind") {
      nums().currentKind = trimmed(rawOf(child(id, 0)));
      return nullptr;
    }
    if (name == "value") {
      const int* v = counterOf(id, trimmed(rawOf(child(id, 0))));
      return v == nullptr ? nullptr : literalText(std::to_string(*v));
    }
    // A counter's name or, for `\roman{12}`, which has always been the number
    // itself, a number; anything else (enumitem's `\roman*`) is not for this.
    if (isCounterStyle(name)) {
      const std::string given = trimmed(rawOf(child(id, 0)));
      const bool number = !given.empty() && given.find_first_not_of("0123456789") == std::string::npos;
      const bool word = !given.empty() && std::isalpha(static_cast<unsigned char>(given[0])) != 0;
      if (number) return literalText(formatCounter(name, std::atoi(given.c_str())));
      if (word) {
        const int* v = counterOf(id, given);
        return v == nullptr ? nullptr : literalText(formatCounter(name, *v));
      }
    }
    if (name == "ref" || name == "eqref") return reference(id, name == "eqref");
    // What LaTeX draws for a reference it cannot resolve: a bold ??. A grob
    // has no pages, nor a bibliography.
    const auto bold = [](const std::string& s) {
      return sptrOf<FontStyleAtom>(FontStyle::bf, false, literalText(s));
    };
    if (name == "pageref") return bold("??");
    if (isCrossReference(name)) {
      // \cref{a,b}, \autoref{a}, \crefrange{a}{b}: the kind of each label and its number, drawn once the whole input
      // is read; a key no \label names is ?? and said then.
      const bool range = name == "crefrange" || name == "Crefrange";
      std::vector<std::string> keys;
      if (range) {
        keys = {trimmed(rawOf(child(id, 0))), trimmed(rawOf(child(id, 1)))};
      } else if (name == "autoref") {
        keys = {trimmed(rawOf(child(id, 0)))};
      } else {
        std::stringstream in(rawOf(child(id, 0)));
        for (std::string k; std::getline(in, k, ',');) keys.push_back(trimmed(k));
        if (keys.empty()) keys.emplace_back();
      }
      nums().crossrefs.push_back({keys, node(id).span});
      const auto form = name == "autoref"                          ? CrossRefAtom::Form::autoref
                        : (name == "Cref" || name == "Crefrange") ? CrossRefAtom::Form::Cref
                                                                   : CrossRefAtom::Form::cref;
      return sptrOf<CrossRefAtom>(nums().labels, nums().kinds, std::move(keys), form, range,
                                  std::make_shared<const std::map<std::string, std::string>>(_ast.declared));
    }
    if (isCitation(name)) {
      // `\cite[p.~3]{a,b}`: the label of each key's \bibitem, then the note,
      // in brackets. natbib's take a note before as well,
      // `\citep[see][p.~3]{a}`, or one after alone; \citet names the author
      // it does not have, and \citealp has no brackets. A key no \bibitem
      // names is what LaTeX draws for one it cannot resolve, a bold ?
      // (CitationAtom), and said once the whole input is read.
      const std::string keys = rawOf(child(id, count(id) - 1));
      std::string pre, post = rawOf(child(id, 0));
      if (name != "cite" && node(child(id, 1)).flag) {
        pre = post;
        post = rawOf(child(id, 1));
      }
      std::vector<std::string> list;
      std::stringstream in(keys);
      for (std::string k; std::getline(in, k, ',');) list.push_back(trimmed(k));
      if (list.empty()) list.emplace_back();
      nums().cites.push_back({list, node(id).span, name});
      return sptrOf<CitationAtom>(nums().bibitems, std::move(list), name,
                                  pre.empty() ? nullptr : fragment(pre, false),
                                  post.empty() ? nullptr : fragment(post, false));
    }
    // \footnote and kin are the prelude's, over these three: the mark as the counter has it by now, and the note.
    // A document has the notes set after its body (finishNotes()); a label has no page to put them on, so a
    // note's text is set where it was written.
    if (name == "gmfootnote" || name == "gmfootnotetext" || name == "gmfootnotemark") {
      const std::string mark = trimmed(withCounterValues(id, rawOf(child(id, 0))));
      const sptr<Atom> sign = fragment("\\textsuperscript{" + mark + "}", false);
      if (name == "gmfootnotemark") return sign;
      const NodeId text = child(id, 1);
      if (_pageNotes) {
        nums().notes.emplace_back(mark, rawOf(text));
        return name == "gmfootnote" ? sign : nullptr;
      }
      _diags.warn(node(id).span, "\\footnote has no page to put a note on: its text is drawn here");
      return argumentFormula(text, rawOf(text), false);
    }
    // A caption is a line of text, as \text{} sets one; the parser put the
    // break that ends its line after it.
    if (name == "caption") {
      // The number first: a \label in the caption's own text (`\caption{Text\label{fig}}`, as it is mostly written) names
      // the float it is in, so the counter has stepped by the time that label is read.
      const std::string label = node(id).star ? std::string() : captionLabel();
      const NodeId text = child(id, count(id) - 1);
      const sptr<Atom> body = argumentFormula(text, rawOf(text), false);
      sptr<Atom> line = body;
      if (!label.empty()) {
        // "Figure 1: text", "Table 1: text", or in a sub-float "(a) text", as LaTeX and subcaption set them.
        auto row = sptrOf<RowAtom>();
        row->add(fragment(label, false));
        row->add(body);
        line = row;
      }
      if (_floatKind.empty() || !_captionOwnLine) return sptrOf<FontStyleAtom>(FontStyle::rm, false, line);
      // In a float: \abovecaptionskip (10pt, 1em of LaTeX's 10pt) above it, as \@makecaption sets one; a subcaption
      // is \small with 6pt (subcaption.sty).
      const bool sub = !_subKind.empty();
      if (sub) line = sized(line, 0.9f);
      return sptrOf<SkipAboveAtom>(sptrOf<FontStyleAtom>(FontStyle::rm, false, line), sub ? 0.6f : 1.f);
    }
    if (name == "columnbreak") return sptrOf<ColumnBreakAtom>();
    if (name == "today") {
      static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
      const std::time_t now = std::time(nullptr);
      std::tm local{};
#ifdef _WIN32
      localtime_s(&local, &now);
#else
      localtime_r(&now, &local);
#endif
      // One run of text, as \text{} sets it, not a glyph a digit.
      return literalText(std::string(months[local.tm_mon % 12]) + " " + std::to_string(local.tm_mday) + ", " +
                         std::to_string(local.tm_year + 1900));
    }
    // The cell a `cmd=` is being applied to: where its `{text}` goes (see tblrAlignmentOf()).
    if (name == "gmcell") return gTblrCell != nullptr ? gTblrCell : sptr<Atom>(sptrOf<EmptyAtom>());
    if (name == "gmfloat") {
      _floatKind = trimmed(rawOf(child(id, 0)));
      _subKind.clear();
      if (!_floatKind.empty()) nums().counters["sub" + _floatKind].value = 0;
      return nullptr;
    }
    if (name == "gmsubfloat") {
      _subKind = trimmed(rawOf(child(id, 0)));
      return nullptr;
    }
    if (name == "subref") {
      const std::string key = trimmed(rawOf(child(id, 0)));
      if (_numbering.outermost()) _refs.push_back({key, node(id).span, false});
      return sptrOf<RefAtom>(nums().sublabels, key, false);
    }
    if (name == "url" || name == "href") return link(id, f);
    if (name == "textsc") {
      // Text, as in LaTeX, in math too: upright, with its spaces.
      const sptr<Atom> caps = fragment(smallCaps(rawOf(child(id, 0))), false);
      return node(id).mode == Mode::math ? sptrOf<FontStyleAtom>(FontStyle::rm, false, caps) : caps;
    }
    // A run of the code of an alltt, as typed (the parser wrote its characters in hex).
    if (name == "gmlit") return typewriter(unhex(rawOf(child(id, 0))), false);
    if (name == "verb" || name == "lstinline" || name == "mintinline" || name == "Verb") {
      // The code is the last argument; listings', minted's and fancyvrb's options and language are not drawn.
      const NodeId text = child(id, count(id) - 1);
      return typewriter(rawOf(text), node(text).star);
    }
    if (name == "includegraphics") return image(id);
    if (name == "rowcolors") {
      // The colours as they are now (an undefined one warns here), and the first row; empty is no colour.
      const auto one = [&](std::size_t k) {
        const std::string spec = trimmed(rawOf(child(id, k)));
        return spec.empty() ? std::string("-") : std::to_string(static_cast<unsigned>(colourOf(id, spec)));
      };
      nums().lengths.setWord("rowcolors", trimmed(rawOf(child(id, 1))) + "|" + one(2) + "|" + one(3));
      return nullptr;
    }
    if (name == "gmarraystretch") {
      _stretch = static_cast<float>(std::atof(rawOf(child(id, 0)).c_str()));
      if (!(_stretch > 0.f)) _stretch = 1.f;
      return nullptr;
    }
    if (name == "gmextracolsep") return nullptr;
    if (name == "gmrows") {
      _rows = trimmed(rawOf(child(id, 0)));
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
      const color c = colourOf(id, stripComments(rawOf(child(id, 0))));
      if (isTransparent(c)) return nullptr;
      return sptrOf<CellForegroundAtom>(c);
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

  /** The index of the `close` that ends the group `open` at `from` begins,
   *  or npos. A backslash escapes the next character when `escapes`. */
  static std::size_t groupEnd(const std::string& s, std::size_t from, char open, char close,
                              bool escapes) {
    int depth = 0;
    for (std::size_t j = from; j < s.size(); j++) {
      if (escapes && s[j] == '\\') {
        j++;
      } else if (s[j] == open) {
        depth++;
      } else if (s[j] == close && --depth == 0) {
        return j;
      }
    }
    return std::string::npos;
  }

  /** The arguments that follow the control word `name`, which ends at
   *  `text[at]`, for smallCaps(): the ones the command spec reads as text are
   *  set in small capitals, the rest (a colour, a label, a file, a font name,
   *  a dimension, math) as written. Returns the index of the last character
   *  read. */
  static std::size_t smallCapsArgs(const std::string& text, std::size_t at, const std::string& name,
                                   std::string& out) {
    const CommandSpec* spec = findCommand(name);
    if (spec == nullptr || spec->shape != Shape::prefix) return at;
    std::size_t end = at;
    for (const ArgSpec& a : spec->args) {
      std::size_t p = end + 1;
      while (p < text.size() && text[p] == ' ') p++;
      if (p >= text.size()) break;
      const char open = a.optional ? '[' : '{';
      if (text[p] != open) {
        if (a.optional) continue;
        break;
      }
      const std::size_t last =
        groupEnd(text, p, open, a.optional ? ']' : '}', a.kind != ArgKind::url);
      if (last == std::string::npos) break;
      const bool words = a.kind == ArgKind::text || a.kind == ArgKind::current;
      const std::string inside = text.substr(p + 1, last - p - 1);
      out.append(text, end + 1, p - end - 1);
      out += open;
      out += words ? smallCaps(inside) : inside;
      out += text[last];
      end = last;
    }
    return end;
  }

  /** Small capitals, faked as every device has to: the lowercase letters of
   *  `text` as capitals at 0.8 of the size, the rest as it is. A command's
   *  name and what is in math are left alone, and so are the arguments of a
   *  command that are not text (smallCapsArgs()). Only ASCII letters change. */
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
        std::string name;
        do {
          if (++i < text.size()) {
            out += text[i];
            if (word) name += text[i];
          }
        } while (word && i + 1 < text.size() && std::isalpha(static_cast<unsigned char>(text[i + 1])) != 0);
        if (word) i = smallCapsArgs(text, i, name, out);
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

  /** The text of a verbatim environment: a line to a row, flush left, a baselineskip apart (a blank line is a row too, as
   *  in LaTeX: without the glue between rows they stood as close as their glyphs, and blank lines were no taller than
   *  nothing). The parser has set its tabs as spaces. An `alltt`, or a `Verbatim` with `commandchars`, has its commands
   *  read: the parser put them back as a `\gmcodeline` for each line, whose text between commands is `\gmlit`. */
  sptr<Atom> verbatimBlock(NodeId id) {
    const Node& x = node(id);
    auto lines = sptrOf<VRowAtom>();
    lines->_halign = Alignment::left;
    lines->setAddInterline(true);
    if (x.aux == 1 && count(id) > 0) {
      const NodeId body = child(id, count(id) - 1);
      for (std::uint32_t i = 0; i < count(body); i++) {
        const NodeId item = child(body, i);
        if (node(item).kind != NodeKind::command || node(item).text != "gmcodeline" || count(item) == 0) continue;
        const NodeId arg = child(item, 0);
        Formula g;
        if (count(arg) > 0) lowerList(child(arg, 0), g, 0);
        // In the typewriter face, nested as \texttt is: added to the roman the line is in.
        lines->append(sptrOf<FontStyleAtom>(FontStyle::tt, false, g._root != nullptr ? g._root : literalText(" "), true));
      }
      return lines;
    }
    std::size_t from = 0;
    while (true) {
      const std::size_t to = x.raw.find('\n', from);
      const std::string line = x.raw.substr(from, to == std::string::npos ? std::string::npos : to - from);
      lines->append(typewriter(line.empty() ? " " : line, x.text.back() == '*'));
      if (to == std::string::npos) break;
      from = to + 1;
    }
    return lines;
  }

  /** The text `hex` stands for, two digits a byte (`\gmlit`); what is not a digit is left out. */
  static std::string unhex(const std::string& hex) {
    std::string out;
    int high = -1;
    for (const char c : hex) {
      int v = -1;
      if (c >= '0' && c <= '9') v = c - '0';
      if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
      if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
      if (v < 0) continue;
      if (high < 0) {
        high = v;
      } else {
        out += static_cast<char>(high * 16 + v);
        high = -1;
      }
    }
    return out;
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

    std::function<void(const std::string&)> reporter() override {
      if (_who.empty()) return {};
      return [at = _at, who = _who](const std::string& message) { warnLater(at, who + ": " + message); };
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
    a.reportAt(node(id).span, "\\" + name);
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
        return FracAtom::fence(frac, args.size() > 0 ? args[0] : "", args.size() > 1 ? args[1] : "");
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
      out = FracAtom::fence(sptrOf<FracAtom>(num, den, false), l, r);
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
        const color c = colourOf(id, rawOf(child(id, 0)));
        return sptrOf<ColorAtom>(body(), TRANSPARENT, c);
      }
      if (name == "fontsize") {
        // The size over the 10pt of \normalsize, which a grob's own size stands for, as \large's 1.2 does.
        // A bare number is points; `em` is the size around it; the other units of the page are lengths.
        const std::string raw = trimmed(rawOf(child(id, 0)));
        auto a = body();
        if (a == nullptr) a = sptrOf<EmptyAtom>();
        char* end = nullptr;
        const float number = std::strtof(raw.c_str(), &end);
        float factor = 0.f;
        if (end != raw.c_str() && *end == '\0') {
          factor = number / 10.f;
        } else {
          const Dimen d = Units::getDimen(raw);
          if (d.isValid() && d.unit == UnitType::em) {
            factor = d.val;
          } else if (d.isValid() && pointsPer(d.unit) > 0) {
            factor = static_cast<float>(d.val * pointsPer(d.unit) / 10.0);
          }
        }
        if (!std::isfinite(factor) || factor <= 0) {
          _diags.warn(x.span, "\\fontsize: `" + raw + "' is not a positive size in em or an absolute unit; the size is kept");
          return a;
        }
        return sized(a, std::min(factor, 16384.f));
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
    /** What `\vspace` has asked for since: TeX's glue between this line and the next, added to the baselineskip glue.
     *  It lands before the next line that starts. */
    Dimen skip;
    /** What a `\vspace` in the text of a line not yet started asked for: the glue goes after that line (it is a vadjust,
     *  which TeX puts below the line holding it), so it becomes `skip` when the line starts. */
    Dimen skipAfter;
    /** Nothing placed since a line break: a space here goes with it. */
    bool afterBreak = false;
    /** Anything drawn at all: a break before that is none. */
    bool drawn = false;
    /** A paragraph starts here: its first line is indented, as in TeX. */
    bool indentNext = false;
    /** Paragraphs are indented at all: a minipage sets \parindent to 0. */
    bool indents = true;
    /** A verse: each `\\` starts a paragraph of its own. */
    bool verse = false;
    /** Nothing has been set since a heading. LaTeX leaves the paragraph
     *  that follows one unindented, whether it comes straight after the
     *  heading or after a blank line. */
    bool afterHeading = false;
    // Text of the paragraph stands on the line before: a display after it interrupts the paragraph, and TeX chooses its
    // skips by how far that line reaches. A paragraph break, a heading or another block ends it.
    bool textBefore = false;
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
    const Dimen d = Units::getDimen("\\parindent");
    if (!d.isValid() || d.val == 0.f) return nullptr;
    return sptrOf<SpaceAtom>(d.unit, d.val, 0.f, 0.f);
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
    // The counters made `within` a section (\newcounter{c}[section], a theorem's) start again with it.
    static const char* const kNames[] = {"section", "subsection", "subsubsection"};
    for (int i = level; i < 3; i++) resetCountersWithin(kNames[i]);
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
    if (!number.empty()) {
      nums().current = number;
      static const char* kinds[] = {"section", "subsection", "subsubsection", "paragraph", "subparagraph"};
      nums().currentKind = kinds[std::min(std::max(level, 0), 4)];
    }
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
    if (l.align == Alignment::left && l.indents) {
      if (const auto indent = parIndent()) l.line->add(indent);
    }
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
    Dimen gap = l.gap.empty() ? Dimen() : Units::getDimen(l.gap);
    if (l.skip.isValid()) {
      gap = gap.isValid() ? addSkips(gap, l.skip) : l.skip;
      l.skip = Dimen();
    }
    if (gap.isValid()) l.rows->addRowGap(gap);
    l.gap.clear();
    l.rows->addRow();
    // A \vspace written in this line's own text lies below it, before the next.
    if (l.skipAfter.isValid()) {
      l.skip = l.skipAfter;
      l.skipAfter = Dimen();
    }
  }

  /** The natural length of the glue `\vspace` is given. A grob's lines do not stretch, so `1cm plus 5pt minus 2pt` is 1cm;
   *  `\smallskipamount`, `\medskipamount` and `\bigskipamount` are LaTeX's 3, 6 and 12pt. What is not a length warns and
   *  is no space (it was read as a number of pixels, and `20sp` as 65536 times too much). */
  Dimen glueNatural(std::string text, NodeId id) {
    for (const char* word : {"plus", "minus"}) {
      for (std::size_t p = text.find(word); p != std::string::npos; p = text.find(word, p + 1)) {
        if (p > 0 && std::isspace(static_cast<unsigned char>(text[p - 1])) != 0) {
          text.erase(p);
          break;
        }
      }
    }
    text = trimmed(text);
    static const std::map<std::string, std::string> amounts = {
      {"\\smallskipamount", "3pt"}, {"\\medskipamount", "6pt"}, {"\\bigskipamount", "12pt"}};
    const auto named = amounts.find(text);
    if (named != amounts.end()) text = named->second;
    const Dimen d = Units::getDimen(text);
    const bool known = d.isValid() && (d.unit != UnitType::pixel || text.find("px") != std::string::npos) &&
                       (d.unit != UnitType::none || d.val == 0.f);
    if (!known) {
      _diags.warn(node(id).span, "\\vspace: `" + text + "' is not a length: no space is added");
      return Dimen();
    }
    return d;
  }

  /** Two skips added: in one unit when they have it, else in points when both are lengths of the page; two skips of
   *  different kinds (an `em` and a `pt`) cannot be added here, and the later one stands. */
  static Dimen addSkips(const Dimen& a, const Dimen& b) {
    if (a.unit == b.unit) return {a.val + b.val, a.unit};
    if (pointsPer(a.unit) > 0 && pointsPer(b.unit) > 0) {
      return {static_cast<float>(a.val * pointsPer(a.unit) + b.val * pointsPer(b.unit)), UnitType::pt};
    }
    return b;
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
      l.textBefore = true;
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
      l.textBefore = false;
      // \parskip, when the input made it more than the nothing article has.
      if (l.drawn && l.gap.empty() && !Units::getDimen("\\parskip").isEmpty()) l.gap = "\\parskip";
    } else if (l.verse && brk != kNoNode) {
      // A verse's `\\` ends its paragraph (\@centercr is \par), so the next line is the first of one: outdented.
      l.indentNext = l.indents;
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
    if (x.kind == NodeKind::environment) return isDisplay(x.text);
    return x.kind == NodeKind::group && x.aux == 3;
  }

  /** The expansion of document, table, figure or center: nothing of their
   *  own, so a label sets their content as if they were not there. */
  bool isTransparentGroup(NodeId id) const {
    const Node& x = node(id);
    return x.kind == NodeKind::group && (x.aux == 2 || (x.aux >= 4 && x.aux <= 7));
  }

  /** A list, verbatim text or tcolorbox, which a document sets apart from its text. */
  bool isList(NodeId id) const {
    const Node& x = node(id);
    return x.kind == NodeKind::environment &&
           isSetApart(x.text);
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
    _pageNotes = paragraphs && _numbering.outermost();
    feedBody(l);
    if (_pageNotes) finishNotes(l);
    _pageNotes = false;
    finishLabel(l, f);
  }

  /** The notes of the page, set after the body of a document, as LaTeX sets them at the foot of a page: \skip\footins (9pt) below the
   *  text, \footnoterule (0.4 of the line, 0.4pt), then each note in \footnotesize, its first line indented 1em and the mark
   *  in a box of 1.8em, right-aligned, before the text; the notes a \footnotesep (6.65pt) strut apart. */
  void finishNotes(Label& l) {
    if (nums().notes.empty()) return;
    lineBreak(l);
    l.gap = "0.9em";
    l.indentNext = false;
    beforeLine(l);
    l.line->add(fragment("\\rule{0.4\\linewidth}{0.4pt}", false));
    lineBreak(l);
    bool first = true;
    for (const auto& note : nums().notes) {
      l.gap = first ? "0.2em" : "0.05em";
      first = false;
      l.indentNext = false;
      beforeLine(l);
      l.line->add(fragment("{\\footnotesize \\hspace{2.8em}\\llap{\\textsuperscript{" + note.first + "}}" + note.second + "}", false));
      lineBreak(l);
    }
    nums().notes.clear();
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
    l.textBefore = false;
  }

  /** Something a document sets on a line of its own, `skip` above and
   *  below it: a display, centred, with TeX's 10pt \abovedisplayskip and
   *  \belowdisplayskip (1em at a 10pt font); or a list, with \topsep's
   *  8pt. It interrupts a paragraph without ending it, so what follows
   *  goes on unindented, as in TeX. */
  void blockLine(Label& l, NodeId id, bool display, const char* skip, const char* above = nullptr) {
    lineBreak(l);
    // The text before it, if any, is ended by now.
    const bool interrupts = l.textBefore;
    // A display is set off by TeX's display skips, which VRowAtom chooses once it knows the width of the line before.
    if (l.drawn && !display) l.gap = above != nullptr ? above : skip;
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
    // An equation or align is in display style, as a display is.
    if (display && node(id).kind != NodeKind::math) atom = sptrOf<StyleAtom>(TexStyle::display, atom);
    atom = wrap(l, atom);
    if (display) {
      auto shown = sptrOf<DisplayAtom>(atom);
      // amsmath's alignments are `\halign to \displaywidth`: they start at the left margin, so the long skips apply.
      static const std::set<std::string> wide = {"align", "alignat", "flalign", "gather", "multline", "eqnarray"};
      std::string name = node(id).kind == NodeKind::environment ? node(id).text : std::string();
      if (!name.empty() && name.back() == '*') name.pop_back();
      shown->asEquation(interrupts, wide.count(name) > 0);
      l.line->add(shown);
    } else {
      l.line->add(atom);
    }
    lineBreak(l);
    if (!display) l.gap = skip;
    l.indentNext = false;
    l.textBefore = false;
  }

  /** The content of a group a document sets apart from its paragraphs,
   *  `skip` above and below: a float (table, figure) where it is written,
   *  as LaTeX's [h] placement sets one, with \intextsep's 12pt; or center,
   *  flushleft or flushright, their lines aligned so, with \topsep's 8pt.
   *  A \centering in any of them lasts to its end. */
  void blockLines(Label& l, NodeId id, Alignment align, const char* skip) {
    LengthScope scope;  // an environment is a group
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
    l.textBefore = false;
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
        found = isList(id) || isDisplay(x.text);
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
    // A group ends what a length was set to in it; a declaration is not one.
    std::unique_ptr<LengthScope> scope;
    if (!declaration) scope = std::make_unique<LengthScope>();
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
        LengthScope scope;
        const Alignment was = l.align;
        feedLines(l, child(id, 0));
        l.align = was;
      } else if (l.document && (x.kind == NodeKind::declaration ||
                                (x.kind == NodeKind::group && x.aux == 0)) && hasBlock(id)) {
        blockGroup(l, id);
      } else if (l.document && isDisplay(id)) {
        blockLine(l, id, true, "1em");
      } else if (l.document && isList(id)) {
        // A list is set off by \topsep's 8pt, a tcolorbox by 0.5\baselineskip, multicols by \multicolsep (12pt), which
        // with a header is below it only: pdflatex puts none between the text before and the header.
        if (x.text == "multicols") {
          blockLine(l, id, false, "1.2em", child(id, 1) != kNoNode && !trimmed(rawOf(child(id, 1))).empty() ? "0em" : nullptr);
        } else {
          // A quote is set off by \topsep and, after a blank line, \partopsep: 10pt (pdflatex: 22 between the baselines
          // either side of it against 12 in a paragraph).
          const bool quoted = x.text == "quote" || x.text == "quotation" || x.text == "verse";
          blockLine(l, id, false, x.text == "tcolorbox" || x.text == "mdframed" ? "0.6em" : quoted ? "1em" : "0.8em");
        }
      } else if (l.document && x.kind == NodeKind::command && !x.flag && x.text == "caption") {
        // On a line of its own, unindented; the parser ended its line. In a float it is centred, as \@makecaption
        // centres one that fits, whether or not \centering came before it.
        lineBreak(l);
        l.indentNext = false;
        const Alignment was = l.align;
        if (!_floatKind.empty()) l.align = Alignment::center;
        beforeProse(l);
        _captionOwnLine = true;
        lowerItem(id, prose(l));
        _captionOwnLine = false;
        endProse(l);  // the line takes its alignment as its text ends
        l.align = was;
      } else if (l.document && x.kind == NodeKind::command && !x.flag && x.text == "paragraph") {
        // \paragraph starts a paragraph of its own, flush left with LaTeX's
        // 3.25ex above it, and runs into it.
        lineBreak(l);
        if (l.drawn) l.gap = "1.625em";
        l.indentNext = false;
        beforeProse(l);
        lowerItem(id, prose(l));
      } else if (l.document && l.drawn && x.kind == NodeKind::command && !x.flag && x.text == "vspace") {
        // Glue between this line and the next, as TeX adds it: on top of the baselineskip glue, not instead of it.
        // Before anything is drawn (the top of a page, which TeX would drop) it stays what it was, a height.
        const Dimen d = glueNatural(rawOf(child(id, 0)), id);
        if (d.isValid()) {
          // In the text of a line that has not started (words of the paragraph are waiting), the glue is below that line.
          const bool inText = l.broken && l.prose != nullptr && l.prose->_root != nullptr;
          Dimen& into = inText ? l.skipAfter : l.skip;
          into = into.isValid() ? addSkips(into, d) : d;
        }
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
      } else if (x.kind == NodeKind::command && !x.flag && (x.text == "gmfloat" || x.text == "gmsubfloat")) {
        // What a float is: state of the lowering, not text, so the spaces round it stay as they were.
        Formula state;
        lowerItem(id, state);
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
          l.line->add(sptrOf<StyleAtom>(TexStyle::display, g == nullptr ? sptrOf<EmptyAtom>() : RowAtom::withItalic(g)));
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
            l.line->add(wrap(l, sptrOf<TypedAtom>(AtomType::ordinary, AtomType::ordinary, RowAtom::withItalic(g->_root))));
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
        const bool atLineStart = l.afterBreak;
        beforeProse(l);
        lowerItem(id, prose(l));
        // `\label`, `\setlength`, `\definecolor` and the rest that draw nothing leave the line as it was: a space after
        // one at the start of a paragraph is still the space after a break, which TeX ignores (it was drawn, and the
        // text after `\section{S}\n\label{x}\n` stood a space in).
        if (atLineStart && l.prose != nullptr && l.prose->_root == nullptr) l.afterBreak = true;
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
  sptr<Atom> subDocument(NodeId list, bool indents = false, bool verse = false) {
    Formula f;
    Label l(f);
    l.document = true;
    l.indents = indents;
    l.indentNext = indents;
    l.verse = verse;
    l.afterBreak = true;
    // A verse's paragraphs start 1.5em out of its margin (\itemindent = \listparindent = -1.5em).
    LengthScope scope;
    if (verse) nums().lengths.set("parindent", Dimen{-1.5f, UnitType::em});
    feedLines(l, list);
    finishLabel(l, f);
    return f._root;
  }

  /** \begin{quote}, quotation and verse: article's lists with no label, whose margins are \leftmargin (2.5em at the first
   *  level) on both sides. A quotation indents the first line of each paragraph by \listparindent, 1.5em; a verse has a
   *  left margin 1.5em deeper and its paragraphs -- each `\\` line, a blank line's stanza -- start 1.5em out of it, so a
   *  line that wraps is set in against the first (pdflatex: 96bp, 111bp from a margin at 72). */
  sptr<Atom> quotation(NodeId id) {
    const bool verse = node(id).text == "verse";
    sptr<Atom> body = subDocument(child(id, count(id) - 1), node(id).text == "quotation" || verse, verse);
    if (body == nullptr) return nullptr;
    return sptrOf<IndentAtom>(body, verse ? 4.f : 2.5f, 2.5f);
  }

  /** \begin{minipage}[position][height][inner position]{width}: its
   *  body set to its width, and to its height when that is taller. The
   *  inner position is the position unless given, as in LaTeX. */
  sptr<Atom> minipage(NodeId id) {
    // The first letter of an optional argument, or `fallback`.
    const auto letter = [&](NodeId arg, const char* allowed, char fallback) {
      const std::string s = rawOf(arg);
      const auto at = s.find_first_not_of(" \t\r\n");
      if (at == std::string::npos || s[at] == '\0' || std::strchr(allowed, s[at]) == nullptr) {
        return fallback;
      }
      return s[at];
    };
    const char p = letter(child(id, 0), "tb", 'c');
    const char inner = letter(child(id, 2), "tbcs", p);
    const Dimen height = Units::getDimen(rawOf(child(id, 1)));
    const Dimen width = Units::getDimen(rawOf(child(id, 3)));
    return sptrOf<MinipageAtom>(subDocument(child(id, count(id) - 1)), width, p, height, inner);
  }

  /** A tcolorbox length in em of the text around, as 0.2845em is a millimetre at LaTeX's 10pt; false when `value` is
   *  not a length. */
  bool tcolorboxLength(const std::string& value, float& em) {
    const Dimen d = Units::getDimen(value);
    if (!d.isValid()) return false;
    switch (d.unit) {
      case UnitType::em: em = d.val; return true;
      case UnitType::mm: em = d.val * 0.284528f; return true;
      case UnitType::cm: em = d.val * 2.84528f; return true;
      case UnitType::pt: em = d.val * 0.1f; return true;
      case UnitType::point: em = d.val * 0.100375f; return true;
      case UnitType::in: em = d.val * 7.227f; return true;
      case UnitType::pica: em = d.val * 1.2f; return true;
      default: return false;
    }
  }

  /** \begin{tcolorbox}[keys]: tcolorbox's default box -- a frame of `boxrule`, rounded by `arc`, `colback` inside,
   *  padded by `boxsep` and `left`, `right`, `top`, `bottom` -- with the keys that change how it is drawn
   *  (`title`, the colours, the lengths); another key warns and is ignored. */
  sptr<Atom> tcolorbox(NodeId id) {
    // mdframed is the same box with its own keys and defaults (mdframed.sty: a 0.4pt black line, white inside, 10pt at the
    // sides and 0.4 baselineskip above and below the text, square corners, a title that is not on a band of its own).
    const bool md = node(id).text == "mdframed";
    const std::string name = md ? "mdframed" : "tcolorbox";
    TcolorboxStyle style;
    bool titleBack = false;
    if (md) {
      style.back = 0xFFFFFFFF;
      style.frame = 0xFF000000;
      style.titleText = 0xFF000000;
      style.rule = 0.04f;
      style.arc = 0.f;
      style.boxsep = 0.f;
      style.left = style.right = 1.f;
      style.top = style.bottom = 0.48f;
      style.sharp = true;
    }
    std::string title, fonttitle;
    std::vector<std::string> keys{""};
    int depth = 0;
    for (const char c : rawOf(child(id, 0))) {
      if (c == '{') depth++;
      if (c == '}') depth--;
      if (c == ',' && depth == 0) {
        keys.emplace_back();
      } else {
        keys.back() += c;
      }
    }
    for (const std::string& item : keys) {
      const std::size_t eq = item.find('=');
      std::string key = trimmed(item.substr(0, eq));
      std::string value = eq == std::string::npos ? std::string() : trimmed(item.substr(eq + 1));
      if (value.size() >= 2 && value.front() == '{' && value.back() == '}') value = value.substr(1, value.size() - 2);
      if (key.empty()) continue;
      if (md) {
        // mdframed's keys, as the tcolorbox keys that draw the same thing.
        static const std::map<std::string, std::string> names = {
          {"linecolor", "colframe"},       {"linewidth", "boxrule"},         {"backgroundcolor", "colback"},
          {"roundcorner", "arc"},          {"innerleftmargin", "left"},      {"innerrightmargin", "right"},
          {"innertopmargin", "top"},       {"innerbottommargin", "bottom"},  {"frametitle", "title"},
          {"frametitlefont", "fonttitle"}, {"frametitlefontcolor", "coltitle"},
          {"frametitlebackgroundcolor", "colbacktitle"}};
        const auto named = names.find(key);
        if (named != names.end()) key = named->second;
        if (key == "hidealllines") {
          if (value == "true" || value.empty()) style.rule = 0.f;
          continue;
        }
        if (key == "colbacktitle") titleBack = true;
        if (key == "arc") style.sharp = false;
      }
      const auto colour = [&](color& into) {
        const color c = colourOf(id, value);
        if (!isTransparent(c)) into = c;
      };
      const auto length = [&](float& into) {
        float em = 0.f;
        if (!tcolorboxLength(value, em) || em < 0.f) {
          _diags.warn(node(id).span, name + ": `" + key + "=" + value + "' is not a length of zero or more: ignored");
        } else {
          into = em;
        }
      };
      if (key == "title") title = value;
      else if (key == "fonttitle") fonttitle = value;
      else if (key == "colback") colour(style.back);
      else if (key == "colframe") colour(style.frame);
      else if (key == "colbacktitle") colour(style.backTitle);
      else if (key == "coltitle") colour(style.titleText);
      else if (key == "boxrule") length(style.rule);
      else if (key == "arc") length(style.arc);
      else if (key == "boxsep") length(style.boxsep);
      else if (key == "left") length(style.left);
      else if (key == "right") length(style.right);
      else if (key == "top") length(style.top);
      else if (key == "bottom") length(style.bottom);
      else if (key == "sharp corners") style.sharp = true;
      else _diags.warn(node(id).span, name + ": the key `" + key + "' is not drawn: ignored");
    }
    // mdframed's title stands on the box's own colour unless it has one.
    if (md && !titleBack) style.backTitle = style.back;
    if (md && style.arc <= 0.f) style.sharp = true;
    if (md && !title.empty()) {
      // A title has 5pt of its own above and below (frametitleaboveskip, frametitlebelowskip): tcolorbox's band is its
      // `boxsep` above and below, which the margins give up so that the text stands where they say.
      const float sep = 0.5f;
      style.boxsep = sep;
      style.left = std::max(0.f, style.left - sep);
      style.right = std::max(0.f, style.right - sep);
      style.top = std::max(0.f, style.top - sep);
      style.bottom = std::max(0.f, style.bottom - sep);
    }
    sptr<Atom> head;
    if (!title.empty()) {
      head = fragment(fonttitle.empty() ? title : "{" + fonttitle + " " + title + "}", false);
      head = sptrOf<ColorAtom>(head, TRANSPARENT, style.titleText);
    }
    sptr<Atom> body = subDocument(child(id, count(id) - 1));
    if (body == nullptr) body = sptrOf<EmptyAtom>();
    return sptrOf<TcolorboxAtom>(body, head, style);
  }

  /** \begin{multicols}{n}[header]: the body in `n` balanced columns, a column of `\columnsep` between. */
  sptr<Atom> multicols(NodeId id) {
    const std::string columns = trimmed(rawOf(child(id, 0)));
    int n = 0;
    try {
      n = std::stoi(columns);
    } catch (const std::exception&) {
    }
    if (n < 1) {
      _diags.warn(node(id).span, "multicols: `" + columns + "' is not a number of columns: one is used");
      n = 1;
    }
    const std::string head = trimmed(rawOf(child(id, 1)));
    Dimen sep = Units::getDimen("\\columnsep");
    if (!sep.isValid()) sep = {1.f, UnitType::em};
    sptr<Atom> body = subDocument(child(id, count(id) - 1));
    if (body == nullptr) body = sptrOf<EmptyAtom>();
    return sptrOf<MulticolsAtom>(body, head.empty() ? nullptr : fragment(head, false), n, sep);
  }

  /** Built by the engine's environment handler from its source text, as
   *  the old parser's `\name@@env{...}{body}` did. */
  sptr<Atom> environment(NodeId id) {
    const Node& x = node(id);
    if (x.text == "minipage") return minipage(id);
    if (x.text == "multicols") return multicols(id);
    if (x.text == "tcolorbox" || x.text == "mdframed") return tcolorbox(id);
    if (x.text == "quote" || x.text == "quotation" || x.text == "verse") return quotation(id);
    // The comment package's: what is between its markers is not read.
    if (const EnvSpec* dropped = findEnvironment(x.text); dropped != nullptr && dropped->dropped) return nullptr;
    if (isVerbatimEnvironment(x.text)) return verbatimBlock(id);
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
    // siunitx's S columns are set as right-aligned ones, their numbers padded
    // to line up (alignmentOf()).
    if (isTextTable(x.text) && k > 1 && k <= args.size()) args[k - 1] = plainColumns(args[k - 1], nullptr);
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
    arr->_extraRow = Units::getDimen("\\extrarowheight");
    arr->_rows = std::move(_rows);
    _rows.clear();
    if (env == kNoNode || node(env).kind != NodeKind::environment) return arr;
    if (const EnvSpec* built = findEnvironment(node(env).text); built != nullptr && built->tblr) {
      return tblrAlignmentOf(env);
    }
    // tabular[t], array[b]: LaTeX's vertical position, the first optional argument.
    if ((node(env).text == "tabular" || node(env).text == "array") && count(env) > 0) {
      const std::string where = trimmed(rawOf(child(env, 0)));
      if (!where.empty() && (where[0] == 't' || where[0] == 'b')) arr->_vpos = where[0];
    }
    const bool text = node(env).flag;
    // The rows of align, gather, ... are numbered, unless the environment is
    // starred; a multline has one number, on its last line.
    const bool family = _ast.environmentIs(node(env).text, EnvironmentKind::numbered);
    const bool automatic = family && !node(env).star;
    const bool once = family && node(env).text == "multline";
    // The cases package's: a case and its explanation, which is text; the
    // sub form numbers its rows 3a, 3b, ... as subequations does.
    const bool cases = node(env).text == "numcases" || node(env).text == "subnumcases";
    const bool lettered = node(env).text == "subnumcases" && !nums().sub;
    if (lettered) beginSubequations();
    // What the row of an enclosing display was told is set aside: this
    // alignment's rows are told their own.
    RowNote outer = std::move(_note);
    _note = RowNote();
    _inRow++;
    if (family) _inFamilyRow++;
    int lastRow = -1;
    std::vector<NodeId> rows;
    for (std::uint32_t r = 0; r < count(env); r++) {
      if (node(child(env, r)).kind == NodeKind::row) rows.push_back(child(env, r));
    }
    // A longtable's head and foot rows are set once: first and last.
    const bool longtable = node(env).text == "longtable";
    if (longtable) rows = longtableOrder(rows);
    // siunitx's S columns: a number's digits after the marker are padded, with
    // phantoms, to the most any cell of the column has, so that the markers
    // line up in a column set flush right.
    std::vector<int> sColumns;
    std::map<int, int> sDecimals;
    std::map<int, char> sMark;
    std::map<int, std::pair<std::string, std::string>> dColumns;
    if (text && isTextTable(node(env).text)) {
      for (std::uint32_t a = 0; a < count(env); a++) {
        const Node& arg = node(child(env, a));
        if (arg.kind == NodeKind::argument && arg.flag && !arg.raw.empty()) {
          sColumns.clear();
          plainColumns(arg.raw, &sColumns);
          const auto found = decimalColumns(arg.raw);
          if (!found.empty()) dColumns = found;
        }
      }
      for (const NodeId row : rows) {
        for (std::uint32_t c = 0; c < count(row); c++) {
          if (std::find(sColumns.begin(), sColumns.end(), static_cast<int>(c)) == sColumns.end()) continue;
          const SNumber n = sNumber(cellText(child(row, c)));
          if (!n.numeric) continue;
          sDecimals[static_cast<int>(c)] = std::max(sDecimals[static_cast<int>(c)], n.decimals);
          if (n.marker) sMark[static_cast<int>(c)] = n.mark;
        }
      }
    }
    // xcolor's \rowcolors: the rows from the first it names, the odd and the even in their colours.
    bool coloring = false;
    int firstColoured = 1, rowNumber = 0;
    color oddColour = TRANSPARENT, evenColour = TRANSPARENT;
    if (text && isTextTable(node(env).text)) {
      std::string word;
      if (nums().lengths.getWord("rowcolors", word)) {
        const std::size_t a = word.find('|'), b = word.find('|', a + 1);
        if (a != std::string::npos && b != std::string::npos) {
          const auto read = [](const std::string& s) {
            return s == "-" ? TRANSPARENT : static_cast<color>(std::strtoul(s.c_str(), nullptr, 10));
          };
          firstColoured = std::max(1, std::atoi(word.substr(0, a).c_str()));
          oddColour = read(word.substr(a + 1, b - a - 1));
          evenColour = read(word.substr(b + 1));
          coloring = true;
        }
      }
    }
    for (std::size_t k = 0; k < rows.size(); k++) {
      const NodeId row = rows[k];
      if (longtable && k == 0 && captionRow(env, row, *arr)) {
        arr->addRow();
        continue;
      }
      if (coloring && hasContent(row)) {
        rowNumber++;
        const color shade = rowNumber % 2 == 1 ? oddColour : evenColour;
        if (rowNumber >= firstColoured && !isTransparent(shade)) arr->addRowSpecifier(sptrOf<CellColorAtom>(shade));
      }
      for (std::uint32_t c = 0; c < count(row); c++) {
        if (c > 0) arr->addCol();
        // An S column's number that is not just digits (1.2e3, 1.5 +- 0.1) is set as siunitx sets it, in math.
        std::string sSet;
        if (text && std::find(sColumns.begin(), sColumns.end(), static_cast<int>(c)) != sColumns.end()) {
          const std::string raw = trimmed(cellText(child(row, c)));
          if (!raw.empty() && !sNumber(raw).numeric && raw.find_first_of("\\{}$") == std::string::npos) {
            std::string problem;
            const std::string set = siunitx::number(raw, problem);
            if (problem.empty()) sSet = set;
          }
        }
        const auto decimal = dColumns.find(static_cast<int>(c));
        bool acts = false;
        for (std::uint32_t k = 0; k < count(child(child(row, c), 0)); k++) {
          acts = acts || actsOnAlignment(node(child(child(child(row, c), 0), k)));
        }
        if (decimal != dColumns.end() && node(child(row, c)).mode == Mode::math && !acts) {
          arr->add(decimalCell(child(child(row, c), 0), decimal->second));
        } else if (!sSet.empty()) {
          arr->add(fragment("$" + sSet + "$", false));
        } else if (text || (cases && c > 0)) {
          textCell(child(child(row, c), 0), *arr);
          const auto padded = sDecimals.find(static_cast<int>(c));
          if (padded != sDecimals.end() && padded->second > 0) {
            const SNumber n = sNumber(cellText(child(row, c)));
            std::string pad;
            if (n.numeric && !n.marker) pad += "\\phantom{" + std::string(1, sMark.count(static_cast<int>(c)) > 0 ? sMark[static_cast<int>(c)] : '.') + "}";
            for (int d = n.decimals; n.numeric && d < padded->second; d++) pad += "\\phantom{0}";
            if (!pad.empty()) arr->add(fragment(pad, false));
          }
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
    if (lettered) nums().sub = false;
    _inRow--;
    if (family) _inFamilyRow--;
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

  /** Whether a row holds nothing but spaces and the command `name` that ends it. */
  bool holdsOnly(NodeId row, const std::string& name) const {
    for (std::uint32_t c = 0; c < count(row); c++) {
      const NodeId list = child(child(row, c), 0);
      for (std::uint32_t i = 0; i < count(list); i++) {
        const Node& x = node(child(list, i));
        if (!isSpace(x) && !(x.kind == NodeKind::command && x.text == name)) return false;
      }
    }
    return true;
  }

  /** A longtable's rows, in the order a single page sets them: the first
   *  head (the head, where there is no first one), the rows, and the last
   *  foot (the foot). A group ends at the row that has its marker. */
  std::vector<NodeId> longtableOrder(const std::vector<NodeId>& rows) const {
    std::vector<NodeId> firstHead, head, foot, lastFoot, group;
    for (const NodeId row : rows) {
      const std::string& end = node(row).text;
      // `H1 & H2 \\ \endhead`: the `\\` ended the last row of the head, so the row that only carries `\endhead` is no
      // row at all (it was a blank row of its own, a baselineskip tall).
      if (!(isLongtableMarker(end) && holdsOnly(row, end))) group.push_back(row);
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

  /** The characters of a table cell, less the spaces at its ends; anything
   *  else in it (a command, a group, a space inside) is a backslash, so that
   *  what reads the text as a number does not take it for one. */
  std::string cellText(NodeId cell) const {
    const NodeId list = child(cell, 0);
    std::uint32_t from = 0, to = count(list);
    while (from < to && isSpace(node(child(list, from)))) from++;
    while (to > from && isSpace(node(child(list, to - 1)))) to--;
    std::string out;
    for (std::uint32_t i = from; i < to; i++) {
      const Node& x = node(child(list, i));
      if (x.kind == NodeKind::character && x.cp < 128) {
        out += static_cast<char>(x.cp);
      } else {
        out += '\\';
      }
    }
    return out;
  }

  /** An environment LaTeX sets as a display (equation, align, gather, ...): a line of its own. */
  bool isDisplay(const std::string& name) const { return _ast.environmentIs(name, EnvironmentKind::display); }

  /** A column specification with siunitx's `S` and `S[options]` written as `r`,
   *  and, in `sColumns`, the indices of the columns that were S. The letters
   *  of what is in braces or brackets are not columns. A repeated group
   *  (`*{3}{S}`) is left as it is. */
  static std::string plainColumns(const std::string& spec, std::vector<int>* sColumns) {
    if ((spec.find('S') == std::string::npos && spec.find('D') == std::string::npos) ||
        spec.find('*') != std::string::npos) {
      return spec;
    }
    std::string out;
    int column = 0;
    // The index just past the group opened at `i` by `open`, which closes with `close`.
    const auto past = [&](std::size_t i, char open, char close) {
      int depth = 0;
      for (; i < spec.size(); i++) {
        if (spec[i] == open) depth++;
        if (spec[i] == close && --depth == 0) return i + 1;
      }
      return spec.size();
    };
    for (std::size_t i = 0; i < spec.size();) {
      const char c = spec[i];
      std::size_t end = i + 1;
      if (c == 'S') {
        if (sColumns != nullptr) sColumns->push_back(column);
        column++;
        out += 'r';
        if (end < spec.size() && spec[end] == '[') end = past(end, '[', ']');
        i = end;
        continue;
      }
      if (c == 'D') {
        // dcolumn's D{in}{out}{places}: its cells are aligned on the separator by the matrix (DecimalAtom)
        column++;
        out += 'r';
        for (int g = 0; g < 3 && end < spec.size() && spec[end] == '{'; g++) end = past(end, '{', '}');
        i = end;
        continue;
      }
      if (c == 'p' || c == 'm' || c == 'b') {
        column++;
        if (end < spec.size() && spec[end] == '{') end = past(end, '{', '}');
      } else if (c == '@' || c == '!' || c == '>' || c == '<') {
        if (end < spec.size() && spec[end] == '{') end = past(end, '{', '}');
      } else if (c == 'l' || c == 'c' || c == 'r' || c == 'X' || c == 'Q') {
        column++;
        if (end < spec.size() && spec[end] == '[') end = past(end, '[', ']');
      }
      out.append(spec, i, end - i);
      i = end;
    }
    return out;
  }

  /** A cell of an S column as siunitx reads a number: a sign, digits, and a
   *  decimal marker with digits after it. */
  struct SNumber {
    bool numeric = false;
    bool marker = false;
    char mark = '.';
    int decimals = 0;
  };

  static SNumber sNumber(const std::string& raw) {
    const std::string s = trimmed(raw);
    SNumber n;
    std::size_t i = 0, digits = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) i++, digits++;
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
      n.marker = true;
      n.mark = s[i++];
      while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) i++, n.decimals++;
      digits += static_cast<std::size_t>(n.decimals);
    }
    n.numeric = digits > 0 && i == s.size();
    return n;
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
        const std::size_t last = close(first + 1);
        // *{n}{cols} with a group missing: nothing more to count.
        if (first >= spec.size() || last >= spec.size()) break;
        const int times = std::atoi(spec.substr(i + 2, first - i - 2).c_str());
        const long long more = static_cast<long long>(std::min(std::max(times, 0), 1000)) *
                               columnCount(spec.substr(first + 2, last - first - 2), depth + 1);
        n = static_cast<int>(std::min<long long>(n + more, 1 << 14));
        i = last;
      } else if (c == 'D') {
        n++;
        for (int g = 0; g < 3 && i + 1 < spec.size() && spec[i + 1] == '{'; g++) i = close(i + 1);
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
    // A longtable's caption is a table's: "Table N: text", by the table counter.
    const std::string kind = _floatKind;
    if (_floatKind.empty()) _floatKind = "table";
    arr.add(sptrOf<MulticolumnAtom>(columns, "c", commandAtom(cap, arr)));
    _floatKind = kind;
    if (columns > 1) arr.addCol(columns);
    return true;
  }

  /** A tblr (tabularray's table, tinytable's) as an alignment: the rules
   *  its spec puts between rows, and the spans, colours, fonts and
   *  alignments it sets for cells. */
  sptr<ArrayFormula> tblrAlignmentOf(NodeId env) {
    auto arr = sptrOf<ArrayFormula>();
    const bool text = node(env).flag;
    const NodeId outerArg = count(env) > 0 ? child(env, 0) : kNoNode;
    const NodeId innerArg = count(env) > 1 ? child(env, 1) : kNoNode;
    // tinytable writes a `%` comment after each bracket, which would be part of the first key.
    TblrSpec spec = parseTblr(outerArg == kNoNode ? std::string() : stripComments(rawOf(outerArg)),
                              innerArg == kNoNode ? std::string() : stripComments(rawOf(innerArg)));
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

    std::set<std::string> badCommands;  // what is said of a spec's text, once each
    // LaTeX the spec holds (a caption, a note, `preto`) is read where tabularray reads it, with what the document had
    // defined at the `\begin` (`scope`), as TeX has it in force then. What is wrong with it is said at the table, not
    // left drawn as a red name.
    const auto scope = _ast.scopeOf(env);
    const auto specText = [&](const std::string& latex, const std::string& what, bool math) {
      Diagnostics problems;
      const sptr<Atom> atom = buildFragment(latex, math, problems, scope);
      for (const Diagnostic& d : problems.items()) {
        if (badCommands.insert(what + d.message).second) {
          _diags.warn(node(env).span, "tabularray: " + what + ": " + d.message);
        }
      }
      return atom;
    };

    // A caption, as a longtable's: a line across the table, above it.
    const int captionRows = !spec.caption.empty() && columns > 0 ? 1 : 0;
    if (captionRows > 0) {
      // "Table 1: text", the counter stepped, in a float or not (tabularray's caption-tag).
      const std::string kind = _floatKind;
      if (_floatKind.empty()) _floatKind = "table";
      std::string label = captionLabel();
      _floatKind = kind;
      // tabularray's caption-tag and caption-sep: `\tablename\hspace{0.25em}\thetable` and `:\enskip`. A caption
      // too long for the table hangs its lines by the width of that (its `\hangindent`).
      if (const auto tilde = label.find('~'); tilde != std::string::npos) label.replace(tilde, 1, "\\hspace{0.25em}");
      if (label.size() >= 2 && label.compare(label.size() - 2, 2, ": ") == 0) label.replace(label.size() - 2, 2, ":\\enskip{}");
      const sptr<Atom> lead = label.empty() ? sptr<Atom>(sptrOf<EmptyAtom>()) : specText(label, "caption", false);
      const sptr<Atom> text = specText(spec.caption, "caption", false);
      auto whole = sptrOf<RowAtom>();
      whole->add(lead);
      if (text != nullptr) whole->add(text);
      auto caption = sptrOf<FontStyleAtom>(FontStyle::rm, false,
                                           sptrOf<HangingAtom>(whole, lead, text == nullptr ? sptr<Atom>(sptrOf<EmptyAtom>()) : text));
      auto span = sptrOf<MulticolumnAtom>(columns, "c", caption);
      span->setWrapToTable(true);
      span->report = [at = node(env).span](const std::string& m) { warnLater(at, "tabularray: " + m); };
      arr->add(span);
      if (columns > 1) arr->addCol(columns);
      // tabularray's `headsep`: 6pt between the caption and the table.
      arr->addRowSep(Units::getDimen("0pt"), spec.headsep.isValid() ? spec.headsep : Units::getDimen("6pt"));
      arr->addRow();
    }

    bool noted = false;
    // A colour is applied to every cell it covers: said once per name.
    std::set<std::string> badColours;
    const auto tblrColour = [&](const std::string& spec) {
      color c = TRANSPARENT;
      if (ColorAtom::parseColor(spec, c)) return c;
      if (badColours.insert(spec).second) {
        _diags.warn(node(env).span, "undefined colour `" + trimmed(spec) + "': the colour is not changed");
      }
      return static_cast<color>(TRANSPARENT);
    };

    std::vector<std::pair<sptr<HlineAtom>, std::string>> ruleColours;
    // The cell just lowered (`arr->_root`), set under LaTeX that has `\gmcell` where the cell goes, read with the
    // definitions the document had at the `\begin` (`scope`).
    const auto applyToCell = [&](const std::string& latex, const std::string& what) {
      if (arr->_root == nullptr) return;
      gTblrCell = arr->_root;
      Diagnostics problems;
      const sptr<Atom> applied = buildFragment(latex, !text, problems, scope);
      gTblrCell.reset();
      if (applied != nullptr) arr->_root = applied;
      for (const Diagnostic& d : problems.items()) {
        if (badCommands.insert(what + d.message).second) {
          _diags.warn(node(env).span, "tabularray: " + what + ": " + d.message);
        }
      }
    };

    // The rule above row `at`: its pieces share one row. Pieces of another thickness or colour are a row
    // of their own, which stands a hair lower (a table that mixes them in one rule is rare).
    const auto rule = [&](int at) {
      const std::vector<TblrRuleSegment> pieces = tblrRuleSegments(spec, at, n, columns);
      std::vector<bool> drawn(pieces.size(), false);
      for (std::size_t k = 0; k < pieces.size(); k++) {
        if (drawn[k]) continue;
        auto line = sptrOf<HlineAtom>();
        const TblrRuleSegment& first = pieces[k];
        if (first.thickness.isValid()) line->setThickness(first.thickness.val, first.thickness.unit);
        // tinytable defines its colours in the body's first cell: they are looked up once it is lowered.
        if (!first.colour.empty()) ruleColours.emplace_back(line, first.colour);
        line->setDashed(first.dashed);
        const bool whole = pieces.size() == 1 && first.from <= 1 && first.to >= columns &&
                           first.leftpos >= 0 && first.rightpos >= 0;
        for (std::size_t m = k; m < pieces.size(); m++) {
          const TblrRuleSegment& other = pieces[m];
          if (other.thickness.isValid() != first.thickness.isValid() || other.colour != first.colour ||
              other.dashed != first.dashed ||
              (first.thickness.isValid() &&
               (other.thickness.val != first.thickness.val || other.thickness.unit != first.thickness.unit))) {
            continue;
          }
          drawn[m] = true;
          if (!whole) line->addPiece(other.from - 1, other.to - 1, other.leftpos, other.rightpos);
        }
        arr->add(line);
        arr->addRow();
      }
    };

    // siunitx's columns (`si={...}`): their numbers are padded after the marker to the most any cell has,
    // so that the markers line up in a column set flush right.
    std::map<int, int> siDecimals;
    std::map<int, char> siMark;
    std::map<int, siunitx::Options> siOptions;
    for (int c = 0; c < columns; c++) {
      const TblrSetting column = tblrCell(spec, 1, c + 1, n, columns);
      if (!column.siunitx) continue;
      siDecimals[c] = 0;
      siOptions[c] = siunitx::parseOptions(column.si);
      // What siunitx does with its options that this does not: said, once for each.
      for (const std::string& key : siunitx::unreadOptions(column.si)) {
        // (what stands round a number is read as text, which is told below where it matters)
        if (key == "table-align-text-before" || key == "table-align-text-after" || key == "input-symbols") continue;
        if (badCommands.insert("si" + key).second) {
          _diags.warn(node(env).span, "tabularray: si option `" + key + "' is not supported: left out");
        }
      }
      if (siOptions[c].tableExponent && badCommands.insert("siexponent").second) {
        _diags.warn(node(env).span, "tabularray: the exponent part of si's table-format is not supported: left out");
      }
      for (const NodeId row : rows) {
        if (c >= static_cast<int>(count(row))) continue;
        const std::string raw = trimmed(cellText(child(row, static_cast<std::uint32_t>(c))));
        const SNumber num = sNumber(raw);
        if (!num.numeric) {
          // A number with a symbol round it (`1.5*`, `(2.0)`): siunitx's input-symbols set it apart, as text.
          std::string bare;
          for (const char ch : raw) {
            if (ch != '*' && ch != '(' && ch != ')') bare += ch;
          }
          if (bare != raw && sNumber(bare).numeric && badCommands.insert("siinputsymbols").second) {
            _diags.warn(node(env).span,
                        "tabularray: a symbol round a number in an si column is set as text, not as siunitx sets it");
          }
          continue;
        }
        siDecimals[c] = std::max(siDecimals[c], num.decimals);
        if (num.marker) siMark[c] = num.mark;
      }
    }
    // The array row each table row is, to say which rows a vertical rule runs through.
    std::vector<int> arrayRowOf;
    // The cells (row, column; from 0) a row span covers.
    std::set<std::pair<int, int>> covered;
    for (int k = 0; k < n; k++) {
      const NodeId row = rows[static_cast<std::size_t>(k)];
      rule(k + 1);
      arrayRowOf.push_back(arr->rows());
      const std::uint32_t cells = count(row);
      for (std::uint32_t c = 0; c < cells;) {
        if (c > 0) arr->addCol();
        // What a row span covers was written as cells of its own, which are not drawn.
        if (covered.count({k, static_cast<int>(c)}) > 0) {
          c++;
          continue;
        }
        const NodeId list = child(child(row, c), 0);
        const TblrSetting s = tblrCell(spec, k + 1, static_cast<int>(c) + 1, n, columns);
        // A number of a siunitx column is set as \num sets it, with its digits in groups, unless the cell is
        // guarded (a `cmd=` of its own, or `guard`).
        bool setAsNumber = false;
        if (siDecimals.count(static_cast<int>(c)) > 0 && !s.hasCmd) {
          const std::string raw = trimmed(cellText(child(row, c)));
          std::string problem;
          if (!raw.empty() && raw.find_first_of("\\{}$") == std::string::npos) {
            const std::string set = siunitx::number(raw, problem);
            const SNumber num = sNumber(raw);
            if (problem.empty() && !set.empty()) {
              // The room siunitx keeps: what `table-format` says, else what the column's own numbers need.
              const siunitx::Options& so = siOptions[static_cast<int>(c)];
              std::string pad, before;
              const int most = so.tableDecimals >= 0 ? so.tableDecimals : siDecimals[static_cast<int>(c)];
              if (num.numeric && !num.marker && most > 0) {
                pad += "\\phantom{" + std::string(1, siMark.count(static_cast<int>(c)) > 0 ? siMark[static_cast<int>(c)] : '.') + "}";
              }
              for (int d = num.decimals; num.numeric && d < most; d++) pad += "\\phantom{0}";
              if (num.numeric && so.tableIntegers >= 0) {
                int digits = 0;
                std::size_t at = raw.find_first_of("+-") == 0 ? 1 : 0;
                for (; at < raw.size() && std::isdigit(static_cast<unsigned char>(raw[at])) != 0; at++) digits++;
                if (so.tableIntegers > digits) before += "\\hphantom{" + std::string(static_cast<std::size_t>(so.tableIntegers - digits), '0') + "}";
                if (so.tableSign && raw.find_first_of("+-") != 0) before += "\\hphantom{\\mathord{-}}";
              }
              arr->add(fragment("$" + before + set + pad + "$", false));
              setAsNumber = true;
            }
          }
        }
        if (setAsNumber) {
          // done
        } else if (text) {
          textCell(list, *arr);
        } else {
          lowerList(list, *arr, 0);
        }
        // `cmd=\a\b` is applied as tabularray applies it, `\a\b{text}`, and `font=` is in force round the whole:
        // `{<font> \a\b{text}}`, with the cell where `\gmcell` stands.
        if (!trimmed(s.cmd).empty()) applyToCell(trimmed(s.cmd) + "{\\gmcell}", "cmd=" + trimmed(s.cmd));
        if (s.hasFont && !trimmed(s.font).empty()) {
          // (no space before `\gmcell`: tabularray's cell text has none at its start either)
          applyToCell("{" + trimmed(s.font) + "\\gmcell}", "font=" + trimmed(s.font));
          // The engine sets small capitals from the source text of what they are in force over, which a cell that
          // is built already has not: not drawn, and said.
          if (s.font.find("\\scshape") != std::string::npos && badCommands.insert("scshape").second) {
            _diags.warn(node(env).span, "tabularray: font=\\scshape: small capitals are not drawn for a table cell: left out");
          }
        }
        if (!s.prepend.empty() || !s.append.empty()) {
          auto cellRow = sptrOf<RowAtom>();
          if (!s.prepend.empty()) cellRow->add(specText(s.prepend, "preto=" + s.prepend, !text));
          if (arr->_root != nullptr) cellRow->add(arr->_root);
          if (!s.append.empty()) cellRow->add(specText(s.append, "appto=" + s.append, !text));
          arr->_root = cellRow;
        }
        if (!s.background.empty()) {
          const color bg = tblrColour(s.background);
          if (!isTransparent(bg)) arr->addCellSpecifier(sptrOf<CellColorAtom>(bg));
        }
        if (!s.foreground.empty()) {
          const color fg = tblrColour(s.foreground);
          if (!isTransparent(fg)) arr->addCellSpecifier(sptrOf<CellForegroundAtom>(fg));
        }
        const int span = std::min(s.colspan, static_cast<int>(cells - c));
        const int rowspan = std::min(s.rowspan, n - k);
        // Where the text stands in the rows it spans, or in a row taller than it: only `m`, `b` and `f` differ from
        // the cell's own place at the top (`t`) and its first baseline (`h`), which a cell in a row is at already.
        const bool vertical = s.valign == 'm' || s.valign == 'b' || s.valign == 'f';
        if (span > 1 || rowspan > 1 || !s.halign.empty() || vertical) {
          // A cell of its own width keeps its column's alignment (an empty one), where a span has `l` unless told.
          auto cell = sptrOf<MulticolumnAtom>(std::max(span, 1), s.halign.empty() && span <= 1 ? "" : s.halign.empty() ? "l" : s.halign,
                                              arr->_root);
          cell->setRowSpan(rowspan, s.valign == 0 ? 't' : s.valign);
          arr->_root = cell;
          if (span > 1) arr->addCol(span);
          // The rows below were written as cells of their own, which are empty and not drawn.
          for (int dr = 1; dr < rowspan; dr++) {
            for (int dc = 0; dc < std::max(span, 1); dc++) covered.insert({k + dr, static_cast<int>(c) + dc});
          }
        }
        // What it spans was written as cells of its own, which are empty.
        c += static_cast<std::uint32_t>(std::max(span, 1));
      }
      // tabularray's rows have 2pt above and below their strut, unless the spec says otherwise.
      const TblrSetting sep = tblrCell(spec, k + 1, 1, n, columns);
      arr->addRowSep(sep.aboveSep.isValid() ? sep.aboveSep : Units::getDimen("2pt"),
                     sep.belowSep.isValid() ? sep.belowSep : Units::getDimen("2pt"));
      if (!node(row).raw.empty()) arr->addRowGap(Units::getDimen(node(row).raw));
      arr->addRow();
    }
    rule(n + 1);
    for (const auto& [line, name] : ruleColours) {
      const color c = tblrColour(name);
      if (!isTransparent(c)) line->setColor(c);
    }
    // Vertical rules: the last written for a place wins, and says which rows it runs through.
    for (int b = 0; b <= columns; b++) {
      const TblrRule* last = nullptr;
      for (const TblrRule& r : spec.vertical) {
        if (r.at.matches(b + 1, columns + 1)) last = &r;
      }
      if (last == nullptr) continue;
      VlineStyle style;
      style.thickness = last->thickness;
      style.dashed = last->dashed;
      if (!last->colour.empty()) {
        const color c = tblrColour(last->colour);
        if (!isTransparent(c)) {
          style.hasColour = true;
          style.colour = c;
        }
      }
      // A caption and the notes stand outside the table's rules.
      if (!last->cols.all || !spec.notes.empty() || captionRows > 0) {
        style.rows.assign(static_cast<std::size_t>(arr->rows()) + 2, false);
        if (last->cols.all) {
          for (int r = captionRows; r < arr->rows(); r++) style.rows[static_cast<std::size_t>(r)] = true;
        } else {
          for (int k = 0; k < n; k++) {
            if (last->cols.matches(k + 1, n)) style.rows[static_cast<std::size_t>(arrayRowOf[static_cast<std::size_t>(k)])] = true;
          }
        }
      }
      arr->_vlineStyles[b] = std::move(style);
    }
    // Notes, as tabularray's `normal` note template sets them: a paragraph each, its tag in a box of
    // 0.7em at the left, then the text. Left in one line (no wrapping to the table's width yet).
    for (const TblrNote& note : spec.notes) {
      const std::string tag = trimmed(note.tag);
      // The tag in a box of 0.7em (`\hangindent`), the text after it, its wrapped lines under the text.
      const sptr<Atom> lead = fragment(
        "\\makebox[0.7em][l]{" + (tag.empty() ? std::string() : "\\textsuperscript{\\textsf{" + tag + "}}") + "}", false);
      const sptr<Atom> text = specText(note.text, "note{" + tag + "}", false);
      auto whole = sptrOf<RowAtom>();
      whole->add(lead);
      if (text != nullptr) whole->add(text);
      auto atom = sptrOf<HangingAtom>(whole, lead, text == nullptr ? sptr<Atom>(sptrOf<EmptyAtom>()) : text);
      auto span = sptrOf<MulticolumnAtom>(std::max(columns, 1), "l", atom);
      span->setWrapToTable(true);
      span->report = [at = node(env).span](const std::string& m) { warnLater(at, "tabularray: " + m); };
      arr->add(span);
      if (columns > 1) arr->addCol(columns);
      // tabularray's `footsep` (6pt) above the first, the rest a baselineskip apart.
      const bool first = &note == &spec.notes.front();
      arr->addRowSep(first ? (spec.footsep.isValid() ? spec.footsep : Units::getDimen("6pt")) : Units::getDimen("0pt"),
                     Units::getDimen("0pt"));
      arr->addRow();
    }
    for (const std::string& key : spec.unsupported) {
      _diags.warn(node(env).span, "tabularray: `" + key + "' is not supported: left out");
    }
    return arr;
  }

  /** A row with nothing in it: the one a final \\ starts. */
  /** A row of a table that has something of its own: not only spaces, rules and the colour of the row. */
  bool hasContent(NodeId row) const {
    for (std::uint32_t c = 0; c < count(row); c++) {
      const NodeId list = child(child(row, c), 0);
      for (std::uint32_t i = 0; i < count(list); i++) {
        const Node& x = node(child(list, i));
        if (isSpace(x)) continue;
        if (x.kind == NodeKind::command && !x.flag && (isRule(x.text) || x.text == "rowcolor" || x.text == "noalign")) continue;
        return true;
      }
    }
    return false;
  }

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

  /** The number of the next numbered row: the next of the equation counter,
   *  or in `subequations` its number with the next letter. */
  std::string nextNumber() {
    if (!nums().sub) return std::to_string(++nums().equation);
    std::string letters;
    for (int n = ++nums().subIndex; n > 0; n = (n - 1) / 26) {
      letters.insert(letters.begin(), static_cast<char>('a' + (n - 1) % 26));
    }
    return std::to_string(nums().equation) + letters;
  }

  /** `subequations` begins: it takes a number of the equation counter, which
   *  a \label right after it names, and the rows in it are lettered. */
  void beginSubequations() {
    nums().sub = true;
    nums().subIndex = 0;
    nums().current = std::to_string(++nums().equation);
    nums().currentKind = "equation";
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
      text = nextNumber();
      tag = numberOf(text, true);
    }
    if (tag != nullptr) {
      arr._rowTags[row] = tag;
      nums().current = text;
      nums().currentKind = "equation";
    }
    for (const auto& key : n.labels) {
      (*nums().labels)[key] = nums().current;
      (*nums().kinds)[key] = nums().currentKind;
    }
  }

  /** The content of a display, `$$...$$` or `\[...\]`: its list, with a
   *  \tag it holds set at the right of it. */
  sptr<Atom> displayBody(NodeId list) {
    RowNote outer = std::move(_note);
    _note = RowNote();
    _inRow++;
    _inFamilyRow++;
    const sptr<Atom> body = build(list);
    _inRow--;
    _inFamilyRow--;
    const bool tagged = _note.tag != nullptr;
    auto arr = sptrOf<ArrayFormula>();
    // `\[\tag{1}\]`: a display with nothing in it still has its number.
    if (body != nullptr || tagged) arr->add(body != nullptr ? body : sptrOf<EmptyAtom>());
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
    // In a numbered row (align, gather, equation: `_inFamilyRow`) the label waits for the row's number. Anywhere else -- a
    // list item, a table cell, an aligned on its own -- it names what is current, as LaTeX's \label does: the item's
    // number, the float's, the section's. It used to wait for a row number that nothing gave, and \ref found nothing.
    if (_inFamilyRow > 0) {
      _note.labels.push_back(key);
    } else {
      (*nums().labels)[key] = nums().current;
      (*nums().sublabels)[key] = nums().subCurrent;
      (*nums().kinds)[key] = nums().currentKind;
    }
  }

  /** What a \caption in a float starts with, and the number a \label after it names: "Figure 1: " (the counter steps
   *  here, as \refstepcounter does in \caption), or in a subfigure "(a) ", whose label is the figure's number, which the
   *  main caption will take, and the letter. Nothing outside a float. */
  std::string captionLabel() {
    if (_floatKind.empty()) return std::string();
    if (!_subKind.empty()) {
      const std::string letter = formatCounter("alph", ++nums().counters[_subKind].value);
      nums().current = std::to_string(nums().counters[_floatKind].value + 1) + letter;
      nums().currentKind = _floatKind;
      nums().subCurrent = letter;
      return "(" + letter + ")~";
    }
    const int n = ++nums().counters[_floatKind].value;
    nums().current = std::to_string(n);
    nums().currentKind = _floatKind;
    nums().subCurrent.clear();
    return std::string(_floatKind == "table" ? "Table" : "Figure") + "~" + std::to_string(n) + ": ";
  }

  /** The name of the length an argument names, `\tabcolsep` as `tabcolsep`.
   *  For \setlength and \addtolength the length has to be defined, or this
   *  warns and answers empty; \newlength's is a new name. */
  std::string lengthName(NodeId id, std::uint32_t arg, bool mustExist) {
    std::string name = trimmed(rawOf(child(id, arg)));
    if (!name.empty() && name[0] == '\\') name.erase(0, 1);
    if (name.empty() || (mustExist && !nums().lengths.known(name))) {
      _diags.warn(node(id).span, "length `\\" + name + "' is not defined: \\newlength makes one");
      return std::string();
    }
    return name;
  }

  /** Whether `s` is a length: a number and a unit, or a number of a length
   *  that is defined (`2\tabcolsep`, `\textwidth`). Units::getDimen() reads
   *  any word as a unit, in pixels, so it cannot say. */
  bool isLengthText(const std::string& s) {
    static const std::set<std::string> units = {"em", "ex", "pt", "bp", "pc", "cm", "mm", "in",
                                                "sp", "dd", "cc", "mu", "px"};
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) != 0 || s[i] == '.')) i++;
    std::string rest = trimmed(s.substr(i));
    if (rest.empty()) return false;
    if (rest[0] == '\\') {
      const std::string name = trimmed(rest.substr(1));
      return name == "textwidth" || name == "linewidth" || name == "columnwidth" ||
             nums().lengths.known(name);
    }
    for (char& c : rest) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return units.count(rest) > 0;
  }

  /** `1.5em`, for a message. */
  static std::string describe(const Dimen& d) {
    std::ostringstream out;
    out << d.val;
    static const char* names[] = {"em", "ex", "px", "bp", "pc", "mu", "cm", "mm", "in", "sp", "pt", "dd", "cc", "tt", "tw"};
    const int unit = static_cast<int>(d.unit);
    if (unit >= 0 && unit < 15) out << names[unit];
    return out.str();
  }

  bool counterIsKnown(const std::string& key) {
    return key == "equation" || key == "section" || key == "subsection" || key == "subsubsection" ||
           nums().counters.count(key) > 0;
  }

  /** Whether `name` is one of the commands that write a counter. */
  static bool isCounterStyle(const std::string& name) {
    static const std::set<std::string> styles = {"arabic", "roman", "Roman", "alph", "Alph", "fnsymbol"};
    return styles.count(name) > 0;
  }

  /** The value of the counter `name`: the ones LaTeX has that the engine
   *  keeps itself, and the ones \newcounter and \newtheorem made. Null, with
   *  a warning, for none. */
  int* counterOf(NodeId at, const std::string& name) {
    if (name == "equation") return &nums().equation;
    if (name == "section") return &_section[0];
    if (name == "subsection") return &_section[1];
    if (name == "subsubsection") return &_section[2];
    const auto it = nums().counters.find(name);
    if (it != nums().counters.end()) return &it->second.value;
    _diags.warn(node(at).span, "no counter `" + name + "': \\newcounter makes one");
    return nullptr;
  }

  /** A counter's number: an integer, or \value{other}. */
  bool counterNumber(NodeId at, const std::string& raw, int& out) {
    const std::string s = trimmed(raw);
    if (s.rfind("\\value{", 0) == 0 && s.back() == '}') {
      const int* other = counterOf(at, trimmed(s.substr(7, s.size() - 8)));
      if (other == nullptr) return false;
      out = *other;
      return true;
    }
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0') {
      _diags.warn(node(at).span, "`" + s + "' is not a number for a counter");
      return false;
    }
    out = static_cast<int>(v);
    return true;
  }

  /** \stepcounter: one more, and every counter made `within` it starts again. */
  void stepCounter(const std::string& name) {
    if (name == "equation") {
      nums().equation++;
    } else if (name == "section" || name == "subsection" || name == "subsubsection") {
      _section[name == "section" ? 0 : name == "subsection" ? 1 : 2]++;
    } else {
      nums().counters[name].value++;
    }
    resetCountersWithin(name);
  }

  /** Every counter made `within` `parent`, and those within them, start again. Counters that are each other's parents
   *  (`\newcounter{a}[a]`, which LaTeX accepts as a loop that never ends) are reset once: `seen` stops the recursion. */
  void resetCountersWithin(const std::string& parent, std::set<std::string>* seen = nullptr) {
    std::set<std::string> here;
    if (seen == nullptr) seen = &here;
    if (!seen->insert(parent).second) return;
    for (auto& entry : nums().counters) {
      if (entry.second.parent != parent) continue;
      entry.second.value = 0;
      resetCountersWithin(entry.first, seen);
    }
  }

  /** LaTeX source with the numbers its counters have now: `\arabic{c}` and the
   *  other styles replaced by their text, so a \label made from \thec keeps the
   *  number the counter had then. */
  std::string withCounterValues(NodeId at, const std::string& source) {
    static const char* styles[] = {"arabic", "roman", "Roman", "alph", "Alph", "fnsymbol"};
    std::string out;
    for (std::size_t i = 0; i < source.size();) {
      bool replaced = false;
      if (source[i] == '\\') {
        for (const char* style : styles) {
          const std::string head = std::string("\\") + style + "{";
          if (source.compare(i, head.size(), head) != 0) continue;
          const std::size_t close = source.find('}', i + head.size());
          if (close == std::string::npos) break;
          const int* v = counterOf(at, trimmed(source.substr(i + head.size(), close - i - head.size())));
          if (v != nullptr) out += formatCounter(style, *v);
          i = close + 1;
          replaced = true;
          break;
        }
      }
      if (!replaced) out += source[i++];
    }
    return out;
  }

  /** The colour `spec` names, or TRANSPARENT -- no colour, so the colour
   *  around stays -- with a warning at `id`: xcolor stops with "Undefined
   *  color", and we recover. */
  color colourOf(NodeId id, const std::string& spec) {
    color c = TRANSPARENT;
    if (ColorAtom::parseColor(spec, c)) return c;
    _diags.warn(node(id).span, "undefined colour `" + trimmed(spec) + "': the colour is not changed");
    return TRANSPARENT;
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
    for (const auto& c : nums().crossrefs) {
      for (const auto& key : c.keys) {
        if (key.empty() || nums().labels->count(key) > 0) continue;
        _diags.warn(c.at, "reference `" + key + "' is undefined: drawn as ??");
      }
    }
    for (const auto& c : nums().cites) {
      const std::string drawn = c.form == "citet" ? "(author?) [?]" : c.form == "citealp" ? "?" : "[?]";
      for (const auto& key : c.keys) {
        // No key at all is a missing argument, which the parser has said.
        if (key.empty() || nums().bibitems->count(key) > 0) continue;
        _diags.warn(c.at, "citation `" + key + "' is undefined: drawn as " + drawn);
      }
    }
  }

  /** A command that works on the alignment it is in, not on the cell's
   *  content: a rule, \intertext, \multicolumn, \cellcolor ... */
  static bool actsOnAlignment(const Node& x) {
    static const std::set<std::string> names = {"intertext", "multicolumn", "multirow",
                                                "hdotsfor", "cellcolor", "rowcolor"};
    return x.kind == NodeKind::command && !x.flag && (isRule(x.text) || names.count(x.text) > 0);
  }

  /** A cell of dcolumn's D column, read in math: split at the first `in` that is not inside a group,
   *  and drawn as `out`. A cell without it is all before the separator. */
  sptr<Atom> decimalCell(NodeId list, const std::pair<std::string, std::string>& sep) {
    std::uint32_t from = 0, to = count(list);
    while (from < to && isSpace(node(child(list, from)))) from++;
    while (to > from && isSpace(node(child(list, to - 1)))) to--;
    std::uint32_t at = to;
    for (std::uint32_t i = from; i < to && !sep.first.empty(); i++) {
      const Node& x = node(child(list, i));
      if (x.kind == NodeKind::character && x.cp == static_cast<c32>(static_cast<unsigned char>(sep.first[0]))) {
        at = i;
        break;
      }
    }
    Formula before, after;
    for (std::uint32_t i = from; i < at; i++) lowerItem(child(list, i), before);
    for (std::uint32_t i = at + 1; i < to; i++) lowerItem(child(list, i), after);
    sptr<Atom> mark;
    if (at < to) mark = fragment("\\mathord{" + (sep.second.empty() ? sep.first : sep.second) + "}", true);
    return sptrOf<DecimalAtom>(before._root, mark, at < to ? after._root : nullptr);
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
  Diagnostics unreported;
  return buildFragment(latex, math, unreported);
}

sptr<Atom> buildFragment(const std::string& latex, bool math, Diagnostics& problems,
                         std::shared_ptr<const UserScope> scope) {
  if (latex.empty()) return nullptr;
  // A fragment is parsed afresh, its depth from 0, so a fragment inside a
  // fragment (a raw argument in a raw argument) is counted here.
  static int depth = 0;
  if (depth >= kMaxDepth) throw ex_parse("Input nested too deeply");
  struct Deeper {
    Deeper() { depth++; }
    ~Deeper() { depth--; }
  } deeper;
  const Ast ast = parseLatex(latex, math ? Mode::math : Mode::text, problems, false, false, 0, UINT32_MAX, true, {},
                             std::move(scope));
  if (ast.root == kNoNode) return nullptr;
  Formula g;
  Lowerer(ast, problems).run(g, false, false);
  return g._root;
}

}  // namespace microtex::front
