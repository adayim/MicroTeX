#include "atom/atom_misc.h"

#include "atom/atom_basic.h"
#include "atom/atom_delim.h"
#include "env/units.h"
#include "front/lower.h"
#include "utils/string_utils.h"
#include "utils/utf.h"

using namespace std;
using namespace microtex;

sptr<Box> BigSymbolAtom::createBox(Env& env) {
  // \big, \Big, \bigg and \Bigg are 1.2, 1.8, 2.4 and 3 times the size, as TeX sets
  // them: the first variant of the delimiter that is so tall, else its assembly.
  static const float kTimes[] = {1.f, 1.2f, 1.8f, 2.4f, 3.f};
  const float times = kTimes[std::min(std::max(_size, 0), 4)];
  auto b = microtex::createVDelim(_delim->name(), env, times * Units::fsize(UnitType::em, 1.f, env), false);
  const auto axis = env.mathConsts().axisHeight() * env.scale();
  b->_shift = -(b->vlen() / 2 - b->_height) - axis;
  return sptrOf<HBox>(b);
}

LapedAtom::LapedAtom(const sptr<Atom>& a, char type) : _type(type) {
	_at = a == nullptr ? sptrOf<EmptyAtom>() : a;
}

sptr<Box> LapedAtom::createBox(Env& env) {
  auto b = _at->createBox(env);
  auto* vb = new VBox();
  vb->add(b);
  vb->_width = 0;
  switch (_type) {
    case 'l': b->_shift = -b->_width; break;
    case 'r': b->_shift = 0; break;
    default: b->_shift = -b->_width / 2; break;
  }

  return sptr<Box>(vb);
}

sptr<Box> RaiseAtom::createBox(Env& env) {
  auto base = _base->createBox(env);
  if (_raise.isValid()) {
    base->_shift = Units::fsize(_raise, env);
  }
  auto hb = sptrOf<HBox>(base);
  if (_height.isValid()) {
    hb->_height = Units::fsize(_height, env);
  }
  if (_depth.isValid()) {
    hb->_depth = Units::fsize(_depth, env);
  }
  return hb;
}

sptr<Box> ResizeAtom::createBox(Env& env) {
  auto box = _base->createBox(env);
  // Only if it is wider than the line, which there may be none of.
  if (_atMost && (env.textWidth() == POS_INF || box->_width <= Units::fsize(_width, env))) {
    return box;
  }
  // A size the box does not have cannot be scaled to: it is ignored, as
  // `!` would be.
  const bool w = _width.isValid() && box->_width > 0;
  const bool h = _height.isValid() && box->vlen() > 0;
  if (!w && !h) return box;
  auto sx = 1.f, sy = 1.f;
  if (w && h) {
    sx = Units::fsize(_width, env) / box->_width;
    sy = Units::fsize(_height, env) / box->vlen();
    if (_keepAspectRatio) {
      sx = std::min(sx, sy);
      sy = sx;
    }
  } else if (w) {
    sx = Units::fsize(_width, env) / box->_width;
    sy = sx;
  } else {
    sy = Units::fsize(_height, env) / box->vlen();
    sx = sy;
  }
  return sptrOf<ScaleBox>(box, sx, sy);
}

RotateAtom::RotateAtom(const sptr<Atom>& base, float angle, const string& option)
    : _angle(0), _option(Rotation::bl) {
  _base = base == nullptr ? sptrOf<EmptyAtom>() : base;
  _type = _base->_type;
  _angle = angle;
  const auto& opt = parseOption(option);
  auto it = opt.find("origin");
  if (it != opt.end()) {
    _option = RotateBox::getOrigin(it->second);
    return;
  }
  it = opt.find("x");
  if (it != opt.end()) {
    _x = Units::getDimen(it->second);
  } else {
    _x = 0._em;
  }
  it = opt.find("y");
  if (it != opt.end()) {
    _y = Units::getDimen(it->second);
  } else {
    _y = 0._em;
  }
}

RotateAtom::RotateAtom(const sptr<Atom>& base, const string& angle, const string& option)
    : _angle(0), _option(Rotation::none), _x(0._em), _y(0._em) {
  _type = base->_type;
  _base = base;
  valueOf(angle, _angle);
  _option = RotateBox::getOrigin(option);
}

sptr<Box> RotateAtom::createBox(Env& env) {
  if (_option != Rotation::none) {
    return sptrOf<RotateBox>(_base->createBox(env), _angle, _option);
  }

  const auto x = Units::fsize(_x, env);
  const auto y = Units::fsize(_y, env);
  return sptrOf<RotateBox>(_base->createBox(env), _angle, x, y);
}

