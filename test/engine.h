// The whole engine as an embedder runs it: LaTeX in, draw records and the
// problems found out. The tests that need a layout (two spellings that must
// draw alike, a number at the right margin, a warning at its line:col) use
// `render()`, which starts the engine on first use, as engine_fuzz does.
//
// Text is measured by a stub (a run is as wide as its bytes), so a layout's
// numbers say nothing about any real font: compare layouts with each other
// (`sameInk()`), or look at what is drawn, not how wide. gridmicrotex tests
// the numbers a real text measurer gives, in R.

#ifndef MICROTEX_TEST_ENGINE_H
#define MICROTEX_TEST_ENGINE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <utility>
#include <string>
#include <vector>

#include "atom/atom_row.h"
#include "atom/font_family_atom.h"
#include "atom/image_atom.h"
#include "atom/mark_atom.h"
#include "check.h"
#include "core/split.h"
#include "front/front.h"
#include "graphic/graphic.h"
#include "graphic/graphic_recorder.h"
#include "microtex.h"
#include "render/render.h"
#include "unimath/font_src.h"

#ifndef MICROTEX_TEST_OTF
#error "MICROTEX_TEST_OTF must name the math font file the engine starts from"
#endif

namespace check {

namespace detail {

// A text run as the package's fallback measures one when R gives it no
// measurer: 0.55 em a character. Drawing it is a text record, as R's.
class StubTextLayout : public microtex::TextLayout {
public:
  StubTextLayout(std::string src, microtex::FontStyle style, float size)
      : _src(std::move(src)), _style(style), _size(size) {}

  void getBounds(microtex::Rect& bounds) override {
    std::size_t characters = 0;
    for (const unsigned char c : _src) characters += (c & 0xC0) != 0x80 ? 1 : 0;
    bounds.x = 0;
    bounds.y = -_size * 0.8f;
    bounds.w = _size * 0.55f * static_cast<float>(characters);
    bounds.h = _size;
  }

  void draw(microtex::Graphics2D& g2, float x, float y) override {
    if (auto* recorder = dynamic_cast<microtex::Graphics2D_Recorder*>(&g2)) {
      recorder->drawTextRun(_src, x, y, static_cast<int>(_style), _size);
    }
  }

private:
  std::string _src;
  microtex::FontStyle _style;
  float _size;
};

class StubPlatform : public microtex::PlatformFactory {
public:
  microtex::sptr<microtex::Font> createFont(const std::string& file) override {
    return microtex::sptrOf<microtex::RecordedFont>(file);
  }
  microtex::sptr<microtex::TextLayout> createTextLayout(const std::string& src, microtex::FontStyle style,
                                                       float size) override {
    return microtex::sptrOf<StubTextLayout>(src, style, size);
  }
};

inline void startEngine() {
  static bool ready = false;
  if (ready) return;
  microtex::PlatformFactory::registerFactory("test", std::make_unique<StubPlatform>());
  microtex::PlatformFactory::activate("test");
  microtex::FontSrcOtf src(MICROTEX_TEST_OTF);
  microtex::MicroTeX::init(src);
  microtex::register_mark_macro();
  microtex::register_font_family_macro();
  microtex::register_image_macros();
  ready = true;
}

}  // namespace detail

struct Options {
  Options() = default;
  explicit Options(microtex::InputMode m, float width = 0) : mode(m), maxWidth(width) {}

