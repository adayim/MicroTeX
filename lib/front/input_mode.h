#ifndef GRIDMICROTEX_FRONT_INPUT_MODE_H
#define GRIDMICROTEX_FRONT_INPUT_MODE_H

#include <cstdint>

namespace microtex {

/**
 * How an input is read.
 *
 *   math      A formula: the input is math from the start.
 *   mixed     A plot label: prose, with math in `$...$`, `\(...\)`, `$$...$$`
 *             and `\[...\]`. TeX's rules throughout, but for one: a line end
 *             in the prose is a line break, as R's "\n" is in a label.
 *   document  A LaTeX document body, by TeX's rules with no exception: a
 *             line end is a space, and a blank line (or \par) starts a
 *             paragraph.
 */
enum class InputMode : std::uint8_t { math, mixed, document };

}  // namespace microtex

#endif
