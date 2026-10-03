#include "atom/atom_text.h"

#include "atom/font_family_atom.h"
#include "box/box_single.h"
#include "env/env.h"
#include "utils/utf.h"

using namespace std;
using namespace microtex;

void TextAtom::append(c32 code) {
  appendToUtf8(_txt, code);
}

sptr<Box> TextAtom::createBox(Env& env) {
  auto style = (_mathMode ? env.mathFontStyle() : env.textFontStyle());
  if (!_mathMode) style = withRoleFamily(style, env);
  return sptrOf<TextBox>(_txt, style, Env::fixedTextSize() * env.scale());
}
