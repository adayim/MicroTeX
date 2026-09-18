#include "atom/font_family_atom.h"

#include "atom/atom_font.h"
#include "box/box_single.h"
#include "core/formula.h"
#include "env/env.h"
#include "macro/macro.h"

namespace microtex {

const std::string kDefaultFamilyName = "gridmicrotex.default";

namespace {

std::vector<std::string> s_families;
const std::string s_empty;

bool s_registered = false;

// Delegate for \gmfontfamily{name}{content}. args[0] is the macro name.
sptr<Atom> font_family_macro_delegate(Parser& tp, std::vector<std::string>& args) {
    const int index = register_font_family(args[1]);
    const auto atom = Formula(tp, args[2], false, false)._root;
    if (index == 0) return atom;  // registry full: render, just unstyled
    return sptr<Atom>(new FontFamilyAtom(index, atom));
}

// Delegate for \textrm{content}, replacing MicroTeX's built-in.
//
// The inner FontStyleAtom is exactly what macro_fonts.h builds -- text mode,
// nested, so \textbf{\textrm{x}} keeps its bold. The wrapper is the part
// upstream cannot express: it names the reserved family, so a \textrm inside
// \textsf{} or \texttt{} returns to the caller's font instead of inheriting
// sans or mono.
sptr<Atom> text_rm_macro_delegate(Parser& tp, std::vector<std::string>& args) {
    const auto atom = Formula(tp, args[1], false, false)._root;
    const auto styled = sptrOf<FontStyleAtom>(FontStyle::rm, false, atom, true);
    return sptr<Atom>(new FontFamilyAtom(kDefaultFamilyIndex, styled));
}

}  // namespace

int register_font_family(const std::string& name) {
    if (name.empty()) return 0;
    for (std::size_t i = 0; i < s_families.size(); i++) {
        if (s_families[i] == name) return static_cast<int>(i) + 1;
    }
    if (s_families.size() >= kMaxFamilies) return 0;
    s_families.push_back(name);
    return static_cast<int>(s_families.size());
}

const std::string& font_family_name(int index) {
    if (index == kDefaultFamilyIndex) return kDefaultFamilyName;
    if (index <= 0 || static_cast<std::size_t>(index) > s_families.size()) {
        return s_empty;
    }
    return s_families[static_cast<std::size_t>(index) - 1];
}

const std::string& font_family_of_style(int font_style) {
    const u16 bits = static_cast<u16>(font_style) & kFamilyMask;
    return font_family_name(bits >> kFamilyShift);
}

void clear_font_families() {
    s_families.clear();
}

sptr<Box> FontFamilyAtom::createBox(Env& env) {
    if (_atom == nullptr) return StrutBox::empty();
    // Replace any enclosing family rather than OR onto it: two indices
    // OR'd together address a third, unrelated family. The bold/italic
    // bits in the low byte are left exactly as they were, so a family
    // span composes with the emphasis around it.
    const u16 outer = static_cast<u16>(env.textFontStyle()) & kFamilyMask;
    const auto mine = static_cast<FontStyle>(_index << kFamilyShift);
    if (outer != 0) env.removeTextFontStyle(static_cast<FontStyle>(outer));
    env.addTextFontStyle(mine);
    auto box = _atom->createBox(env);
    env.removeTextFontStyle(mine);
    if (outer != 0) env.addTextFontStyle(static_cast<FontStyle>(outer));
    return box;
}

void register_font_family_macro() {
    if (s_registered) return;
    MacroInfo::add("gmfontfamily", new PreDefMacro(2, font_family_macro_delegate));
    MacroInfo::add("textrm", new PreDefMacro(1, text_rm_macro_delegate));
    s_registered = true;
}

}  // namespace microtex
