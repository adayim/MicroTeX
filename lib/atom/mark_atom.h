#pragma once

// MarkAtom / MarkBox: zero-ink anchors inserted by the \mark{name} macro.
// At draw time, the MarkBox detects a Graphics2D_Recorder and pushes a
// MARK record carrying the name and the current (x, y) in world coords.
// On any other Graphics2D it draws nothing — safe for the (currently
// unused) live-rendering paths.

#include <string>

#include "atom/atom.h"
#include "box/box.h"

namespace microtex {

class MarkBox : public Box {
public:
    explicit MarkBox(std::string name) : _name(std::move(name)) {
        _width = 0;
        _height = 0;
        _depth = 0;
    }

    void draw(Graphics2D& g2, float x, float y) override;

    boxname(MarkBox);

private:
    std::string _name;
};

class MarkAtom : public Atom {
public:
    explicit MarkAtom(std::string name) : _name(std::move(name)) {}

    sptr<Box> createBox(Env& env) override {
        return sptr<Box>(new MarkBox(_name));
    }

private:
    std::string _name;
};

// Register the \mark{name} macro with MicroTeX. Idempotent, and registered
// for the life of the process: microtex_release() deliberately leaves the
// macro registry standing (see init.cpp), so this never needs re-running.
void register_mark_macro();

}  // namespace microtex
