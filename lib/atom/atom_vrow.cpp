#include "atom/atom_vrow.h"

#include <algorithm>
#include <set>
#include <vector>

#include "atom/atom_row.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "core/split.h"
#include "env/env.h"
#include "graphic/graphic.h"

using namespace microtex;
using namespace std;

VRowAtom::VRowAtom() {
  _addInterline = false;
  _valign = Alignment::center;
  _halign = Alignment::none;
  _raise = sptrOf<SpaceAtom>(UnitType::ex, 0.f, 0.f, 0.f);
}

VRowAtom::VRowAtom(const sptr<Atom>& base) {
  _addInterline = false;
  _valign = Alignment::center;
  _halign = Alignment::none;
  _raise = sptrOf<SpaceAtom>(UnitType::ex, 0.f, 0.f, 0.f);
  if (base != nullptr) {
    auto* a = dynamic_cast<VRowAtom*>(base.get());
    if (a != nullptr) {
      _elements.insert(_elements.end(), a->_elements.begin(), a->_elements.end());
    } else {
      _elements.push_back(base);
    }
  }
}

void VRowAtom::setRaise(UnitType unit, float r) {
  _raise = sptrOf<SpaceAtom>(unit, r, 0.f, 0.f);
}

void VRowAtom::prepend(const sptr<Atom>& el) {
  if (el != nullptr) _elements.insert(_elements.begin(), el);
}

void VRowAtom::append(const sptr<Atom>& el) {
  if (el != nullptr) _elements.push_back(el);
}

void VRowAtom::addGapAfterLast(const Dimen& gap) {
  if (!_elements.empty()) _gaps[_elements.size() - 1] = gap;
}

namespace {

/** The rows of an alignment display, found inside the boxes it is wrapped in: TeX sets such a display as a vertical list,
 *  so the space above it is measured to its first row's baseline and below it from its last row's. */
const VBox* rowsOf(const sptr<Box>& box) {
  if (const auto* v = dynamic_cast<const VBox*>(box.get())) return v->_children.empty() ? nullptr : v;
  if (const auto* d = dynamic_cast<const DecorBox*>(box.get())) return rowsOf(d->_base);
  if (const auto* h = dynamic_cast<const HBox*>(box.get())) {
    const VBox* found = nullptr;
    for (const auto& child : h->_children) {
      if (child->_height + child->_depth <= 0.f) continue;
      if (found != nullptr) return nullptr;
      found = rowsOf(child);
      if (found == nullptr) return nullptr;
    }
    return found;
  }
  return nullptr;
}

/** The columns of a multicols that `box` is, or wraps alone (a line holds nothing else). */
const ColumnsBox* columnsOf(const sptr<Box>& box) {
  if (const auto* c = dynamic_cast<const ColumnsBox*>(box.get())) return c;
  if (const auto* d = dynamic_cast<const DecorBox*>(box.get())) return columnsOf(d->_base);
  if (const auto* h = dynamic_cast<const HBox*>(box.get())) {
    const ColumnsBox* found = nullptr;
    for (const auto& child : h->_children) {
      if (child->_height + child->_depth <= 0.f) continue;
      if (found != nullptr) return nullptr;
      found = columnsOf(child);
      if (found == nullptr) return nullptr;
    }
    return found;
  }
  return nullptr;
}

/** The width of the last line `box` makes when it is broken to `width`: what TeX calls predisplaysize, less the 2em. */
float lastLineWidth(const sptr<Box>& box, float width, float baselineSkip) {
  if (width == POS_INF || box->_width <= width) return box->_width;
  const auto [split, lines] = BoxSplitter::split(box, width, baselineSkip);
  const auto vb = std::dynamic_pointer_cast<VBox>(lines);
  if (!split || vb == nullptr || vb->_children.empty()) return box->_width;
  return vb->_children.back()->_width;
}

}  // namespace

