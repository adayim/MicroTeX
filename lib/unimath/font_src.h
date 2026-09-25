#ifndef MICROTEX_FONT_SRC_H
#define MICROTEX_FONT_SRC_H

#include <string>
#include <vector>

#include "microtexexport.h"
#include "utils/types.h"

namespace microtex {

class Otf;

/** Source to load font. */
class MICROTEX_EXPORT FontSrc {
public:
  /**
   * The font file (descriptor), may be empty if glyphs are drawn by graphical-paths.
   *
   * NOTICE: the 'fontFile' doesn't have to be a real font file, it could be a font id,
   * a font name, or something like that, because all font loading will be done on the
   * user side (you can preload the font resources and gives its id, name or something
   * can distinguish various fonts).
   *
   * If no file was given, the engine will use the font family name as its identifier,
   * thus when load font the font family name will be used as its 'font file' instead
   * of a real font file.
   */
  const std::string fontFile;

  explicit FontSrc(std::string fontFile);

  virtual sptr<Otf> loadOtf() const = 0;

  virtual ~FontSrc() = default;
};

/**
 * Font source from an OpenType font file, read by otf_math_reader: its
 * metrics from its own tables, its math from its MATH table. The file is
 * also the font file drawn with.
 */
class MICROTEX_EXPORT FontSrcOtf : public FontSrc {
public:
  /** The face in a font collection (.ttc); 0 for a plain font. */
  const int index;

  explicit FontSrcOtf(std::string otfFile, int index = 0);

  sptr<Otf> loadOtf() const override;
};

}  // namespace microtex

#endif  // MICROTEX_FONT_SRC_H
