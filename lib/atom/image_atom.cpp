#include "atom/image_atom.h"

#include "atom/atom_text.h"
#include "env/units.h"
#include "graphic/graphic_recorder.h"
#include "macro/macro.h"
#include "utils/utf.h"

namespace microtex {

sptr<Box> ImageAtom::createBox(Env& env) {
    const float w = Units::fsize(Units::getDimen(_width + "bp"), env);
    const float h = Units::fsize(Units::getDimen(_height + "bp"), env);
    return sptr<Box>(new ImageBox(_ref, w, h));
}

void ImageBox::draw(Graphics2D& g2, float x, float y) {
    auto* recorder = dynamic_cast<Graphics2D_Recorder*>(&g2);
    if (recorder != nullptr) {
        // The record carries the box's *top* edge, matching fillRect, so the
        // R side can place every rectangular record the same way.
        recorder->recordImage(_ref, x, y - _height, _width, _height);
    }
}

namespace {

// \gmgraphics{ref}{width}{height} -- the resolved form R emits.
sptr<Atom> gmgraphics_macro_delegate(Parser&, std::vector<std::string>& args) {
    // args[0] is the macro name; 1..3 are the mandatory arguments.
    return sptr<Atom>(new ImageAtom(args[1], args[2], args[3]));
}

// The file's name, without its directory. Both separators, because the
// argument may hold a Windows path.
std::string basename_of(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

// \includegraphics[opts]{path} that R did not get to: malformed input the
// R scanner leaves alone, or one a \newcommand or \def expanded inside the
// parser, after both resolver passes had run. Draw the file's name so the
// gap is visible, and record it so R can refuse it (see the header).
//
// The name is appended codepoint by codepoint rather than re-parsed, so a
// `_` or `%` in it is a literal and cannot open a subscript or a comment.
sptr<Atom> includegraphics_macro_delegate(Parser&, std::vector<std::string>& args) {
    unresolved_images().push_back(args[1]);
    const std::string name = basename_of(args[1]);
    auto txt = sptrOf<TextAtom>(false);
    int i = 0;
    const int n = static_cast<int>(name.size());
    while (i < n) {
        int len = 0;
        const c32 c = nextUnicode(name, i, len);
        if (len <= 0) break;
        txt->append(c);
        i += len;
    }
    return txt;
}

bool s_registered = false;

}  // namespace

void register_image_macros() {
    if (s_registered) return;
    MacroInfo::add("gmgraphics", new PreDefMacro(3, gmgraphics_macro_delegate));
    // Replaces MicroTeX's own no-op stub; MacroInfo::add deletes the entry it
    // supersedes, which is why this must run at most once (see init.cpp).
    MacroInfo::add("includegraphics", new PreDefMacro(1, 1, includegraphics_macro_delegate));
    s_registered = true;
}

std::vector<std::string>& unresolved_images() {
    static std::vector<std::string> images;
    return images;
}

}  // namespace microtex
