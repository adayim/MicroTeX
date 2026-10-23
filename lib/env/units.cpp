#include "env/units.h"

#include <cstring>

#include "utils/string_utils.h"
#include "utils/utf.h"

using namespace microtex;
using namespace std;

namespace {

// IMPORTANT: sorted by the unit name, you must re-sort this array after add item
const pair<const char*, UnitType> _units[]{
  {"bp",    UnitType::point},
  {"cc",    UnitType::cc   },
  {"cm",    UnitType::cm   },
  {"dd",    UnitType::dd   },
  {"em",    UnitType::em   },
  {"ex",    UnitType::ex   },
  {"in",    UnitType::in   },
  {"mm",    UnitType::mm   },
  {"mu",    UnitType::mu   },
  {"pc",    UnitType::pica },
  {"pica",  UnitType::pica },
  {"pix",   UnitType::pixel},
  {"pixel", UnitType::pixel},
  {"pt",    UnitType::pt   },
  {"px",    UnitType::pixel},
  {"sp",    UnitType::sp   },
};

const u32 _unitsCount = sizeof(_units) / sizeof(pair<const char*, UnitType>);

/**
 * Helper function to get the size of 1 point (big-point) corresponds to
 * the font design unit. It is the device's point: the host's `px` and the
 * width and line space it hands in are counted in it, and stay the same
 * whatever the size of the text.
 */
float pt(const Env& env) {
  return Env::pixelsPerPoint() * env.upem() / env.ppem() * env.fixedScale();
}

/**
 * 1 TeX point (1/72.27 in) in the same units. A grob is a LaTeX page set at
 * 10pt and magnified to the size of its text, so a length written in a TeX
 * unit (pt, bp, pc, cm, mm, in, dd, cc, sp) follows that size: a point is a
 * tenth of the em of the grob, as it is of a 10pt page. It is the grob's em
 * and not the style's: a centimetre in a subscript is still a centimetre.
 * (pt() times the size of the text over 10, written so that the size
 * cancels: the layout is made at FIXED_TEXT_SIZE.)
 */
float texPt(const Env& env) {
  return Env::fixedTextSize() / 10.f * Env::pixelsPerPoint() * env.upem() / env.ppem();
}

// IMPORTANT: the order corresponds to the order of the enum UnitType
const function<float(const Env&)> _unitConversions[]{
  // em
  [](const Env& env) -> float { return env.em(); },
  // ex
  [](const Env& env) -> float { return env.xHeight(); },
  // pixel
  [](const Env& env) -> float { return pt(env) / Env::pixelsPerPoint(); },
  // point: TeX's big point, 72.27/72 points
  [](const Env& env) -> float { return 1.00375f * texPt(env); },
  // 1 pica = 12 point
  [](const Env& env) -> float { return 12.f * texPt(env); },
  // 1 mu = 1/18 em
  [](const Env& env) -> float { return env.em() / 18.f; },
  // 1 cm = 72.27/2.54 points
  [](const Env& env) -> float { return 28.452756f * texPt(env); },
  // 1 mm = 1/10 cm
  [](const Env& env) -> float { return 2.8452756f * texPt(env); },
  // 1 in = 72.27 points
  [](const Env& env) -> float { return 72.27f * texPt(env); },
  // 1 sp = 1/65536 point (TeX: a point is 65536 sp)
  [](const Env& env) -> float { return texPt(env) / 65536.f; },
  // 1 pt
  [](const Env& env) -> float { return texPt(env); },
  // 1 dd = 1238/1157 points
  [](const Env& env) -> float { return 1.0700086f * texPt(env); },
  // 1 cc = 12 dd
  [](const Env& env) -> float { return 12.840104f * texPt(env); },
  // 1 tt = rule thickness
  [](const Env& env) -> float { return env.ruleThickness() * env.scale(); },
  // 1 tw = the text width. Without one there is no page to measure, so it
  // is what LaTeX's article class sets \textwidth to: 345pt.
  [](const Env& env) -> float {
    const float w = env.textWidth();
    return w != POS_INF ? w : 345.f * texPt(env);
  },
};

Units::LengthResolver lengthResolver = nullptr;

}  // namespace

void Units::setLengthResolver(LengthResolver resolver) {
  lengthResolver = resolver;
}

float Units::fsize(UnitType unit, float size, const Env& env) {
  if (unit == UnitType::none) return size;
  return _unitConversions[static_cast<i8>(unit)](env) * size;
}

float Units::fsize(const Dimen& dimen, const Env& env) {
  return Units::fsize(dimen.unit, dimen.val, env);
}

UnitType Units::getUnit(const std::string& unit) {
  const auto i =
    binIndexOf(_unitsCount, [&](int i) { return strcmp(unit.c_str(), _units[i].first); });
  if (i < 0) return UnitType::pixel;
  return _units[i].second;
}

Dimen Units::getDimen(const std::string& lgth) {
  if (lgth.empty()) return {0.f, UnitType::none};

  // TeX's factor times a length: `0.5\textwidth`, or `\linewidth` alone,
  // which is one of it; and the same of a length the input names
  // (`2\tabcolsep`).
  const size_t bs = lgth.find('\\');
  if (bs != string::npos) {
    string name = lgth.substr(bs + 1);
    trim(name);
    string num = lgth.substr(0, bs);
    trim(num);
    float f = 1.f;
    if (num == "-") {
      f = -1.f;
    } else if (!num.empty() && num != "+") {
      valueOf(num, f);
    }
    if (name == "textwidth" || name == "linewidth" || name == "columnwidth") return {f, UnitType::tw};
    Dimen named;
    if (lengthResolver != nullptr && lengthResolver(name, named)) return {f * named.val, named.unit};
  }

  size_t i = 0;
  for (; i < lgth.length() && !isAlpha(lgth[i]); i++)
    ;
  float f = 0;
  valueOf(lgth.substr(0, i), f);

  UnitType unit = UnitType::none;
  string str = lgth.substr(i);
  string x = trim(str);
  toLower(x);
  if (i != lgth.size()) unit = getUnit(x);

  return {f, unit};
}
