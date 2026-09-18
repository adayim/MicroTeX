#include "utils/bidi.h"

#include <algorithm>

#ifdef HAVE_FRIBIDI
#include <fribidi.h>
#endif

namespace microtex {

bool bidi_available() {
#ifdef HAVE_FRIBIDI
  return true;
#else
  return false;
#endif
}

bool bidi_has_rtl(const std::vector<microtex::c32>& text) {
  // The strong right-to-left blocks. Cheap enough to run over every row,
  // and it is what lets an all-Latin formula skip the work entirely.
  for (const auto c : text) {
    if ((c >= 0x0590 && c <= 0x08FF) ||    // Hebrew, Arabic, Syriac, Thaana, NKo
        (c >= 0xFB1D && c <= 0xFDFF) ||    // Hebrew and Arabic presentation forms
        (c >= 0xFE70 && c <= 0xFEFF) ||    // Arabic presentation forms B
        (c >= 0x10800 && c <= 0x10FFF) ||  // Kharoshthi, Avestan, older RTL scripts
        (c >= 0x1E800 && c <= 0x1EFFF)) {  // Adlam, Arabic mathematical
      return true;
    }
    // The explicit formatting characters carry no script of their own, so
    // the ranges above miss them -- yet they are exactly how a right-to-left
    // run is marked when the letters themselves are direction-neutral.
    if (c == 0x200F ||                  // RIGHT-TO-LEFT MARK
        c == 0x202B || c == 0x202E ||   // RTL EMBEDDING / OVERRIDE
        c == 0x2067) {                  // RIGHT-TO-LEFT ISOLATE
      return true;
    }
  }
  return false;
}

bool bidi_levels(
  const std::vector<microtex::c32>& text,
  std::vector<std::uint8_t>& levels
) {
#ifdef HAVE_FRIBIDI
  if (text.empty()) return false;
  const FriBidiStrIndex n = static_cast<FriBidiStrIndex>(text.size());

  // FriBidiChar is UTF-32, the same as our c32, so the text goes across
  // without conversion.
  static_assert(sizeof(FriBidiChar) == sizeof(microtex::c32),
                "FriBidiChar must be a 32-bit code point");
  const auto* str = reinterpret_cast<const FriBidiChar*>(text.data());

  std::vector<FriBidiCharType> types(text.size());
  fribidi_get_bidi_types(str, n, types.data());

  // FRIBIDI_PAR_ON asks for the base direction to be taken from the first
  // strong character, which is what "auto" means everywhere else.
  FriBidiParType par = FRIBIDI_PAR_ON;
  std::vector<FriBidiLevel> lv(text.size());
  std::vector<FriBidiBracketType> brackets(text.size());
  fribidi_get_bracket_types(str, n, types.data(), brackets.data());

  const FriBidiLevel ok = fribidi_get_par_embedding_levels_ex(
    types.data(), brackets.data(), n, &par, lv.data()
  );
  if (ok == 0) return false;

  levels.assign(text.size(), 0);
  for (std::size_t i = 0; i < text.size(); i++) {
    levels[i] = static_cast<std::uint8_t>(lv[i]);
  }
  return true;
#else
  (void)text;
  (void)levels;
  return false;
#endif
}

bool bidi_assign_levels(const microtex::sptr<microtex::Atom>& root) {
  if (root == nullptr || !bidi_available()) return false;

  std::vector<microtex::c32> text;
  root->collectBidiText(text);
  if (!bidi_has_rtl(text)) return false;

  std::vector<std::uint8_t> levels;
  if (!bidi_levels(text, levels)) return false;

  // The two traversals visit the same atoms in the same order, so the
  // cursor lands on each atom's first character. A mismatch would show
  // as every level after it being off by the same amount.
  std::size_t cursor = 0;
  root->assignBidiLevels(levels, cursor);
  return true;
}

void bidi_reorder(
  std::vector<microtex::sptr<microtex::Box>>& boxes,
  std::vector<std::uint8_t>& levels
) {
  const std::size_t n = boxes.size();
  if (n < 2 || levels.size() != n) return;

  std::uint8_t highest = 0;
  std::uint8_t lowestOdd = 0xFF;
  for (const auto l : levels) {
    highest = std::max(highest, l);
    if ((l & 1u) != 0 && l < lowestOdd) lowestOdd = l;
  }
  if (lowestOdd == 0xFF) return;  // nothing right-to-left on this line

  // Rule L2. Working down from the highest level, every maximal run of
  // boxes at or above the current level is reversed. Applying it level by
  // level is what nests correctly: an Arabic phrase containing a Latin
  // word ends up with the phrase reversed and the word left alone.
  for (int level = highest; level >= lowestOdd; level--) {
    std::size_t i = 0;
    while (i < n) {
      if (levels[i] < level) {
        i++;
        continue;
      }
      std::size_t j = i;
      while (j + 1 < n && levels[j + 1] >= level) j++;
      std::reverse(boxes.begin() + i, boxes.begin() + j + 1);
      std::reverse(levels.begin() + i, levels.begin() + j + 1);
      i = j + 1;
    }
  }
}

}  // namespace microtex
