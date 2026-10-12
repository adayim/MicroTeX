// microtex-svg: LaTeX in, an SVG image out.
//
// The smallest complete use of the engine, and the one gridmicrotex makes: no
// graphics library. The engine draws into a Graphics2D_Recorder, which keeps
// every draw operation as data (graphic/graphic_recorder.h); glyphs come as
// outlines, so the image needs no font. This file turns the records into SVG.
//
// Text (\text{}, and the prose modes) is laid out by this program, which the
// engine asks to measure and draw it: FreeType reads the text fonts, and each
// glyph is drawn as an outline too. There is no shaping, ligatures or
// kerning, and a character no font has is left out; --fallback adds fonts.
// A font with colour layers (COLR/CPAL, such as res/twemoji) is drawn in colour.
//
//   microtex-svg [options] "LATEX" out.svg
//   microtex-svg [options] --samples FILE --outdir DIR [--prefix P] [--index FILE.md]
//
//   --res DIR         the `res` directory (fonts and the LaTeX files), default ./res
//   --font FILE       the OpenType math font, default <res>/lm-math/latinmodern-math.otf
//   --text-font FILE  the roman text font, default <res>/xits/XITS-Regular.otf; the
//                     bold, italic and bold italic ones are found beside it by name
//                     (XITS-Bold.otf for XITS-Regular.otf)
//   --sans-font FILE  sans-serif text, default <res>/firamath/FiraMath-Regular.otf
//   --mono-font FILE  monospaced text, default the roman font
//   --fallback FILE   a font for the characters the others lack (repeat for more)
//   --size PT         text size, default 20
//   --width PX        wrap lines at this width, default 0 (no limit)
//   --mode MODE       math (default), mixed (a label of text with $math$) or document
//   --display         set the formula in display style
//   --background C    fill the image with the colour C (an SVG colour), default none
//
// With --samples the file is split into formulas by the lines of a single '%'
// (res/SAMPLES.tex is one) and each is drawn into DIR/PREFIXn.svg; --index
// also writes a Markdown page with each formula's source, its image and the
// warnings the engine gave.

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_OUTLINE_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "atom/font_family_atom.h"
#include "atom/image_atom.h"
#include "atom/mark_atom.h"
#include "front/front.h"
#include "front/packages.h"
#include "graphic/font_style.h"
#include "graphic/graphic.h"
#include "graphic/graphic_recorder.h"
#include "microtex.h"
#include "render/render.h"
#include "samples.h"
#include "unimath/font_src.h"

using namespace microtex;
namespace fs = std::filesystem;

namespace {

// ---- text: FreeType, outlines ----------------------------------------------

struct Face {
  FT_Face face = nullptr;
  float unitsPerEm = 1000;
  // The first palette of a colour font (COLR/CPAL), owned by the face; null otherwise.
  const FT_Color* palette = nullptr;
  unsigned paletteSize = 0;
};

class TextFonts {
public:
  ~TextFonts() {
    for (auto& f : _all) FT_Done_Face(f.face);
    if (_library != nullptr) FT_Done_FreeType(_library);
  }

  bool init() { return FT_Init_FreeType(&_library) == 0; }

  // Index of the face loaded from `file`, or -1.
  int load(const std::string& file) {
    if (file.empty()) return -1;
    Face f;
    if (FT_New_Face(_library, file.c_str(), 0, &f.face) != 0) return -1;
    FT_Select_Charmap(f.face, FT_ENCODING_UNICODE);
    f.unitsPerEm = f.face->units_per_EM > 0 ? static_cast<float>(f.face->units_per_EM) : 1000.f;
    FT_Palette_Data data;
    FT_Color* palette = nullptr;
    if (FT_Palette_Data_Get(f.face, &data) == 0 && data.num_palettes > 0 &&
        FT_Palette_Select(f.face, 0, &palette) == 0) {
      f.palette = palette;
      f.paletteSize = data.num_palette_entries;
    }
    _all.push_back(f);
    return static_cast<int>(_all.size()) - 1;
  }

  // The face for a style, in the order: the style's own, the roman, the fallbacks.
  void setStyles(int roman, int bold, int italic, int boldItalic, int sans, int mono) {
    _roman = roman; _bold = bold; _italic = italic; _boldItalic = boldItalic; _sans = sans; _mono = mono;
  }
  void addFallback(int face) { _fallbacks.push_back(face); }

