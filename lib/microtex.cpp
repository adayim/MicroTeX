#include "microtex.h"

#include "core/formula.h"
#include "otf/fontsense.h"
#include "macro/macro.h"
#include "utils/exceptions.h"
#include "utils/string_utils.h"
#include "render/builder.h"
#include "atom/atom_row.h"
#include "utils/bidi.h"

#include <clocale>

using namespace std;
using namespace microtex;

namespace microtex {

struct Config {
  bool isInited;
  bool isPrivilegedEnvironment;
  std::string defaultMainFontFamily;
  std::string defaultMathFontName;
  bool renderGlyphUsePath;
  bool enableOverrideTeXStyle;
  TexStyle overrideTeXStyle;
};

static Config MICROTEX_CONFIG{false, false, "", "", false, false, TexStyle::text};

} // namespace microtex

Config* MicroTeX::_config = &microtex::MICROTEX_CONFIG;

std::string MicroTeX::version() {
  static const auto ver = std::to_string(MICROTEX_VERSION_MAJOR) + "."
     + std::to_string(MICROTEX_VERSION_MINOR) + "."
     + std::to_string(MICROTEX_VERSION_PATCH);
  return ver;
}

#ifdef HAVE_AUTO_FONT_FIND

struct InitVisitor {

  FontMeta operator()(const FontSrc* src) {
    auto meta = FontContext::addFont(*src);
    if (!meta.isMathFont) {
      throw ex_invalid_param("'" + meta.name + "' is not a math font!");
    }
    return meta;
  }

  FontMeta operator()(const string& name) {
    fontsenseLookup();
    if (!FontContext::isMathFontExists(name)) {
      throw ex_invalid_param("Math font '" + name + "' does not exists!");
    }
    return FontContext::mathFontMetaOf(name);
  }

  FontMeta operator()(const InitFontSenseAuto& sense) {
    auto mathFont = fontsenseLookup();
    if (!mathFont.has_value()) {
      throw ex_invalid_param("No math font found by font-sense.");
    }
    return mathFont.value();
  }
};

FontMeta MicroTeX::init(const Init& init) {
  if (_config->isInited) return {};
  std::setlocale(LC_NUMERIC, "C"); // workaround for decimal parsing on German (decimal comma) systems
  auto meta = std::visit(InitVisitor(), init);
  _config->defaultMathFontName = meta.name;
  _config->isInited = true;
  _config->isPrivilegedEnvironment = false;
  NewCommandMacro::_init_();
  return meta;
}

#endif // HAVE_AUTO_FONT_FIND

FontMeta MicroTeX::init(const FontSrc& mathFontSrc) {
  if (_config->isInited) return {};
  std::setlocale(LC_NUMERIC, "C"); // workaround for decimal parsing on German (decimal comma) systems
  auto meta = FontContext::addFont(mathFontSrc);
  if (!meta.isMathFont) {
    throw ex_invalid_param("'" + meta.name + "' is not a math font!");
  }
  _config->defaultMathFontName = meta.name;
  _config->isInited = true;
  _config->isPrivilegedEnvironment = false;
  NewCommandMacro::_init_();
  return meta;
}

bool MicroTeX::isInited() {
  return _config->isInited;
}

void MicroTeX::release() {
  MacroInfo::_free_();
  NewCommandMacro::_free_();
}

bool MicroTeX::isPrivilegedEnvironment() {
	return _config->isPrivilegedEnvironment;
}

void MicroTeX::setPrivilegedEnvironment(bool privileged) {
	_config->isPrivilegedEnvironment = privileged;
}

FontMeta MicroTeX::addFont(const FontSrc& src) {
  auto meta = FontContext::addFont(src);
  if (meta.isMathFont && _config->defaultMathFontName.empty()) {
    _config->defaultMathFontName = meta.name;
  }
  if (!meta.isMathFont && _config->defaultMainFontFamily.empty()) {
    _config->defaultMainFontFamily = meta.family;
  }
  return meta;
}

bool MicroTeX::setDefaultMathFont(const std::string& name) {
  if (!FontContext::isMathFontExists(name)) return false;
  _config->defaultMathFontName = name;
  return true;
}

bool MicroTeX::setDefaultMainFont(const std::string& family) {
  if (family.empty() || FontContext::isMainFontExists(family)) {
    _config->defaultMainFontFamily = family;
    return true;
  }
  return false;
}

std::vector<std::string> MicroTeX::mathFontNames() {
  return FontContext::mathFontNames();
}

std::vector<std::string> MicroTeX::mainFontFamilies() {
  return FontContext::mainFontFamilies();
}

void MicroTeX::overrideTexStyle(bool enable, TexStyle style) {
  _config->enableOverrideTeXStyle = enable;
  _config->overrideTeXStyle = style;
}

bool MicroTeX::hasGlyphPathRender() {
#ifdef HAVE_GLYPH_RENDER_PATH
  return true;
#else
  return false;
#endif
}

void MicroTeX::setRenderGlyphUsePath(bool use) {
#if GLYPH_RENDER_TYPE == GLYPH_RENDER_TYPE_BOTH
  _config->renderGlyphUsePath = use;
#endif
}

bool MicroTeX::isRenderGlyphUsePath() {
#if GLYPH_RENDER_TYPE == GLYPH_RENDER_TYPE_BOTH
  return _config->renderGlyphUsePath;
#elif GLYPH_RENDER_TYPE == GLYPH_RENDER_TYPE_PATH
  return true;
#else
  return false;
#endif
}

Render* MicroTeX::parse(
  const string& latex, float width, float textSize, float lineSpace, color fg,
  bool fillWidth, const OverrideTeXStyle& overrideTeXStyle,
  const string& mathFontName, const string& mainFontFamily
) {
  Formula formula(latex);
  // Bidirectional levels, resolved once over the whole formula while the
  // atoms still hold their text -- a box keeps only an opaque layout. It
  // has to be one pass over the whole tree, not one per row: a group's
  // direction follows the paragraph it sits in, not its own contents.
  //
  // Independent of _mergeText, which is a different question. Merging
  // decides how much text goes into one run; levelling decides what order
  // the runs sit in. Gating this on !_mergeText meant no reordering at
  // all whenever max_width was 0 -- the default -- so a right-to-left
  // label came out in logical order the moment it held a second child
  // (any emphasis, colour or font change). The merged run is still handed
  // to the backend whole and still ordered internally by it; this orders
  // the runs around it.
  RowAtom::_levelled = microtex::bidi_assign_levels(formula._root);
  const auto isInline = !startsWith(latex, "$$") && !startsWith(latex, "\\[");
  const auto align = isInline ? Alignment::left : Alignment::center;
  TexStyle style = isInline ? TexStyle::text : TexStyle::display;
  if (overrideTeXStyle.enable) {
    style = overrideTeXStyle.style;
  } else if (_config->enableOverrideTeXStyle) {
    style = _config->overrideTeXStyle;
  }
  Render* render = RenderBuilder()
    .setStyle(style)
    .setTextSize(textSize)
    .setMathFontName(
      mathFontName.empty()
      ? _config->defaultMathFontName
      : mathFontName
    )
    .setMainFontName(
      mainFontFamily.empty()
      ? _config->defaultMainFontFamily
      : mainFontFamily
    )
    .setWidth({width, UnitType::pixel}, align)
    .setFillWidth(!isInline && fillWidth)
    .setLineSpace({lineSpace, UnitType::pixel})
    .setForeground(fg)
    .build(formula);
  return render;
}