sptr<Box> VRowAtom::createBox(Env& env) {
  auto vb = sptrOf<VBox>();
  // Interline space is what makes these a label's lines (getAsVRow()).
  vb->_rows = _addInterline;
  // The space between two lines: TeX's interline glue (Env::interline()), from the depth of the one to the height of the next.
  const auto lineSpace = [&](const sptr<Box>& above, const sptr<Box>& below) {
    return sptrOf<StrutBox>(0.f, env.interline(above->_depth, below->_height), 0.f, 0.f);
  };
  // `\\[len]`: extra space below an element that has one.
  const auto gapAfter = [&](size_t i) {
    const auto it = _gaps.find(i);
    if (it != _gaps.end()) vb->add(sptrOf<StrutBox>(0.f, Units::fsize(it->second, env), 0.f, 0.f));
  };

  if (_halign != Alignment::none) {
    float maxWidth = F_MIN;
    vector<sptr<Box>> boxes;
    const size_t size = _elements.size();
    // find the width of the widest box
    for (auto& atom : _elements) {
      auto box = atom->createBox(env);
      boxes.push_back(box);
      maxWidth = std::max(maxWidth, box->_width);
    }
    // align the boxes and add it to the vertical box
    for (int i = 0; i < size; i++) {
      auto box = boxes[i];
      auto hb = sptrOf<HBox>(box, maxWidth, _halign);
      vb->add(hb);
      if (i < size - 1) gapAfter(i);
      if (_addInterline && i < size - 1) vb->add(lineSpace(box, boxes[i + 1]));
    }
  } else {
    // convert atoms to boxes and add to the vertical box
    const size_t size = _elements.size();
    vector<sptr<Box>> boxes;
    float widest = 0;
    for (auto& atom : _elements) {
      boxes.push_back(atom->createBox(env));
      widest = std::max(widest, boxes.back()->_width);
    }
    // TeX's display skips (\abovedisplayskip and \belowdisplayskip, 1em at 10pt) are long when the line before the
    // display reaches past where the equation starts, 2em of room counted, and short (0 above, 0.6em below) when it
    // does not -- or when no text of the paragraph stands before the display. One choice for the display's two skips.
    const float measure = env.textWidth() == POS_INF ? widest : env.textWidth();
    const auto equation = [&](size_t i) -> const DisplayAtom* {
      const auto* d = dynamic_cast<DisplayAtom*>(_elements[i].get());
      return d != nullptr && d->isEquation() ? d : nullptr;
    };
    vector<int> shortSkips(size, -1);
    const auto isShort = [&](size_t d) {
      if (shortSkips[d] < 0) {
        const DisplayAtom* eq = equation(d);
        bool shortSkip = true;
        if (eq != nullptr && eq->interrupts() && d > 0) {
          const float pre = lastLineWidth(boxes[d - 1], measure, env.baselineSkip());
          const float left =
            eq->alignment() == Alignment::center && !eq->fullWidth() ? (measure - eq->innerWidth()) / 2 : 0.f;
          shortSkip = left > pre + 2 * env.em();
        }
        shortSkips[d] = shortSkip ? 1 : 0;
      }
      return shortSkips[d] == 1;
    };
    for (size_t i = 0; i < size; i++) {
      // A display, or a centred or right-aligned line, with no text width
      // to align in is aligned on the widest line.
      const auto* display = dynamic_cast<DisplayAtom*>(_elements[i].get());
      if (env.textWidth() == POS_INF && display != nullptr) {
        boxes[i] = sptrOf<HBox>(boxes[i], widest, display->alignment());
      }
      vb->add(boxes[i]);
      if (i < size - 1) gapAfter(i);
      if (_addInterline && i < size - 1) {
        // An alignment display is measured from its last row below and to its first row above.
        float depth = boxes[i]->_depth, height = boxes[i + 1]->_height;
        if (const auto* d = dynamic_cast<DisplayAtom*>(_elements[i].get()); d != nullptr && d->fullWidth()) {
          if (const VBox* rows = rowsOf(boxes[i])) depth = rows->_children.back()->_depth;
        }
        if (const auto* d = dynamic_cast<DisplayAtom*>(_elements[i + 1].get()); d != nullptr && d->fullWidth()) {
          if (const VBox* rows = rowsOf(boxes[i + 1])) height = rows->_children.front()->_height;
        }
        // Columns of lines are measured from their last line's depth too.
        if (const ColumnsBox* c = columnsOf(boxes[i])) depth = c->_lastDepth;
        vb->add(sptrOf<StrutBox>(0.f, env.interline(depth, height), 0.f, 0.f));
      }
      float skip = 0.f;
      if (i < size - 1 && equation(i + 1) != nullptr) skip += isShort(i + 1) ? 0.f : env.em();
      if (i < size - 1 && equation(i) != nullptr) skip += isShort(i) ? 0.6f * env.em() : env.em();
      if (skip > 0.f) vb->add(sptrOf<StrutBox>(0.f, skip, 0.f, 0.f));
    }
  }

  vb->_shift = -_raise->createBox(env)->_width;
  if (_valign == Alignment::top) {
    float t = vb->size() == 0 ? 0 : vb->_children.front()->_height;
    vb->_height = t;
    vb->_depth = vb->_depth + vb->_height - t;
  } else if (_valign == Alignment::center) {
    const float axis = env.axisHeight();
    const float h = vb->_height + vb->_depth;
    vb->_height = h / 2 + axis;
    vb->_depth = h / 2 - axis;
  } else {
    float t = vb->size() == 0 ? 0 : vb->_children.back()->_depth;
    vb->_height = vb->_depth + vb->_height - t;
    vb->_depth = t;
  }
  return vb;
}