  // `emoji`: the character is followed by U+FE0F, which asks for its colour form.
  const Face* find(FontStyle style, c32 cp, FT_UInt& glyph, bool emoji = false) const {
    int own = _roman;
    if (isMono(style)) own = _mono;
    else if (isSansSerif(style)) own = _sans;
    else if (isBold(style) && isItalic(style)) own = _boldItalic;
    else if (isBold(style)) own = _bold;
    else if (isItalic(style)) own = _italic;
    std::vector<int> order;
    if (emoji) {
      for (const int i : _fallbacks) {
        if (_all[static_cast<std::size_t>(i)].palette != nullptr) order.push_back(i);
      }
    }
    order.push_back(own);
    order.push_back(_roman);
    order.insert(order.end(), _fallbacks.begin(), _fallbacks.end());
    for (const int i : order) {
      if (i < 0) continue;
      glyph = FT_Get_Char_Index(_all[static_cast<std::size_t>(i)].face, cp);
      if (glyph != 0) return &_all[static_cast<std::size_t>(i)];
    }
    return nullptr;
  }

  const Face* primary(FontStyle style) const {
    int own = isMono(style) ? _mono : isSansSerif(style) ? _sans : _roman;
    if (own < 0) own = _roman;
    return own < 0 ? nullptr : &_all[static_cast<std::size_t>(own)];
  }

private:
  FT_Library _library = nullptr;
  std::vector<Face> _all;
  int _roman = -1, _bold = -1, _italic = -1, _boldItalic = -1, _sans = -1, _mono = -1;
  std::vector<int> _fallbacks;
};

TextFonts* gFonts = nullptr;

std::vector<c32> decode(const std::string& s) {
  std::vector<c32> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    c32 cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    out.push_back(cp);
    i += n;
  }
  return out;
}

bool zeroWidth(c32 cp) { return cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x200B; }

class OutlineText : public TextLayout {
public:
  OutlineText(std::string src, FontStyle style, float size)
      : _chars(decode(src)), _style(style), _size(size) {}

  void getBounds(Rect& bounds) override {
    const Face* p = gFonts->primary(_style);
    const float scale = p != nullptr ? _size / p->unitsPerEm : 0.f;
    const float ascent = p != nullptr ? static_cast<float>(p->face->ascender) * scale : _size * 0.8f;
    const float descent = p != nullptr ? -static_cast<float>(p->face->descender) * scale : _size * 0.2f;
    float width = 0;
    for (std::size_t i = 0; i < _chars.size(); i++) width += advance(i);
    bounds.x = 0;
    bounds.y = -ascent;
    bounds.w = width;
    bounds.h = ascent + descent;
  }

  void draw(Graphics2D& g2, float x, float y) override {
    float pen = x;
    for (std::size_t i = 0; i < _chars.size(); i++) {
      const c32 cp = _chars[i];
      FT_UInt glyph = 0;
      const Face* f = gFonts->find(_style, cp, glyph, emojiForm(i));
      if (f != nullptr && !zeroWidth(cp)) {
        // A colour glyph is a stack of layers, each an outline and a palette
        // colour (0xFFFF: the text colour); any other is its own outline.
        bool layered = false;
        if (f->palette != nullptr) {
          const color text = g2.getColor();
          FT_LayerIterator it;
          it.p = nullptr;
          FT_UInt layer = 0, index = 0;
          while (FT_Get_Color_Glyph_Layer(f->face, glyph, &layer, &index, &it)) {
            layered = true;
            g2.setColor(index < f->paletteSize ? argb(f->palette[index]) : text);
            fill(g2, *f, layer, pen, y);
          }
          g2.setColor(text);
        }
        if (!layered) fill(g2, *f, glyph, pen, y);
      }
      pen += advance(i);
    }
  }

private:
  struct Pen {
    Graphics2D* g2;
    float x, y, scale;
    bool open = false;
  };

  static color argb(const FT_Color& c) {
    return (static_cast<color>(c.alpha) << 24) | (static_cast<color>(c.red) << 16) |
           (static_cast<color>(c.green) << 8) | c.blue;
  }

