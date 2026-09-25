#include "atom/mark_atom.h"

#include "graphic/graphic_recorder.h"
#include "macro/macro.h"
#include "macro/macro_args.h"

namespace microtex {

void MarkBox::draw(Graphics2D& g2, float x, float y) {
    auto* recorder = dynamic_cast<Graphics2D_Recorder*>(&g2);
    if (recorder != nullptr) {
        recorder->recordMark(_name, x, y);
    }
}

namespace {

// Delegate for the \mark{name} macro.
sptr<Atom> mark_macro_delegate(CommandArgs& args) {
    // Argument 0 is the macro name itself ("mark"); 1 is the mark name.
    return sptr<Atom>(new MarkAtom(args.text(1)));
}

bool s_registered = false;

}  // namespace

void register_mark_macro() {
    if (s_registered) return;
    MacroInfo::add("mark", new CommandMacro(1, mark_macro_delegate));
    s_registered = true;
}

}  // namespace microtex