sptr<Box> DisplayAtom::createBox(Env& env) {
  gDisplayFormulaWidth = -1.f;
  auto box = _base->createBox(env);
  const float width = env.textWidth();
  _innerWidth = std::min(box->_width, width);
  if (gDisplayFormulaWidth >= 0.f && width != POS_INF) {
    // A numbered display is centred by its formula: TeX puts it (z - w) / 2 from the left, and when that is under twice the
    // number's width, (z - w - e) / 2, the number taking its room. The row is as wide as the line, the formula is not.
    const float w = gDisplayFormulaWidth, e = gDisplayNumberWidth;
    _innerWidth = (width - w) / 2 < 2 * e ? w + e : w;
  }
  gDisplayFormulaWidth = -1.f;
  if (width == POS_INF) return box;
  if (box->_width <= width) return sptrOf<HBox>(box, width, _align);
  // Too wide for the line: broken as any line is, and each line it makes
  // aligned -- not justified, as LaTeX's \centering leaves them.
  const bool justify = BoxSplitter::_justify;
  BoxSplitter::_justify = false;
  const auto [split, lines] = BoxSplitter::split(box, width, env.baselineSkip());
  BoxSplitter::_justify = justify;
  const auto vb = std::dynamic_pointer_cast<VBox>(lines);
  if (!split || vb == nullptr) return box;
  auto out = sptrOf<VBox>();
  out->_lines = true;
  for (const auto& line : vb->_children) {
    if (std::dynamic_pointer_cast<HBox>(line) != nullptr) {
      out->add(sptrOf<HBox>(line, width, _align));
    } else {
      out->add(line);  // the space between lines
    }
  }
  return out;
}

namespace {

// The baseline of the last line in `b`, from its top: a paragraph broken
// into lines is a column whose own baseline is its first line.
float lastBaseline(const sptr<Box>& b) {
  const auto v = std::dynamic_pointer_cast<VBox>(b);
  if (v == nullptr || !v->_lines || v->_children.empty()) return b->_height;
  const auto& last = v->_children.back();
  return v->_height + v->_depth - (last->_height + last->_depth) + lastBaseline(last);
}

}  // namespace