  // One outline of a face, filled in the current colour with its origin at (x, y).
  void fill(Graphics2D& g2, const Face& f, FT_UInt glyph, float x, float y) const {
    if (FT_Load_Glyph(f.face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) return;
    Pen p{&g2, x, y, _size / f.unitsPerEm};
    FT_Outline_Funcs funcs{moveTo, lineTo, conicTo, cubicTo, 0, 0};
    g2.beginPath(-1);
    FT_Outline_Decompose(&f.face->glyph->outline, &funcs, &p);
    if (p.open) g2.closePath();
    g2.fillPath(-1);
  }

  static float px(const Pen& p, const FT_Vector* v) { return p.x + static_cast<float>(v->x) * p.scale; }
  static float py(const Pen& p, const FT_Vector* v) { return p.y - static_cast<float>(v->y) * p.scale; }

  static int moveTo(const FT_Vector* to, void* user) {
    auto* p = static_cast<Pen*>(user);
    if (p->open) p->g2->closePath();
    p->g2->moveTo(px(*p, to), py(*p, to));
    p->open = true;
    return 0;
  }
  static int lineTo(const FT_Vector* to, void* user) {
    auto* p = static_cast<Pen*>(user);
    p->g2->lineTo(px(*p, to), py(*p, to));
    return 0;
  }
  static int conicTo(const FT_Vector* c, const FT_Vector* to, void* user) {
    auto* p = static_cast<Pen*>(user);
    p->g2->quadTo(px(*p, c), py(*p, c), px(*p, to), py(*p, to));
    return 0;
  }
  static int cubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    auto* p = static_cast<Pen*>(user);
    p->g2->cubicTo(px(*p, c1), py(*p, c1), px(*p, c2), py(*p, c2), px(*p, to), py(*p, to));
    return 0;
  }

  bool emojiForm(std::size_t i) const { return i + 1 < _chars.size() && _chars[i + 1] == 0xFE0F; }

  float advance(std::size_t i) const {
    const c32 cp = _chars[i];
    if (zeroWidth(cp)) return 0.f;
    FT_UInt glyph = 0;
    const Face* f = gFonts->find(_style, cp, glyph, emojiForm(i));
    // A character no font has takes room, so the gap shows it was there.
    if (f == nullptr) return _size * 0.5f;
    if (FT_Load_Glyph(f->face, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) return 0.f;
    return static_cast<float>(f->face->glyph->metrics.horiAdvance) * _size / f->unitsPerEm;
  }

  std::vector<c32> _chars;
  FontStyle _style;
  float _size;
};

class Platform : public PlatformFactory {
public:
  sptr<Font> createFont(const std::string& file) override { return sptrOf<RecordedFont>(file); }
  sptr<TextLayout> createTextLayout(const std::string& src, FontStyle style, float size) override {
    return sptrOf<OutlineText>(src, style, size);
  }
};

// ---- the LaTeX files -------------------------------------------------------

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// The engine's own commands are LaTeX files, and it reads no file: hand it
// the text of the format and of each package.
bool loadTexFiles(const fs::path& dir) {
  if (!fs::exists(dir / "prelude.tex")) return false;
  front::setPrelude(readFile(dir / "prelude.tex"));
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".sty") {
      front::registerPackage(entry.path().stem().string(), readFile(entry.path()));
    }
  }
  return true;
}

// ---- SVG -------------------------------------------------------------------

std::string hex(color c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
  return buf;
}

std::string paint(const char* attribute, color c) {
  char buf[64];
  const float alpha = static_cast<float>((c >> 24) & 0xFF) / 255.f;
  if (alpha < 0.999f) {
    std::snprintf(buf, sizeof buf, "%s=\"%s\" %s-opacity=\"%.3f\"", attribute, hex(c).c_str(), attribute, alpha);
  } else {
    std::snprintf(buf, sizeof buf, "%s=\"%s\"", attribute, hex(c).c_str());
  }
  return buf;
}

// u / 100 with the digits it needs: 1234 is "12.34", -50 is "-0.5".
std::string hundredths(long u) {
  const bool negative = u < 0;
  u = std::labs(u);
  std::string s = std::to_string(u / 100);
  const long f = u % 100;
  if (f != 0) {
    s += '.';
    s += static_cast<char>('0' + f / 10);
    if (f % 10 != 0) s += static_cast<char>('0' + f % 10);
  }
  return negative && s != "0" ? "-" + s : s;
}

