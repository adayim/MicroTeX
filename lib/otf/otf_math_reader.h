#ifndef MICROTEX_OTF_MATH_READER_H
#define MICROTEX_OTF_MATH_READER_H

#include <string>
#include <vector>

#include "utils/types.h"

namespace microtex {

/**
 * Synthesise CLM font metrics straight from a font file's OpenType MATH
 * table, in memory.
 *
 * This is an alternative to shipping companion `.clm2` files built ahead of
 * time: no FontForge, no `otf2clm.py`, no disk cache. The bytes returned by
 * `otfToClmBytes` are what `CLMReader::read` expects, so they can be handed
 * to `MicroTeX::addFont` through `FontSrcData`.
 *
 * Requires FreeType. Both functions throw `std::runtime_error` when the file
 * cannot be opened or parsed.
 */

/**
 * Raw bytes of the font's `MATH` table.
 *
 * @param path  path to an OTF/TTF file
 * @param index face index within a font collection (.ttc); 0 for a plain font
 * @return the table's bytes, or an empty vector when the font has no MATH
 *         table (i.e. it is not a math font)
 */
std::vector<u8> otfMathTableBytes(const std::string& path, int index = 0);

/**
 * CLM metrics synthesised from the font's MATH table and glyph outlines.
 *
 * Fonts without a MATH table are still usable as main (text) fonts; the
 * result then carries `isMathFont = 0`.
 *
 * @param path  path to an OTF/TTF file
 * @param index face index within a font collection (.ttc); 0 for a plain font
 * @return CLM bytes suitable for `FontSrcData`
 */
std::vector<u8> otfToClmBytes(const std::string& path, int index = 0);

}  // namespace microtex

#endif  // MICROTEX_OTF_MATH_READER_H