sptr<Box> RuleAtom::createBox(Env& env) {
  float w = Units::fsize(_w, env);
  float h = Units::fsize(_h, env);
  float r = Units::fsize(_r, env);
  return sptrOf<RuleBox>(h, w, r);
}

sptr<Box> StrikeThroughAtom::createBox(Env& env) {
  const auto t = env.mathConsts().overbarRuleThickness() * env.scale();
  const auto h = env.mathConsts().axisHeight() * env.scale();
  auto b = _at->createBox(env);
  auto r = sptrOf<RuleBox>(t, b->_width, -h + t);
  auto hb = sptrOf<HBox>(b);
  hb->add(StrutBox::create(-b->_width));
  hb->add(r);
  return hb;
}

sptr<Box> RuleDecorAtom::createBox(Env& env) {
  const auto b = (_base == nullptr ? sptrOf<EmptyAtom>() : _base)->createBox(env);
  // One height for the whole run, so a broken rule is straight: under
  // the run's depth, where \underline puts its rule, or through the middle
  // of lowercase letters, where \cancel's horizontal stroke goes.
  float raise, thickness;
  if (_under) {
    thickness = env.mathConsts().underbarRuleThickness() * env.scale();
    const float gap = env.mathConsts().underbarVerticalGap() * env.scale();
    raise = -(b->_depth + gap + thickness / 2);
  } else {
    thickness = env.mathConsts().fractionRuleThickness() * env.scale();
    raise = env.xHeight() / 2;
  }
  auto box = sptrOf<RuleDecorBox>(b, raise, thickness);
  box->_openable = true;
  return box;
}

sptr<Box> VCenterAtom::createBox(Env& env) {
  auto b = _base->createBox(env);
  const auto a = env.mathConsts().axisHeight() * env.scale();
  // The middle of the box on the axis: half of it, and the axis, above the
  // baseline.
  if (!_moveInk) {
    auto hb = sptrOf<HBox>(b);
    hb->_height = b->vlen() / 2 + a;
    hb->_depth = b->vlen() - hb->_height;
    return hb;
  }
  // The box is moved down to be there, and the row follows it.
  b->_shift = b->_height - (b->vlen() / 2 + a);
  auto hb = sptrOf<HBox>();
  hb->add(b);
  return hb;
}

sptr<Box> MathChoiceAtom::createBox(Env& env) {
  const TexStyle style = env.style();
  const int k = style <= TexStyle::display1 ? 0 : style <= TexStyle::text1 ? 1 : style <= TexStyle::script1 ? 2 : 3;
  return _choice[k] == nullptr ? sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f) : _choice[k]->createBox(env);
}

LongDivAtom::LongDivAtom(long divisor, long dividend) : _divisor(divisor), _dividend(dividend) {
  _halign = Alignment::right;
  setAlignTop(true);
  vector<string> results;
  calculate(results);

  auto rule = sptrOf<RuleAtom>(0._ex, 2.5_ex, 0.3_ex);

  const int s = results.size();
  for (int i = 0; i < s; i++) {
    auto num = front::buildFragment(results[i]);
    if (i == 1) {
      string divisor = toString(_divisor);
      auto rparen = SymbolAtom::get("rparen");
      auto big = sptrOf<BigSymbolAtom>(rparen, 1);
      auto raise = sptrOf<RaiseAtom>(big, -1._tt, 0._tt, 0._tt);
      auto row = sptrOf<RowAtom>(raise);
      row->add(num);
      auto o = sptrOf<OverUnderBar>(row, true);
      auto r = sptrOf<RowAtom>(front::buildFragment(divisor));
      r->add(sptrOf<SpaceAtom>(SpaceType::thinMuSkip));
      r->add(o);
      append(r);
      continue;
    }
    if (i % 2 == 0) {
      auto row = sptrOf<RowAtom>(num);
      row->add(rule);
      if (i == 0)
        append(row);
      else
        append(sptrOf<OverUnderBar>(row, false));
    } else {
      auto row = sptrOf<RowAtom>(num);
      row->add(rule);
      append(row);
    }
  }
}

void LongDivAtom::calculate(vector<string>& results) const {
  long quotient = _dividend / _divisor;
  results.push_back(toString(quotient));
  string x = toString(quotient);
  size_t len = x.length();
  long remaining = _dividend;
  results.push_back(toString(remaining));
  for (size_t i = 0; i < len; i++) {
    long b = (x[i] - '0') * pow(10, len - i - 1);
    long product = b * _divisor;
    remaining = remaining - product;
    results.push_back(toString(product));
    results.push_back(toString(remaining));
  }
}

