#ifndef GRIDMICROTEX_BIDI_H
#define GRIDMICROTEX_BIDI_H

#include <cstdint>
#include <vector>

#include "atom/atom.h"
#include "box/box.h"
#include "utils/types.h"

namespace microtex {

/**
 * The ordering half of the Unicode bidirectional algorithm.
 *
 * Shaping is not our problem: a text run is handed to the graphics
 * backend whole, and the backend joins and orders the characters inside
 * it. What the backend cannot do is order the runs *between* themselves,
 * because MicroTeX decides where each box goes. That is what this is for.
 *
 * Only needed once a line breaks. Unwrapped text is a single run and the
 * backend orders all of it (see RowAtom::_mergeText); a wrapped
 * paragraph is one run per word, and those have to be reordered per line
 * after the breaks are chosen -- which is the order the algorithm
 * requires anyway.
 *
 * FriBidi supplies the embedding levels. It is optional: without it
 * bidi_available() is false, levels come back empty, and wrapped
 * right-to-left text keeps the left-to-right order it had before.
 */

/** Whether the package was built against FriBidi. */
bool bidi_available();

/** Whether `text` holds a character with a strong right-to-left
 *  direction, i.e. whether resolving levels could change anything. */
bool bidi_has_rtl(const std::vector<microtex::c32>& text);

/**
 * Resolve one embedding level per character, with the base direction
 * taken from the first strong character in the paragraph.
 *
 * Returns false -- leaving `levels` untouched -- when FriBidi is absent
 * or the text is empty, so every caller has a do-nothing path.
 */
bool bidi_levels(
  const std::vector<microtex::c32>& text,
  std::vector<std::uint8_t>& levels
);

/**
 * Reorder one line into visual order: Unicode rule L2, "from the highest
 * level found in the line to the lowest odd level, reverse any
 * contiguous sequence of boxes at that level or higher".
 *
 * `boxes` and `levels` are the same length and are permuted together.
 * Needs no FriBidi -- it is arithmetic over the levels already resolved.
 */
void bidi_reorder(
  std::vector<microtex::sptr<microtex::Box>>& boxes,
  std::vector<std::uint8_t>& levels
);

/**
 * Resolve the whole formula's levels once and write them onto its atoms.
 *
 * Runs before any box is built, because a box does not keep the text it
 * was built from. Doing this per row instead would ask FriBidi to judge
 * each group's direction from that group's own text, so `\textbf{abc}`
 * inside a right-to-left paragraph came out level 0 where UAX #9 says 2,
 * and a neutral run spanning a group boundary was resolved twice against
 * two different contexts.
 *
 * Returns whether anything right-to-left was found. False leaves every
 * atom at level 0, which is the left-to-right path.
 */
bool bidi_assign_levels(const microtex::sptr<microtex::Atom>& root);

}  // namespace microtex

#endif  // GRIDMICROTEX_BIDI_H