std::string escape(const std::string& s) {
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string toSvg(const std::vector<DrawRecord>& records, float width, float height, float pad,
                  const std::string& background) {
  std::ostringstream svg;
  svg.setf(std::ios::fixed);
  svg.precision(3);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width + 2 * pad << "\" height=\""
      << height + 2 * pad << "\" viewBox=\"" << -pad << " " << -pad << " " << width + 2 * pad << " "
      << height + 2 * pad << "\">\n";
  if (!background.empty()) {
    svg << "<rect x=\"" << -pad << "\" y=\"" << -pad << "\" width=\"" << width + 2 * pad << "\" height=\""
        << height + 2 * pad << "\" fill=\"" << escape(background) << "\"/>\n";
  }
  for (const auto& r : records) {
    switch (r.type) {
      case DrawRecord::PATH: {
        // Relative commands on a grid of 0.01: the numbers are short, and the
        // rounding does not add up along the path.
        svg << "<path " << paint("fill", r.col) << " d=\"";
        long cx = 0, cy = 0, sx = 0, sy = 0;
        const auto rel = [&](float x, float y) {
          const long nx = std::lround(x * 100.f), ny = std::lround(y * 100.f);
          const std::string out = hundredths(nx - cx) + " " + hundredths(ny - cy);
          cx = nx;
          cy = ny;
          return out;
        };
        for (const auto& s : r.path_segments) {
          const float* c = s.coords;
          switch (s.cmd) {
            case DrawRecord::PathSegment::MOVE:
              cx = cy = 0;
              svg << "M" << rel(c[0], c[1]);
              sx = cx;
              sy = cy;
              break;
            case DrawRecord::PathSegment::LINE_TO: svg << "l" << rel(c[0], c[1]); break;
            case DrawRecord::PathSegment::CUBIC: {
              // All three points are relative to where the curve starts.
              const long x0 = cx, y0 = cy;
              std::string out = "c" + rel(c[0], c[1]);
              cx = x0;
              cy = y0;
              out += " " + rel(c[2], c[3]);
              cx = x0;
              cy = y0;
              svg << out << " " << rel(c[4], c[5]);
            } break;
            case DrawRecord::PathSegment::QUAD: {
              const long x0 = cx, y0 = cy;
              std::string out = "q" + rel(c[0], c[1]);
              cx = x0;
              cy = y0;
              svg << out << " " << rel(c[2], c[3]);
            } break;
            case DrawRecord::PathSegment::CLOSE:
              svg << "z";
              cx = sx;
              cy = sy;
              break;
          }
        }
        svg << "\"/>\n";
      } break;
      case DrawRecord::LINE:
        svg << "<line x1=\"" << r.x1 << "\" y1=\"" << r.y1 << "\" x2=\"" << r.x2 << "\" y2=\"" << r.y2
            << "\" " << paint("stroke", r.col) << " stroke-width=\"" << r.line_width << "\"/>\n";
        break;
      case DrawRecord::RECT:
      case DrawRecord::ROUND_RECT:
        svg << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.width << "\" height=\""
            << r.height << "\" rx=\"" << r.rx << "\" ry=\"" << r.ry << "\" fill=\"none\" "
            << paint("stroke", r.col) << " stroke-width=\"" << r.line_width << "\"/>\n";
        break;
      case DrawRecord::FILL_RECT:
      case DrawRecord::FILL_ROUND_RECT:
        svg << "<rect x=\"" << r.x << "\" y=\"" << r.y << "\" width=\"" << r.width << "\" height=\""
            << r.height << "\" rx=\"" << r.rx << "\" ry=\"" << r.ry << "\" " << paint("fill", r.col)
            << "/>\n";
        break;
      default: break;  // GLYPH (only when glyphs are not drawn as outlines), TEXT, MARK, IMAGE
    }
  }
  svg << "</svg>\n";
  return svg.str();
}

// ---- one formula -----------------------------------------------------------

struct Settings {
  float size = 20, width = 0;
  InputMode mode = InputMode::math;
  bool display = false;
  std::string background;
};

struct Drawn {
  bool ok = false;
  std::string svg, error;
  std::vector<std::string> warnings;
};

Drawn draw(const std::string& tex, const Settings& s) {
  Drawn out;
  try {
    std::unique_ptr<Render> render(MicroTeX::parse(
      tex, s.width, s.size, s.size / 3.f, 0xFF000000u, true,
      OverrideTeXStyle{s.display, TexStyle::display}, "", "", s.mode));
    for (const auto& d : front::lastDiagnostics().items()) {
      out.warnings.push_back(std::to_string(d.span.line) + ":" + std::to_string(d.span.col) + ": " + d.message);
    }
    if (!render) {
      out.error = "nothing to draw";
      return out;
    }
    Graphics2D_Recorder recorder;
    render->draw(recorder, 0, 0);
    out.svg = toSvg(recorder.records(), static_cast<float>(render->getWidth()),
                    static_cast<float>(render->getHeight()), s.size / 4.f, s.background);
    out.ok = true;
  } catch (const std::exception& e) {
    out.error = e.what();
  }
  return out;
}

// ---- samples ---------------------------------------------------------------

std::string titleOf(const std::string& sample, int n) {
  // A sample may start with a TeX comment naming it: `%Fractions`.
  if (!sample.empty() && sample[0] == '%') {
    const auto end = sample.find('\n');
    std::string t = sample.substr(1, end == std::string::npos ? std::string::npos : end - 1);
    while (!t.empty() && (t.back() == '\r' || t.back() == ' ')) t.pop_back();
    if (!t.empty()) return t;
  }
  return "Sample " + std::to_string(n);
}

int renderSamples(const std::string& file, const fs::path& outdir, const std::string& prefix,
                  const fs::path& index, const Settings& s, const std::vector<std::string>& fallbacks) {
  Samples samples(file);
  if (samples.isEmpty()) {
    std::fprintf(stderr, "microtex-svg: no samples in %s\n", file.c_str());
    return 1;
  }
  fs::create_directories(outdir);
  std::ostringstream md;
  md << "# Samples\n\nThe formulas of [`res/SAMPLES.tex`](../../res/SAMPLES.tex), drawn by "
        "[`microtex-svg`](../svg/svg_main.cpp) with `--samples`; the warnings are the engine's, "
        "with their line and column in the formula. Text is drawn as outlines from the fonts "
        "given, and a character none of them has is left blank.";
  if (!fallbacks.empty() && !index.empty()) {
    // The fonts the characters beyond the main ones came from, with their notes (a licence, say).
    md << " Fallback fonts:";
    for (const auto& f : fallbacks) {
      md << " `" << fs::path(f).generic_string() << "`";
      const fs::path notes = fs::path(f).parent_path() / "README.md";
      if (fs::exists(notes)) {
        md << " ([notes](" << fs::relative(fs::absolute(notes), fs::absolute(index).parent_path()).generic_string() << "))";
      }
    }
    md << ".";
  }
  md << "\n\n";
  int failed = 0;
  for (int i = 0; i < samples.count(); i++) {
    const std::string& tex = samples.next();
    const Drawn d = draw(tex, s);
    const std::string name = prefix + std::to_string(i + 1) + ".svg";
    if (d.ok) {
      std::ofstream(outdir / name, std::ios::binary) << d.svg;
    } else {
      failed++;
      std::fprintf(stderr, "microtex-svg: sample %d: %s\n", i + 1, d.error.c_str());
    }
    if (!index.empty()) {
      md << "## " << i + 1 << ". " << titleOf(tex, i + 1) << "\n\n```tex\n" << tex << "```\n\n";
      if (d.ok) {
        md << "![sample " << i + 1 << "](" << fs::relative(outdir / name, index.parent_path()).generic_string() << ")\n\n";
      } else {
        md << "Not drawn: " << d.error << "\n\n";
      }
      if (!d.warnings.empty()) {
        md << "Warnings:\n\n";
        for (const auto& w : d.warnings) md << "- " << w << "\n";
        md << "\n";
      }
    }
  }
  if (!index.empty()) std::ofstream(index, std::ios::binary) << md.str();
  std::printf("%d samples, %d not drawn\n", samples.count(), failed);
  return failed == 0 ? 0 : 1;
}

int usage() {
  std::fprintf(stderr,
    "usage: microtex-svg [--res DIR] [--font FILE] [--text-font FILE] [--sans-font FILE]\n"
    "                    [--mono-font FILE] [--fallback FILE]... [--size PT] [--width PX]\n"
    "                    [--mode math|mixed|document] [--display] [--background COLOR]\n"
    "                    \"LATEX\" out.svg\n"
    "       microtex-svg [options] --samples FILE --outdir DIR [--prefix P] [--index FILE.md]\n");
  return 2;
}

// XITS-Regular.otf: its bold, italic and bold italic are XITS-Bold.otf ... beside it.
std::string sibling(const std::string& regular, const char* style) {
  const auto at = regular.rfind("Regular");
  if (at == std::string::npos) return "";
  std::string s = regular;
  s.replace(at, 7, style);
  return fs::exists(s) ? s : "";
}

}  // namespace

