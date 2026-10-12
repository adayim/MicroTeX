// microtex-svg: LaTeX in, an SVG image out.
//
// The smallest complete use of the engine, and the one gridmicrotex makes: no
// graphics library. The engine draws into a Graphics2D_Recorder, which keeps
// every draw operation as data (graphic/graphic_recorder.h); glyphs come as
// outlines, so the image needs no font. This file turns the records into SVG.
//
//   microtex-svg [options] "LATEX" out.svg
//
//   --res DIR      the `res` directory (fonts and the LaTeX files), default ./res
//   --font FILE    the OpenType math font, default <res>/lm-math/latinmodern-math.otf
//   --size PT      text size, default 20
//   --width PX     wrap lines at this width, default 0 (no limit)
//   --mode MODE    math (default), mixed (a label of text with $math$) or document
//   --display      set the formula in display style
//   --background C fill the image with the colour C (an SVG colour), default none
//
// Text in \text{} and in the prose modes is drawn as SVG <text>; its width is
// estimated (0.55 em a character), as the engine asks the host for it, so lay
// out prose with a real text measurer (see README.md).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "atom/font_family_atom.h"
#include "atom/image_atom.h"
#include "atom/mark_atom.h"
#include "front/front.h"
#include "front/packages.h"
#include "graphic/graphic.h"
#include "graphic/graphic_recorder.h"
#include "microtex.h"
#include "render/render.h"
#include "unimath/font_src.h"

using namespace microtex;
namespace fs = std::filesystem;

// The recorder leaves two measurements to the host, and only a flipped draw
// (\reflectbox) asks for them: an estimate will do.
namespace microtex {

float measure_cached_text_width(const std::string& text, int, float fontSize) {
  return static_cast<float>(text.size()) * fontSize * 0.55f;
}

float measure_glyph_advance(const std::string&, u16, float fontSize) { return fontSize * 0.5f; }

}  // namespace microtex

namespace {

// A text run as an estimate: 0.55 em a character, drawn as a record.
class EstimatedText : public TextLayout {
public:
  EstimatedText(std::string src, FontStyle style, float size)
      : _src(std::move(src)), _style(style), _size(size) {}

  void getBounds(Rect& bounds) override {
    std::size_t characters = 0;
    for (const unsigned char c : _src) characters += (c & 0xC0) != 0x80 ? 1 : 0;
    bounds.x = 0;
    bounds.y = -_size * 0.8f;
    bounds.w = _size * 0.55f * static_cast<float>(characters);
    bounds.h = _size;
  }