  microtex::InputMode mode = microtex::InputMode::math;
  /** The measure, in px; 0 sets the input on one line. */
  float maxWidth = 0;
  /** `tex_style = "display"`: a formula is set as a display. */
  bool display = false;
  /** latex_options(packages = ): loaded before the input. */
  std::vector<std::string> packages;
  /** Where the equation count starts. */
  int equationStart = 0;
  /** Draw glyphs as outlines (path mode), not as characters. */
  bool path = true;
  /** justify = TRUE: lines filled to the measure; line_break = "optimal": total-fit breaking. */
  bool justify = false;
  bool optimalBreak = false;
  /** gp$fontsize: the size of the text, and of 1em. */
  float textSize = 20;
};

struct Layout {
  std::vector<microtex::DrawRecord> records;
  /** "line:col: message", in the order they were found. */
  Strings warnings;
  /** What the parse left in the numbering: the equations counted. */
  int equations = 0;
  int width = 0, height = 0, depth = 0;
  /** The layout was broken into lines at the measure. */
  bool split = false;
  float baseline = 0;
};

/** `tex` set as `opt` says. A parse that throws is an exception: the caller
 *  checks it (CHECK_THROWS) or the test fails. */
inline Layout render(const std::string& tex, const Options& opt = {}) {
  detail::startEngine();
  microtex::clear_font_families();
  microtex::MicroTeX::setRenderGlyphUsePath(opt.path);
  microtex::RowAtom::_mergeText = opt.maxWidth <= 0;
  // The paragraph modes and the numbering belong to one parse, as in R's
  // parse_latex_cpp: leave them as a test found them.
  struct Reset {
    ~Reset() {
      microtex::MicroTeX::setRenderGlyphUsePath(true);
      microtex::BoxSplitter::_justify = false;
      microtex::BoxSplitter::_optimalBreak = false;
      microtex::RowAtom::_mergeText = false;
      microtex::RowAtom::_levelled = false;
      microtex::front::setStartNumbering(microtex::front::NumberingState());
      microtex::front::setStartPackages({});
    }
  } reset;
  microtex::BoxSplitter::_justify = opt.justify;
  microtex::BoxSplitter::_optimalBreak = opt.optimalBreak;
  {
    microtex::front::NumberingState start;
    start.equation = opt.equationStart;
    microtex::front::setStartNumbering(std::move(start));
  }
  microtex::front::setStartPackages(opt.packages);

  std::unique_ptr<microtex::Render> render(microtex::MicroTeX::parse(
    tex, opt.maxWidth, opt.textSize, 10.0f, 0xFF000000u, true,
    opt.display ? microtex::OverrideTeXStyle{true, microtex::TexStyle::display}
                : microtex::OverrideTeXStyle{false, microtex::TexStyle::text},
    "", "", opt.mode));

  Layout out;
  for (const auto& d : microtex::front::lastDiagnostics().items()) {
    out.warnings.push_back(std::to_string(d.span.line) + ":" + std::to_string(d.span.col) + ": " +
                           d.message);
  }
  out.equations = microtex::front::lastNumbering().equation;
  if (!render) return out;
  out.width = render->getWidth();
  out.height = render->getHeight();
  out.depth = render->getDepth();
  out.split = render->isSplit();
  out.baseline = render->getBaseline();
  microtex::Graphics2D_Recorder recorder;
  render->draw(recorder, 0, 0);
  out.records = recorder.records();
  return out;
}

inline Layout render(const std::string& tex, microtex::InputMode mode) {
  Options opt;
  opt.mode = mode;
  return render(tex, opt);
}

namespace detail {

inline bool nearly(float a, float b) { return std::fabs(a - b) <= 1e-3f; }

inline bool samePath(const microtex::DrawRecord& a, const microtex::DrawRecord& b) {
  if (a.path_segments.size() != b.path_segments.size()) return false;
  for (std::size_t i = 0; i < a.path_segments.size(); i++) {
    if (a.path_segments[i].cmd != b.path_segments[i].cmd) return false;
    for (int k = 0; k < 6; k++) {
      if (!nearly(a.path_segments[i].coords[k], b.path_segments[i].coords[k])) return false;
    }
  }
  return true;
}

inline bool sameRecord(const microtex::DrawRecord& a, const microtex::DrawRecord& b) {
  return a.type == b.type && nearly(a.x, b.x) && nearly(a.y, b.y) && a.glyph_id == b.glyph_id &&
         nearly(a.font_size, b.font_size) && a.col == b.col && a.font_file == b.font_file &&
         a.text == b.text && a.font_style == b.font_style && a.font_family == b.font_family &&
         nearly(a.rotation, b.rotation) && a.mark_name == b.mark_name &&
         a.image_ref == b.image_ref && nearly(a.x1, b.x1) && nearly(a.y1, b.y1) &&
         nearly(a.x2, b.x2) && nearly(a.y2, b.y2) && nearly(a.width, b.width) &&
         nearly(a.height, b.height) && nearly(a.line_width, b.line_width) &&
         a.path_glyph_id == b.path_glyph_id && samePath(a, b);
}

}  // namespace detail

/** The same ink in the same places, and the same size. */
inline bool sameInk(const Layout& a, const Layout& b) {
  if (a.records.size() != b.records.size()) return false;
  if (a.width != b.width || a.height != b.height || a.depth != b.depth) return false;
  for (std::size_t i = 0; i < a.records.size(); i++) {
    if (!detail::sameRecord(a.records[i], b.records[i])) return false;
  }
  return true;
}

inline const char* typeName(microtex::DrawRecord::Type t) {
  using T = microtex::DrawRecord::Type;
  switch (t) {
    case T::GLYPH: return "glyph";
    case T::LINE: return "line";
    case T::RECT: return "rect";
    case T::FILL_RECT: return "fill_rect";
    case T::ROUND_RECT: return "round_rect";
    case T::FILL_ROUND_RECT: return "fill_round_rect";
    case T::PATH: return "path";
    case T::TEXT: return "text";
    case T::MARK: return "mark";
    case T::IMAGE: return "image";
    case T::STROKE_PATH: return "stroke_path";
  }
  return "?";
}

/** The records, one a line, for a failed check to show. */
inline std::string describe(const Layout& l) {
  std::string out = std::to_string(l.records.size()) + " records, " + std::to_string(l.width) + "x" +
                    std::to_string(l.height) + "+" + std::to_string(l.depth) + "\n";
  char buf[160];
  for (const auto& r : l.records) {
    const bool box = r.type == microtex::DrawRecord::LINE;
    std::snprintf(buf, sizeof buf, "  %-9s %8.2f %8.2f %s\n", typeName(r.type), box ? r.x1 : r.x,
                  box ? r.y1 : r.y, r.text.c_str());
    out += buf;
  }
  return out;
}

/** The text records, in drawing order. */
inline Strings texts(const Layout& l) {
  Strings out;
  for (const auto& r : l.records) {
    if (r.type == microtex::DrawRecord::TEXT) out.push_back(r.text);
  }
  return out;
}

using Records = std::vector<microtex::DrawRecord>;

/** A line starts at its x1, y1; its x, y are those, as in the data frame R makes. */
inline microtex::DrawRecord placed(microtex::DrawRecord r) {
  if (r.type == microtex::DrawRecord::LINE) {
    r.x = r.x1;
    r.y = r.y1;
  }
  return r;
}

/** The records of one type, in drawing order. */
inline Records of(const Layout& l, microtex::DrawRecord::Type type) {
  Records out;
  for (const auto& r : l.records) {
    if (r.type == type) out.push_back(placed(r));
  }
  return out;
}

/** All the records but the marks (a mark carries no ink). */
inline Records ink(const Layout& l) {
  Records out;
  for (const auto& r : l.records) {
    if (r.type != microtex::DrawRecord::MARK) out.push_back(placed(r));
  }
  return out;
}

/** The same kinds of ink in the same places, to `tolerance`, whatever glyphs
 *  they are: type, position, size and extent of each record. */
inline bool samePlaces(const Layout& a, const Layout& b, float tolerance = 1e-4f) {
  const Records ra = ink(a), rb = ink(b);
  if (ra.size() != rb.size()) return false;
  const auto close = [&](float p, float q) { return std::fabs(p - q) <= tolerance; };
  for (std::size_t i = 0; i < ra.size(); i++) {
    const auto &p = ra[i], &q = rb[i];
    if (p.type != q.type || !close(p.x, q.x) || !close(p.y, q.y) || !close(p.font_size, q.font_size) ||
        !close(p.width, q.width) || !close(p.height, q.height)) {
      return false;
    }
    if (p.type == microtex::DrawRecord::LINE && (!close(p.x2, q.x2) || !close(p.y2, q.y2))) return false;
  }
  return true;
}

/** One number of each record: `column(rs, &microtex::DrawRecord::y)`. */
template <class T>
std::vector<T> column(const Records& rs, T microtex::DrawRecord::*member) {
  std::vector<T> out;
  for (const auto& r : rs) out.push_back(r.*member);
  return out;
}

/** How many different values, to `digits` decimals: the rows of a column. */
inline std::size_t distinct(const std::vector<float>& values, int digits = 2) {
  std::set<long long> seen;
  const double scale = std::pow(10.0, digits);
  for (const float v : values) seen.insert(std::llround(static_cast<double>(v) * scale));
  return seen.size();
}

/** The values, to `digits` decimals, smallest first, each once. */
inline std::vector<double> uniqueSorted(const std::vector<float>& values, int digits = 2) {
  std::set<long long> seen;
  const double scale = std::pow(10.0, digits);
  for (const float v : values) seen.insert(std::llround(static_cast<double>(v) * scale));
  std::vector<double> out;
  for (const long long s : seen) out.push_back(static_cast<double>(s) / scale);
  return out;
}

inline float minimum(const std::vector<float>& v) { return *std::min_element(v.begin(), v.end()); }
inline float maximum(const std::vector<float>& v) { return *std::max_element(v.begin(), v.end()); }

/** How many records of one type. */
inline int count(const Layout& l, microtex::DrawRecord::Type type) {
  int n = 0;
  for (const auto& r : l.records) n += r.type == type ? 1 : 0;
  return n;
}

/** Pairs of inputs that must draw alike: the spelling, then what it stands for. */
using Pairs = std::vector<std::pair<std::string, std::string>>;

inline void checkPairs(const char* file, int line, const Pairs& pairs, const Options& opt) {
  for (const auto& p : pairs) {
    const auto a = render(p.first, opt);
    const auto b = render(p.second, opt);
    if (!sameInk(a, b)) {
      fail(file, line,
           "not the same ink:\n  " + p.first + "\n  " + p.second + "\n" + describe(a) + describe(b));
    }
  }
}

/** Something is drawn in `argb`: 0xFFFF0000 is the red of what could not be read. */
inline bool drawsColour(const Layout& l, unsigned argb) {
  for (const auto& r : l.records) {
    if (r.col == argb) return true;
  }
  return false;
}

constexpr unsigned kRed = 0xFFFF0000u;

/** The characters drawn as outlines (path mode), in drawing order. */
inline std::u32string pathCharacters(const Layout& l) {
  std::u32string out;
  for (const auto& r : l.records) {
    if (r.type == microtex::DrawRecord::PATH && r.path_codepoint != 0) out += r.path_codepoint;
  }
  return out;
}

/** Some warning of the layout has `part` in it. */
inline bool warns(const Layout& l, const std::string& part) { return anyContains(l.warnings, part); }

/** The first-column text of each line of text, top to bottom: the text
 *  records of a layout grouped by their (rounded) baseline, each line its
 *  records left to right with nothing between them. Math is glyphs, not
 *  text, so a line of pure math is not here. */
inline Strings lines(const Layout& l) {
  std::vector<const microtex::DrawRecord*> runs;
  for (const auto& r : l.records) {
    if (r.type == microtex::DrawRecord::TEXT) runs.push_back(&r);
  }
  std::stable_sort(runs.begin(), runs.end(), [](const auto* a, const auto* b) {
    const float ya = std::round(a->y), yb = std::round(b->y);
    return ya != yb ? ya < yb : a->x < b->x;
  });
  Strings out;
  float lastY = 0;
  for (const auto* r : runs) {
    const float y = std::round(r->y);
    if (out.empty() || y != lastY) out.emplace_back();
    out.back() += r->text;
    lastY = y;
  }
  return out;
}

}  // namespace check

/** Every pair of `pairs` draws alike, in the mode and at the width of `opt`. */
#define CHECK_PAIRS_WITH(pairs, opt) check::checkPairs(__FILE__, __LINE__, (pairs), (opt))
#define CHECK_PAIRS(pairs) check::checkPairs(__FILE__, __LINE__, (pairs), check::Options())

#endif
