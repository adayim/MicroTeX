#include "unimath/font_src.h"

#include "otf/otf.h"
#include "otf/otf_math_reader.h"

using namespace microtex;

FontSrc::FontSrc(std::string fontFile) : fontFile(std::move(fontFile)) {}

FontSrcOtf::FontSrcOtf(std::string otfFile, int index)
    : FontSrc(std::move(otfFile)), index(index) {}

sptr<Otf> FontSrcOtf::loadOtf() const {
  return sptr<Otf>(otfFromFile(fontFile, index));
}