sptr<Box> MinipageAtom::createBox(Env& env) {
  if (_body == nullptr) return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
  const float width = Units::fsize(_width, env);
  const bool measured = width > 0 && width != POS_INF;
  sptr<Box> box;
  {
    // The breaker needs word-level runs, as a p{} cell does: folded into
    // one phrase a paragraph could not be broken.
    const MergeTextGuard guard(true);
    box = env.withTextWidth(measured ? width : env.textWidth(),
                            [&](Env& e) { return _body->createBox(e); });
  }
  if (measured) box = BoxSplitter::split(box, width, env.baselineSkip()).second;

  // A column of lines, whose baseline can be put where the position says.
  auto vb = std::dynamic_pointer_cast<VBox>(box);
  if (vb == nullptr || vb->_children.empty()) {
    vb = sptrOf<VBox>();
    vb->add(box);
  }
  // Measured from its top: the body's own height, and where its first and
  // last baselines are in it -- the last paragraph's last line, when the
  // breaker broke it.
  const float body = vb->_height + vb->_depth;
  const float first = vb->_children.front()->_height;
  const auto& back = vb->_children.back();
  const float last = body - (back->_height + back->_depth) + lastBaseline(back);
  // A height of its own, taller than the body: the room goes where the
  // inner position puts the body (below it for `t`, above for `b`).
  float total = body;
  float above = 0;
  const float height = Units::fsize(_height, env);
  if (_height.isValid() && height > body) {
    const float room = height - body;
    above = _inner == 't' ? 0.f : _inner == 'b' ? room : room / 2;
    total = height;
  }
  const float baseline = _position == 't'   ? above + first
                         : _position == 'b' ? above + last
                                            : total / 2 + env.axisHeight();
  if (above > 0) {
    // Drawn from its top down: a strut there moves the body down by it.
    vb->add(0, sptrOf<StrutBox>(0.f, above, 0.f, 0.f));
  }
  vb->_height = baseline;
  vb->_depth = total - baseline;
  if (!measured) return vb;
  return sptrOf<HBox>(vb, width, Alignment::left);
}

sptr<Box> IndentAtom::createBox(Env& env) {
  if (_body == nullptr) return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
  const float em = env.em();
  const float measure = env.textWidth();
  const float left = _left * em;
  const float width = measure == POS_INF ? POS_INF : std::max(measure - left - _right * em, em);
  sptr<Box> box;
  {
    // The breaker needs word-level runs, as in a minipage.
    const MergeTextGuard guard(true);
    box = env.withTextWidth(width, [&](Env& e) { return _body->createBox(e); });
  }
  if (width != POS_INF) box = BoxSplitter::split(box, width, env.baselineSkip()).second;
  auto row = sptrOf<HBox>();
  row->add(sptrOf<StrutBox>(left, 0.f, 0.f, 0.f));
  row->add(box);
  return row;
}

namespace {

/** The frame, the fills and the two boxes of a tcolorbox, drawn from its top-left corner. */
class TcolorboxBox : public Box {
public:
  TcolorboxBox(const TcolorboxStyle& style, const sptr<Box>& title, const sptr<Box>& body, float width, float rule,
               float arc, float titleBand, float titleTop, float bodyTop, float height, float left)
      : _style(style), _title(title), _body(body), _rule(rule), _arc(arc), _band(titleBand), _titleTop(titleTop),
        _bodyTop(bodyTop), _left(left) {
    _width = width;
    _height = height;
    _depth = 0.f;
  }

  void draw(Graphics2D& g2, float x, float y) override {
    const color was = g2.getColor();
    const float top = y - _height;
    const float outer = _arc > 0.f ? _rule + _arc : 0.f;
    const auto round = [&](color c, float rx, float ry, float rw, float rh, float r) {
      g2.setColor(c);
      if (r > 0.f) {
        g2.fillRoundRect(rx, ry, rw, rh, r, r);
      } else {
        g2.fillRect(rx, ry, rw, rh);
      }
    };
    // The frame: its colour over the whole box, the fills over what is inside it.
    if (_rule > 0.f) round(_style.frame, x, top, _width, _height, outer);
    const float ix = x + _rule, iw = _width - 2 * _rule, iy = top + _rule, ih = _height - 2 * _rule;
    float by = iy, bh = ih;
    if (_title != nullptr) {
      by = iy + _band + _rule;
      bh = ih - _band - _rule;
      if (!isTransparent(_style.backTitle)) {
        // Rounded at the top only: the lower corners are drawn over by the rule and the body.
        const float extra = std::min(2 * _arc, _rule + bh);
        round(_style.backTitle, ix, iy, iw, _band + extra, _arc);
        g2.setColor(_style.frame);
        g2.fillRect(ix, iy + _band, iw, _rule);
      }
    }
    round(_style.back, ix, by, iw, bh, _arc);
    if (_arc > 0.f && _title != nullptr) {
      // The body is square where it meets the title.
      g2.setColor(_style.back);
      g2.fillRect(ix, by, iw, std::min(_arc, bh));
    }
    g2.setColor(was);
    if (_title != nullptr) _title->draw(g2, x + _left, iy + _titleTop + _title->_height);
    _body->draw(g2, x + _left, by + _bodyTop + _body->_height);
  }

