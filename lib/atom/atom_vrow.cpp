#include "atom/atom_vrow.h"

#include "atom/atom_row.h"
#include "box/box_group.h"
#include "box/box_single.h"
#include "core/split.h"
#include "env/env.h"

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

sptr<Box> VRowAtom::createBox(Env& env) {
  auto vb = sptrOf<VBox>();
  auto lineSpace = sptrOf<StrutBox>(0.f, env.lineSpace(), 0.f, 0.f);
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
      if (_addInterline && i < size - 1) vb->add(lineSpace);
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
    for (size_t i = 0; i < size; i++) {
      // A display with no text width to centre in centres on the widest line.
      if (env.textWidth() == POS_INF && dynamic_cast<DisplayAtom*>(_elements[i].get()) != nullptr) {
        boxes[i] = sptrOf<HBox>(boxes[i], widest, Alignment::center);
      }
      vb->add(boxes[i]);
      if (i < size - 1) gapAfter(i);
      if (_addInterline && i < size - 1) vb->add(lineSpace);
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
  auto box = _base->createBox(env);
  const float width = env.textWidth();
  if (width == POS_INF) return box;
  if (box->_width <= width) return sptrOf<HBox>(box, width, Alignment::center);
  // Too wide for the line: broken as any line is, and each line it makes
  // centred -- not justified, as LaTeX's \centering leaves them.
  const bool justify = BoxSplitter::_justify;
  BoxSplitter::_justify = false;
  const auto [split, lines] = BoxSplitter::split(box, width, env.lineSpace());
  BoxSplitter::_justify = justify;
  const auto vb = std::dynamic_pointer_cast<VBox>(lines);
  if (!split || vb == nullptr) return box;
  auto out = sptrOf<VBox>();
  for (const auto& line : vb->_children) {
    if (std::dynamic_pointer_cast<HBox>(line) != nullptr) {
      out->add(sptrOf<HBox>(line, width, Alignment::center));
    } else {
      out->add(line);  // the space between lines
    }
  }
  return out;
}

sptr<Box> MinipageAtom::createBox(Env& env) {
  if (_body == nullptr) return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
  const float width = Units::fsize(_width, env);
  const bool measured = width > 0 && width != POS_INF;
  sptr<Box> box;
  {
    // The breaker needs word-level runs, as a p{} cell does: folded into
    // one phrase a paragraph could not be broken.
    const bool merge = RowAtom::_mergeText;
    RowAtom::_mergeText = false;
    try {
      box = env.withTextWidth(measured ? width : env.textWidth(),
                              [&](Env& e) { return _body->createBox(e); });
    } catch (...) {
      RowAtom::_mergeText = merge;
      throw;
    }
    RowAtom::_mergeText = merge;
  }
  if (measured) box = BoxSplitter::split(box, width, env.lineSpace()).second;

  // A column of lines, whose baseline can be put where the position says.
  auto vb = std::dynamic_pointer_cast<VBox>(box);
  if (vb == nullptr || vb->_children.empty()) {
    vb = sptrOf<VBox>();
    vb->add(box);
  }
  // Measured from its top: the body's own height, and where its first and
  // last baselines are in it.
  const float body = vb->_height + vb->_depth;
  const float first = vb->_children.front()->_height;
  const float last = body - vb->_children.back()->_depth;
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

sptr<Box> HangingAtom::createBox(Env& env) {
  auto whole = _whole->createBox(env);
  const float width = env.textWidth();
  if (width == POS_INF || whole->_width <= width) return whole;
  auto lead = _lead->createBox(env);
  const float rest = width - lead->_width;
  if (rest <= 0) return whole;
  const auto [split, lines] = BoxSplitter::split(_rest->createBox(env), rest, env.lineSpace());
  if (!split) return whole;
  // The title's lines are a column whose baseline is their first line.
  auto row = sptrOf<HBox>();
  row->add(lead);
  row->add(lines);
  return row;
}
