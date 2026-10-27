#ifndef MICROTEX_VLINE_STYLE_H
#define MICROTEX_VLINE_STYLE_H

#include <vector>

#include "env/units.h"
#include "graphic/graphic_basic.h"

namespace microtex {

/** How a vertical rule between columns looks, where its spec says (tabularray's `vline{2}={1-3}{0.4em, red}`). */
struct VlineStyle {
  Dimen thickness;
  bool hasColour = false;
  color colour = 0;
  bool dashed = false;
  /** The rows (of the array) it runs through; empty for all of them. */
  std::vector<bool> rows;
};

}  // namespace microtex

#endif
