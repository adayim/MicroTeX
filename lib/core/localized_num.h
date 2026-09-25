#ifndef MICROTEX_LOCALIZED_NUM_H
#define MICROTEX_LOCALIZED_NUM_H

#include "utils/types.h"

namespace microtex {

/**
 * Convert a character to roman-number if it is a digit localized
 * @param c character to be converted
 */
c32 convertToRomanNumber(c32 c);

}  // namespace microtex

#endif  // MICROTEX_LOCALIZED_NUM_H