  void draw(Graphics2D& g2, float x, float y) override {
    if (auto* recorder = dynamic_cast<Graphics2D_Recorder*>(&g2)) {
      recorder->drawTextRun(_src, x, y, static_cast<int>(_style), _size);
    }
  }

private:
  std::string _src;
  FontStyle _style;
  float _size;
};

class Platform : public PlatformFactory {
public:
  sptr<Font> createFont(const std::string& file) override { return sptrOf<RecordedFont>(file); }
  sptr<TextLayout> createTextLayout(const std::string& src, FontStyle style, float size) override {
    return sptrOf<EstimatedText>(src, style, size);
  }
};

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

std::string escape(const std::string& s) {
  std::string out;
  for (const char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
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
        svg << "<path " << paint("fill", r.col) << " d=\"";
        for (const auto& s : r.path_segments) {
          const float* c = s.coords;
          switch (s.cmd) {
            case DrawRecord::PathSegment::MOVE: svg << "M" << c[0] << " " << c[1]; break;
            case DrawRecord::PathSegment::LINE_TO: svg << "L" << c[0] << " " << c[1]; break;
            case DrawRecord::PathSegment::CUBIC:
              svg << "C" << c[0] << " " << c[1] << " " << c[2] << " " << c[3] << " " << c[4] << " " << c[5];
              break;
            case DrawRecord::PathSegment::QUAD:
              svg << "Q" << c[0] << " " << c[1] << " " << c[2] << " " << c[3];
              break;
            case DrawRecord::PathSegment::CLOSE: svg << "Z"; break;
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
      case DrawRecord::TEXT: {
        const auto style = static_cast<FontStyle>(r.font_style);
        svg << "<text x=\"" << r.x << "\" y=\"" << r.y << "\" font-size=\"" << r.font_size << "\" "
            << paint("fill", r.col) << " font-family=\""
            << (isMono(style) ? "monospace" : isSansSerif(style) ? "sans-serif" : "serif") << "\""
            << (isBold(style) ? " font-weight=\"bold\"" : "") << (isItalic(style) ? " font-style=\"italic\"" : "");
        if (r.rotation != 0) {
          svg << " transform=\"rotate(" << -r.rotation * 180.f / 3.14159265f << " " << r.x << " " << r.y << ")\"";
        }
        svg << ">" << escape(r.text) << "</text>\n";
      } break;
      default: break;  // GLYPH (only when glyphs are not drawn as outlines), MARK, IMAGE
    }
  }
  svg << "</svg>\n";
  return svg.str();
}

int usage() {
  std::fprintf(stderr,
    "usage: microtex-svg [--res DIR] [--font FILE] [--size PT] [--width PX]\n"
    "                    [--mode math|mixed|document] [--display] [--background COLOR]\n"
    "                    \"LATEX\" out.svg\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path res = "res";
  std::string font;
  float size = 20, width = 0;
  InputMode mode = InputMode::math;
  bool display = false;
  std::string background;
  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    const auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--res" || a == "--font" || a == "--size" || a == "--width" || a == "--mode" ||
        a == "--background") {
      const char* v = value();
      if (v == nullptr) return usage();
      if (a == "--background") background = v;
      else if (a == "--res") res = v;
      else if (a == "--font") font = v;
      else if (a == "--size") size = std::strtof(v, nullptr);
      else if (a == "--width") width = std::strtof(v, nullptr);
      else if (std::string(v) == "math") mode = InputMode::math;
      else if (std::string(v) == "mixed") mode = InputMode::mixed;
      else if (std::string(v) == "document") mode = InputMode::document;
      else return usage();
    } else if (a == "--display") {
      display = true;
    } else {
      positional.push_back(a);
    }
  }
  if (positional.size() != 2) return usage();
  if (font.empty()) font = (res / "lm-math" / "latinmodern-math.otf").string();
  if (!fs::exists(font)) {
    std::fprintf(stderr, "microtex-svg: no math font at %s (see --res and --font)\n", font.c_str());
    return 1;
  }
  if (!loadTexFiles(res / "tex")) {
    std::fprintf(stderr, "microtex-svg: no prelude.tex in %s (see --res)\n", (res / "tex").string().c_str());
    return 1;
  }

  PlatformFactory::registerFactory("svg", std::make_unique<Platform>());
  PlatformFactory::activate("svg");
  MicroTeX::init(FontSrcOtf(font));
  register_mark_macro();
  register_font_family_macro();
  register_image_macros();
  MicroTeX::setRenderGlyphUsePath(true);

  int status = 0;
  try {
    std::unique_ptr<Render> render(MicroTeX::parse(
      positional[0], width, size, size / 3.f, 0xFF000000u, true,
      OverrideTeXStyle{display, TexStyle::display}, "", "", mode));
    for (const auto& d : front::lastDiagnostics().items()) {
      std::fprintf(stderr, "microtex-svg: %u:%u: %s\n", static_cast<unsigned>(d.span.line),
                   static_cast<unsigned>(d.span.col), d.message.c_str());
    }
    if (!render) {
      std::fprintf(stderr, "microtex-svg: nothing to draw\n");
      status = 1;
    } else {
      Graphics2D_Recorder recorder;
      render->draw(recorder, 0, 0);
      std::ofstream out(positional[1], std::ios::binary);
      out << toSvg(recorder.records(), static_cast<float>(render->getWidth()),
                   static_cast<float>(render->getHeight()), size / 4.f, background);
      if (!out) {
        std::fprintf(stderr, "microtex-svg: cannot write %s\n", positional[1].c_str());
        status = 1;
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "microtex-svg: %s\n", e.what());
    status = 1;
  }
  MicroTeX::release();
  return status;
}
