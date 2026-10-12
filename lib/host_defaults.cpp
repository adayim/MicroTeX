// Default answers to the two measurements the recorder asks of its host
// (graphic/graphic_recorder.h): estimates, where a host with a real text
// measurer gives exact ones. They are weak, so a host that defines the
// functions overrides them, and a shared library links with no help from its
// host: a Windows DLL, and Meson's default of -Wl,--no-undefined, need every
// symbol resolved. Only a flipped draw (\reflectbox) asks for them.
//
// This file is the fork's own: gridmicrotex defines the two in R and does not
// build it.

#if defined(__GNUC__) || defined(__clang__)

#include <string>

#include "graphic/graphic_recorder.h"

#define MICROTEX_HOST_DEFAULT __attribute__((weak, visibility("default")))

namespace microtex {

MICROTEX_HOST_DEFAULT float measure_cached_text_width(
  const std::string& text,
  int /*fontStyle*/,
  float fontSize
) {
  std::size_t characters = 0;
  for (const unsigned char c : text) characters += (c & 0xC0) != 0x80 ? 1 : 0;
  return static_cast<float>(characters) * fontSize * 0.55f;
}

MICROTEX_HOST_DEFAULT float measure_glyph_advance(
  const std::string& /*fontFile*/,
  u16 /*glyphId*/,
  float fontSize
) {
  return fontSize * 0.5f;
}

}  // namespace microtex

#endif  // __GNUC__ || __clang__