  int lastFontId() override { return _body->lastFontId(); }

  std::vector<sptr<Box>> descendants() const override {
    return _title != nullptr ? std::vector<sptr<Box>>{_title, _body} : std::vector<sptr<Box>>{_body};
  }

  bool isSingle() const override { return false; }

  boxname(TcolorboxBox);

private:
  TcolorboxStyle _style;
  sptr<Box> _title, _body;
  // The frame's rule and inner arc, the title's band, and from the inside's top (title) or the body region's top
  // (body) to the box drawn there; `_left` is where both start from the left edge.
  float _rule, _arc, _band, _titleTop, _bodyTop, _left;
};

}  // namespace

sptr<Box> TcolorboxAtom::createBox(Env& env) {
  const float em = env.em();
  const TcolorboxStyle& s = _style;
  const float rule = s.rule * em, arc = s.sharp ? 0.f : s.arc * em, left = s.left * em, right = s.right * em;
  const float top = s.top * em, bottom = s.bottom * em, sep = s.boxsep * em;
  const float lead = rule + left + sep;
  const float sides = lead + rule + right + sep;
  const float measure = env.textWidth();
  const auto setBox = [&](const sptr<Atom>& atom, float width) {
    sptr<Box> box;
    {
      // The breaker needs word-level runs, as in a minipage.
      const MergeTextGuard guard(true);
      box = env.withTextWidth(width, [&](Env& e) { return atom->createBox(e); });
    }
    if (width != POS_INF) box = BoxSplitter::split(box, width, env.baselineSkip()).second;
    return box;
  };
  const float inner = measure == POS_INF ? POS_INF : std::max(measure - sides, 0.f);
  sptr<Box> body = setBox(_body, inner);
  sptr<Box> title = _title != nullptr ? setBox(_title, inner) : nullptr;
  const float width = measure == POS_INF
                        ? sides + std::max(body->_width, title != nullptr ? title->_width : 0.f)
                        : measure;
  // From the top of the inside: the title's band, and the body's room.
  const float band = title != nullptr ? title->_height + title->_depth + 2 * sep : 0.f;
  const float bodyRoom = top + sep + body->_height + body->_depth + sep + bottom;
  const float height = 2 * rule + (title != nullptr ? band + rule : 0.f) + bodyRoom;
  return sptrOf<TcolorboxBox>(s, title, body, width, rule, arc, band, sep, top + sep, height, lead);
}

namespace {

class ColumnBreakBox : public Box {
public:
  void draw(Graphics2D&, float, float) override {}
  boxname(ColumnBreakBox);
};

bool hasColumnBreak(const sptr<Box>& box) {
  if (dynamic_cast<const ColumnBreakBox*>(box.get()) != nullptr) return true;
  for (const auto& child : box->descendants()) {
    if (hasColumnBreak(child)) return true;
  }
  return false;
}

/** The lines and gaps of a column, one below the other: the rows of a body and the lines of its paragraphs. `gaps`
 *  collects the glue between rows, the paragraphs' `\parskip`, which a short column stretches; the glue between a
 *  paragraph's own lines does not stretch. */
/** The list that a line holds alone, whose items a column may break between. */
sptr<VBox> flowOf(const sptr<Box>& box) {
  if (const auto v = std::dynamic_pointer_cast<VBox>(box)) return v->_flow ? v : nullptr;
  if (const auto h = std::dynamic_pointer_cast<HBox>(box)) {
    sptr<VBox> found;
    for (const auto& child : h->_children) {
      if (child->_height + child->_depth <= 0.f) continue;
      if (found != nullptr) return nullptr;
      found = flowOf(child);
      if (found == nullptr) return nullptr;
    }
    return found;
  }
  return nullptr;
}

/** Whether a box is a column of lines or rows, or only holds one (so that it can be cut between its lines). */
bool holdsLines(const sptr<Box>& box) {
  if (const auto v = std::dynamic_pointer_cast<VBox>(box)) return v->_lines || v->_rows;
  if (const auto h = std::dynamic_pointer_cast<HBox>(box)) {
    sptr<Box> only;
    for (const auto& child : h->_children) {
      if (child->_height + child->_depth <= 0.f) continue;
      if (only != nullptr) return false;
      only = child;
    }
    return only != nullptr && holdsLines(only);
  }
  return false;
}

void columnLeaves(const sptr<Box>& box, std::vector<sptr<Box>>& out, std::set<const Box*>& gaps) {
  if (const sptr<VBox> list = flowOf(box); list != nullptr && !list->_children.empty()) {
    for (const auto& item : list->_children) out.push_back(item);
    return;
  }
  // A paragraph's lines in a wrapper of their own (a row of the document): the lines are the leaves.
  if (const auto h = std::dynamic_pointer_cast<HBox>(box); h != nullptr && holdsLines(box)) {
    for (const auto& child : h->_children) {
      if (child->_height + child->_depth > 0.f) {
        columnLeaves(child, out, gaps);
        return;
      }
    }
  }
  const auto v = std::dynamic_pointer_cast<VBox>(box);
  // A box of paragraphs, a display and the gaps between them is a VBox with no flag of its own (the document's rows);
  // it is cut between its parts when one of them is a column of lines, which a fraction's box has none of.
  bool cuttable = v != nullptr && (v->_lines || v->_rows);
  if (v != nullptr && !cuttable) {
    for (const auto& child : v->_children) cuttable = cuttable || holdsLines(child);
  }
  if (v != nullptr && cuttable && !v->_children.empty()) {
    for (const auto& child : v->_children) {
      if (!v->_lines && child->isSpace()) gaps.insert(child.get());
      columnLeaves(child, out, gaps);
    }
  } else {
    out.push_back(box);
  }
}

}  // namespace

