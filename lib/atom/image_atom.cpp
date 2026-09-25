#include "atom/image_atom.h"

#include "env/units.h"
#include "graphic/graphic_recorder.h"
#include "macro/macro.h"
#include "macro/macro_args.h"

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
sptr<Atom> gmgraphics_macro_delegate(CommandArgs& args) {
    // Argument 0 is the macro name; 1..3 are the mandatory arguments.
    return sptr<Atom>(new ImageAtom(args.text(1), args.text(2), args.text(3)));
}

bool s_registered = false;

}  // namespace

void register_image_macros() {
    if (s_registered) return;
    MacroInfo::add("gmgraphics", new CommandMacro(3, gmgraphics_macro_delegate));
    s_registered = true;
}

}  // namespace microtex
