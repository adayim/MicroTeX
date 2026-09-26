// libFuzzer / AFL++ harness for the whole engine: MicroTeX::parse() and
// drawing, with arbitrary bytes. Each iteration:
//   - initialises MicroTeX once, from the OTF file named by MICROTEX_FUZZ_OTF
//   - feeds the raw bytes to the parser as a std::string
//   - catches every exception path and releases the render
//
//     MICROTEX_FUZZ_OTF=res/firamath/FiraMath-Regular.otf test/run.sh fuzz 300
//
// Intent: surface crashes, assertion failures, and memory errors reachable
// from user-supplied LaTeX input.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "microtex.h"
#include "atom/font_family_atom.h"
#include "atom/image_atom.h"
#include "atom/mark_atom.h"
#include "graphic/graphic.h"
#include "macro/macro.h"
#include "unimath/font_src.h"
#include "graphic/graphic_recorder.h"

using namespace microtex;

// The recorder leaves these two measurements to the embedder -- see the
// contract at the end of graphic_recorder.h. An embedder measures with its
// own text engine. The fuzzer has no device, so estimate: both only shift a draw anchor under a horizontal flip, and what
// is being fuzzed is whether the parser survives its input, not layout.
namespace microtex {

float measure_cached_text_width(const std::string& text, int, float fontSize) {
  return static_cast<float>(text.size()) * fontSize * 0.55f;
}

float measure_glyph_advance(const std::string&, u16, float fontSize) {
  return fontSize * 0.5f;
}

}  // namespace microtex

namespace {

// Minimal TextLayout: reports a fixed-size rect so fuzzing does not require
// any external font-metrics source. Correctness of layout is not the target —
// we are looking for crashes in the parser and box builder.
class StubTextLayout : public TextLayout {
 public:
  StubTextLayout(std::string src, float size)
      : _src(std::move(src)), _size(size) {}

  void getBounds(Rect& bounds) override {
    bounds.x = 0;
    bounds.y = -_size * 0.8f;
    bounds.w = _size * 0.5f * static_cast<float>(_src.size());
    bounds.h = _size;
  }

  void draw(Graphics2D&, float, float) override {}

 private:
  std::string _src;
  float _size;
};

class StubPlatformFactory : public PlatformFactory {
 public:
  sptr<Font> createFont(const std::string& file) override {
    return sptrOf<RecordedFont>(file);
  }
  sptr<TextLayout> createTextLayout(const std::string& src,
                                    FontStyle,
                                    float size) override {
    return sptrOf<StubTextLayout>(src, size);
  }
};

bool g_ready = false;

void ensure_init() {
  if (g_ready) return;
  const char* otf = std::getenv("MICROTEX_FUZZ_OTF");
  if (otf == nullptr) {
    std::abort();  // harness is misconfigured; surface immediately.
  }
  PlatformFactory::registerFactory("fuzz",
                                   std::make_unique<StubPlatformFactory>());
  PlatformFactory::activate("fuzz");
  // The font is read from the file itself (otf/otf_math_reader.cpp).
  FontSrcOtf src(otf);
  MicroTeX::init(src);
  MicroTeX::setRenderGlyphUsePath(true);
  register_mark_macro();
  register_font_family_macro();
  register_image_macros();
  g_ready = true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  ensure_init();

  // Cap inputs: MicroTeX is quadratic in a few places; unbounded sizes stall
  // the fuzzer without finding new bugs.
  if (size > 4096) size = 4096;

  std::string tex(reinterpret_cast<const char*>(data), size);

  // Each parse starts clean, as an embedder starts it: \gmfontfamily's
  // names are per parse. Without this, one input's state leaked into the
  // next and the fuzzer reported crashes no embedder can reach. (A
  // label's own definitions live in the front end's per-parse layer, made
  // new for each parse.) No image resolver is set, so an \includegraphics
  // draws its file's name.
  clear_font_families();

  // The first byte picks the input mode too: a formula, a label (mixed) or
  // a document body. Half the inputs stay math, which is the common case.
  const InputMode mode = size == 0                 ? InputMode::math
                         : (data[0] & 3) == 1      ? InputMode::mixed
                         : (data[0] & 3) == 2      ? InputMode::document
                                                   : InputMode::math;
  // Bit 2 of the first byte breaks lines, at a measure from the second, so
  // the line breaker (and what it opens: colour and size boxes, list and
  // minipage columns) is fuzzed too.
  const float width = size > 1 && (data[0] & 4) != 0 ? 40.0f + static_cast<float>(data[1]) : 0.0f;
  std::unique_ptr<Render> render;
  try {
    render.reset(MicroTeX::parse(tex, width, 20.0f, 10.0f, 0x00000000u,
                                 true, {false, TexStyle::text}, "", "", mode));
  } catch (...) {
    return 0;  // expected — malformed LaTeX should surface as an exception.
  }

  if (!render) return 0;

  try {
    Graphics2D_Recorder recorder;
    render->draw(recorder, 0, 0);
  } catch (...) {
    // Draw-time exceptions are also a valid harness observation; keep going.
  }

  return 0;
}