int main(int argc, char** argv) {
  fs::path res = "res", outdir, index;
  std::string font, textFont, sansFont, monoFont, samplesFile, prefix = "sample_";
  std::vector<std::string> fallbacks, positional;
  Settings settings;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--display") {
      settings.display = true;
    } else if (a.rfind("--", 0) == 0) {
      const char* v = value();
      if (v == nullptr) return usage();
      if (a == "--res") res = v;
      else if (a == "--font") font = v;
      else if (a == "--text-font") textFont = v;
      else if (a == "--sans-font") sansFont = v;
      else if (a == "--mono-font") monoFont = v;
      else if (a == "--fallback") fallbacks.push_back(v);
      else if (a == "--size") settings.size = std::strtof(v, nullptr);
      else if (a == "--width") settings.width = std::strtof(v, nullptr);
      else if (a == "--background") settings.background = v;
      else if (a == "--samples") samplesFile = v;
      else if (a == "--outdir") outdir = v;
      else if (a == "--prefix") prefix = v;
      else if (a == "--index") index = v;
      else if (a == "--mode") {
        if (std::string(v) == "math") settings.mode = InputMode::math;
        else if (std::string(v) == "mixed") settings.mode = InputMode::mixed;
        else if (std::string(v) == "document") settings.mode = InputMode::document;
        else return usage();
      } else {
        return usage();
      }
    } else {
      positional.push_back(a);
    }
  }
  const bool batch = !samplesFile.empty();
  if (batch ? (!positional.empty() || outdir.empty()) : positional.size() != 2) return usage();

  if (font.empty()) font = (res / "lm-math" / "latinmodern-math.otf").string();
  if (textFont.empty()) textFont = (res / "xits" / "XITS-Regular.otf").string();
  if (sansFont.empty()) sansFont = (res / "firamath" / "FiraMath-Regular.otf").string();
  if (!fs::exists(font)) {
    std::fprintf(stderr, "microtex-svg: no math font at %s (see --res and --font)\n", font.c_str());
    return 1;
  }
  if (!loadTexFiles(res / "tex")) {
    std::fprintf(stderr, "microtex-svg: no prelude.tex in %s (see --res)\n", (res / "tex").string().c_str());
    return 1;
  }

  TextFonts fonts;
  gFonts = &fonts;
  if (!fonts.init()) {
    std::fprintf(stderr, "microtex-svg: FreeType did not start\n");
    return 1;
  }
  const int roman = fonts.load(textFont);
  if (roman < 0) {
    std::fprintf(stderr, "microtex-svg: cannot read the text font %s (see --text-font)\n", textFont.c_str());
    return 1;
  }
  const auto orRoman = [&](int face) { return face < 0 ? roman : face; };
  const int mono = monoFont.empty() ? roman : orRoman(fonts.load(monoFont));
  fonts.setStyles(roman, orRoman(fonts.load(sibling(textFont, "Bold"))),
                  orRoman(fonts.load(sibling(textFont, "Italic"))),
                  orRoman(fonts.load(sibling(textFont, "BoldItalic"))), orRoman(fonts.load(sansFont)), mono);
  for (const auto& f : fallbacks) {
    const int face = fonts.load(f);
    if (face < 0) std::fprintf(stderr, "microtex-svg: cannot read the fallback font %s\n", f.c_str());
    else fonts.addFallback(face);
  }

  PlatformFactory::registerFactory("svg", std::make_unique<Platform>());
  PlatformFactory::activate("svg");
  MicroTeX::init(FontSrcOtf(font));
  register_mark_macro();
  register_font_family_macro();
  register_image_macros();
  MicroTeX::setRenderGlyphUsePath(true);

  int status = 0;
  if (batch) {
    status = renderSamples(samplesFile, outdir, prefix, index, settings, fallbacks);
  } else {
    const Drawn d = draw(positional[0], settings);
    for (const auto& w : d.warnings) std::fprintf(stderr, "microtex-svg: %s\n", w.c_str());
    if (!d.ok) {
      std::fprintf(stderr, "microtex-svg: %s\n", d.error.c_str());
      status = 1;
    } else {
      std::ofstream out(positional[1], std::ios::binary);
      out << d.svg;
      if (!out) {
        std::fprintf(stderr, "microtex-svg: cannot write %s\n", positional[1].c_str());
        status = 1;
      }
    }
  }
  MicroTeX::release();
  return status;
}
