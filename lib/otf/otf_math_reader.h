#ifndef MICROTEX_OTF_MATH_READER_H
#define MICROTEX_OTF_MATH_READER_H

#include <string>

namespace microtex {

class Otf;

/**
 * An engine font read straight from a font file: its basics through
 * FreeType, its OpenType MATH table parsed here, and its glyph outlines
 * walked for path rendering. No companion `.clm` file, no FontForge.
 *
 * A font without a MATH table is still usable as a main (text) font; it
 * then says `isMathFont()` false.
 *
 * Requires FreeType. Throws `std::runtime_error` when the file cannot be
 * opened or read.
 *
 * @param path  path to an OTF/TTF file
 * @param index face index within a font collection (.ttc); 0 for a plain font
 * @return a new font, owned by the caller
 */
Otf* otfFromFile(const std::string& path, int index = 0);

}  // namespace microtex

#endif  // MICROTEX_OTF_MATH_READER_H