sptr<Box> ColumnBreakAtom::createBox(Env&) {
  return sptrOf<ColumnBreakBox>();
}

sptr<Box> MulticolsAtom::createBox(Env& env) {
  const float measure = env.textWidth();
  const auto setBox = [&](const sptr<Atom>& atom, float width) {
    sptr<Box> box;
    {
      // The breaker needs word-level runs, as in a minipage.
      const MergeTextGuard guard(true);
      box = env.withTextWidth(width, [&](Env& e) { return atom->createBox(e); });
    }
    if (width != POS_INF) box = BoxSplitter::split(box, width, env.baselineSkip()).second;
    return box;
  };
  const float sep = Units::fsize(_sep, env);
  const int n = std::max(_count, 1);
  const float colWidth = measure == POS_INF ? POS_INF : (measure - (n - 1) * sep) / n;
  if (measure == POS_INF || n < 2 || colWidth <= 0.f) return setBox(_body, measure);

  std::vector<sptr<Box>> leaves;
  std::set<const Box*> gaps;
  columnLeaves(setBox(_body, colWidth), leaves, gaps);
  // Columns of at most `limit` high, filled from the top, a break after a line that holds a \columnbreak. Glue at the
  // top of a column is dropped, as TeX drops it where a page breaks, and at the bottom it takes no room.
  using Column = std::vector<sptr<Box>>;
  const auto fill = [&](float limit) {
    std::vector<Column> cols(1);
    float used = 0.f, pending = 0.f;
    for (const auto& leaf : leaves) {
      const float h = leaf->_height + leaf->_depth;
      if (leaf->isSpace() || (leaf->_width == 0.f && h == 0.f && !hasColumnBreak(leaf))) {
        if (!cols.back().empty()) {
          cols.back().push_back(leaf);
          pending += h;
        }
        continue;
      }
      if (!cols.back().empty() && used + pending + h > limit + 1e-3f) {
        while (!cols.back().empty() && cols.back().back()->isSpace()) cols.back().pop_back();
        cols.emplace_back();
        used = pending = 0.f;
      }
      cols.back().push_back(leaf);
      used += pending + h;
      pending = 0.f;
      if (hasColumnBreak(leaf)) {
        cols.emplace_back();
        used = pending = 0.f;
      }
    }
    while (cols.size() > 1 && cols.back().empty()) cols.pop_back();
    while (!cols.back().empty() && cols.back().back()->isSpace()) cols.back().pop_back();
    return cols;
  };
  float total = 0.f, tallest = 0.f;
  for (const auto& leaf : leaves) {
    total += leaf->_height + leaf->_depth;
    tallest = std::max(tallest, leaf->_height + leaf->_depth);
  }
  // The least height the lines fit `n` columns in.
  float low = tallest, high = std::max(total, tallest);
  for (int i = 0; i < 40 && high - low > 0.01f; i++) {
    const float mid = (low + high) / 2;
    if (static_cast<int>(fill(mid).size()) <= n) {
      high = mid;
    } else {
      low = mid;
    }
  }
  // A quarter of a line over: lines differ by their own height and depth (a digit's, a descender's), where TeX's
  // columns are made of lines that take the same space each, so a column is not a line short for 11 units of depth.
  std::vector<Column> cols = fill(high + 0.25f * env.baselineSkip());
  // multicol sets every column to the height of the tallest, stretching the glue between a short column's paragraphs
  // evenly: pdflatex's rows of a column a line short are 18 apart where the others' are 12 (a column two lines short,
  // the last of ten lines in three, is left as it is).
  // Columns start on the same baseline, so a column ends where its last baseline is: its extent less the first line's
  // height above the baseline and the last line's depth below.
  const auto heightOf = [](const Column& column) {
    if (column.empty()) return 0.f;
    float sum = 0.f;
    for (const auto& leaf : column) sum += leaf->_height + leaf->_depth;
    return sum - column.front()->_height - column.back()->_depth;
  };
  float target = 0.f;
  for (const auto& column : cols) target = std::max(target, heightOf(column));
  for (auto& column : cols) {
    const float shortBy = target - heightOf(column);
    if (shortBy < 0.01f || shortBy > 1.3f * env.baselineSkip()) continue;
    std::vector<std::size_t> stretchy;
    for (std::size_t k = 0; k < column.size(); k++) {
      if (gaps.count(column[k].get()) > 0) stretchy.push_back(k);
    }
    for (const std::size_t k : stretchy) {
      column[k] = sptrOf<StrutBox>(0.f, column[k]->_height + shortBy / static_cast<float>(stretchy.size()), 0.f, 0.f);
    }
  }
  auto row = sptrOf<HBox>();
  // The last line of the tallest column ends the columns.
  float lastDepth = 0.f, tallestColumn = -1.f;
  for (const auto& column : cols) {
    float sum = 0.f;
    for (const auto& leaf : column) sum += leaf->_height + leaf->_depth;
    if (sum > tallestColumn && !column.empty()) {
      tallestColumn = sum;
      lastDepth = column.back()->_depth;
    }
  }
  for (int i = 0; i < n; i++) {
    if (i > 0) row->add(sptrOf<StrutBox>(sep, 0.f, 0.f, 0.f));
    auto column = sptrOf<VBox>();
    if (i < static_cast<int>(cols.size())) {
      for (const auto& leaf : cols[static_cast<std::size_t>(i)]) column->add(leaf);
    }
    row->add(column->_children.empty() ? sptr<Box>(sptrOf<StrutBox>(colWidth, 0.f, 0.f, 0.f))
                                       : sptr<Box>(sptrOf<HBox>(column, colWidth, Alignment::left)));
  }
  auto whole = sptrOf<ColumnsBox>();
  whole->_lastDepth = lastDepth;
  if (_header != nullptr) {
    const sptr<Box> header = setBox(_header, measure);
    whole->add(header);
    // \multicolsep, and the baselineskip glue TeX adds after it as after any skip.
    whole->add(sptrOf<StrutBox>(0.f, env.interline(header->_depth, row->_height) + 1.2f * env.em(), 0.f, 0.f));
  }
  whole->add(row);
  return whole;
}

sptr<Box> HangingAtom::createBox(Env& env) {
  auto whole = _whole->createBox(env);
  const float width = env.textWidth();
  if (width == POS_INF || whole->_width <= width) return whole;
  auto lead = _lead->createBox(env);
  const float rest = width - lead->_width;
  if (rest <= 0) return whole;
  const auto [split, lines] = BoxSplitter::split(_rest->createBox(env), rest, env.baselineSkip());
  if (!split) return whole;
  // The title's lines are a column whose baseline is their first line.
  auto row = sptrOf<HBox>();
  row->add(lead);
  row->add(lines);
  return row;
}