namespace {

/** The extent of the line cancel.sty's \cancel draws over a box `w` wide and `h` high and deep (`pt` is a point): it is
 *  a picture-mode \line, which has only a few slopes, 2pt longer than the box and as tall as its slope makes it,
 *  and set in a box at least 2pt wide and 6pt high. A flat box (wide) gets a line whose slope goes up with the box's
 *  height; a tall one a line that is as tall as the box and 2pt, and as wide as the slope makes it. */
void cancelSlash(float w, float h, float pt, float& x, float& y) {
  float width = std::max(w, 2.f * pt), total = std::max(h, 6.f * pt);
  if (total < width) {
    width = std::max(width, 8.f * pt);
    const int k = std::min(4, static_cast<int>(5.f * total / width));
    static const float slope[] = {1.f / 6.f, 1.f / 4.f, 1.f / 2.f, 3.f / 4.f, 1.f};  // (6,1) (4,1) (2,1) (4,3) (1,1)
    x = width + 2.f * pt;
    y = x * slope[k];
  } else {
    total = std::max(total, 8.f * pt) + 2.f * pt;
    const int k = std::min(4, static_cast<int>(5.f * width / total));
    static const float span[] = {.16f, .25f, .5f, .75f, 1.f};
    static const float slope[] = {6.f, 4.f, 2.f, 4.f / 3.f, 1.f};  // (1,6) (1,4) (1,2) (3,4) (1,1)
    x = span[k] * total;
    y = x * slope[k];
  }
}

}  // namespace

sptr<Box> CancelAtom::createBox(Env& env) {
  auto box = _base->createBox(env);
  vector<float> lines;
  const float rt = env.mathConsts().fractionRuleThickness() * env.scale();
  // How much taller the lines make the box, and their own thickness.
  float up = 0.f, down = 0.f, thickness = rt;
  // y runs down from the top of the box: a slash, as the cancel package's
  // \cancel draws it, goes from the bottom left to the top right.
  if (_cancelType == SLASH || _cancelType == BACKSLASH || _cancelType == CROSS) {
    const float pt = Units::fsize(Units::getDimen("1pt"), env);
    const float w = box->_width, h = box->_height + box->_depth;
    float x = 0.f, y = 0.f;
    cancelSlash(w, h, pt, x, y);
    // Centred on the box, as cancel.sty's \ooalign has it, and thin lines of 0.4pt.
    const float cx = w / 2.f, cy = h / 2.f;
    if (_cancelType != BACKSLASH) lines.insert(lines.end(), {cx - x / 2.f, cy + y / 2.f, cx + x / 2.f, cy - y / 2.f});
    if (_cancelType != SLASH) lines.insert(lines.end(), {cx - x / 2.f, cy - y / 2.f, cx + x / 2.f, cy + y / 2.f});
    // The lines hang over the box and do not widen it (pdfTeX: \cancel{a} is as wide as a); they make it taller.
    up = std::max(0.f, (y - h) / 2.f);
    down = up;
    thickness = 0.4f * pt;
  } else if (_cancelType == ARROW) {
    // \cancelto: the slash, ending in an arrow head at the top right.
    const float w = box->_width;
    const float h = box->_height + box->_depth;
    lines = {0, h, w, 0};
    const float len = std::sqrt(w * w + h * h);
    if (len > 0) {
      const float head = std::min(len, std::max(0.25f * len, 4 * rt));
      const float bx = -w / len, by = h / len;  // back along the shaft
      const float theta = 0.45f;
      for (const float sign : {1.f, -1.f}) {
        const float c = std::cos(theta), s = sign * std::sin(theta);
        lines.insert(
          lines.end(), {w, 0.f, w + head * (bx * c - by * s), head * (bx * s + by * c)});
      }
    }
  } else if (_cancelType == HORIZONTAL) {
    // Strike through the visible middle of lowercase letters: half of
    // x-height above the baseline. Box coords run y=0 at the top with y
    // increasing downward, so y_line = height - xHeight/2.
    const float y = box->_height - env.xHeight() / 2;
    lines = {0, y, box->_width, y};
  } else {
    return box;
  }

  auto overlap = sptrOf<LineBox>(lines, thickness);
  overlap->_width = box->_width;
  overlap->_height = box->_height + up;
  overlap->_depth = box->_depth + down;
  auto hbox = new HBox(box);
  hbox->add(sptr<Box>(new StrutBox(-box->_width, 0, 0, 0)));
  hbox->add(overlap);
  return sptr<Box>(hbox);
}
