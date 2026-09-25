#pragma once

// ImageAtom / ImageBox: an inline raster or vector figure, reserved as a
// box of a fixed size and emitted as an IMAGE draw record.
//
// The engine deliberately knows nothing about image formats. The front end
// hands `\includegraphics[opts]{path}` to the host (front/hooks.h), and R
// answers with the private, fully-normalised
//
//     \gmgraphics{<hex>}{<width_bp>}{<height_bp>}
//
// so this box only has to be the right size and say which file it stands
// for. What is finally drawn into that box --
// a rasterGrob for a bitmap, a grImport2 pictureGrob for an SVG -- is
// chosen R-side in build_latex_children(). A new reader therefore never
// needs a change here.
//
// `<hex>` is a hex-encoded payload rather than the path itself because a
// real path is hostile to every stage in between: Windows paths hold
// backslashes, paths hold spaces, and `%` would start a comment.

#include <string>

#include "atom/atom.h"
#include "box/box.h"

namespace microtex {

class ImageBox : public Box {
public:
    ImageBox(std::string ref, float width, float height) : _ref(std::move(ref)) {
        _width = width;
        _height = height;
        // Sits on the baseline, as \includegraphics does in LaTeX and as an
        // <img> does in HTML. There is deliberately no \raisebox equivalent.
        _depth = 0;
    }

    void draw(Graphics2D& g2, float x, float y) override;

    boxname(ImageBox);

private:
    std::string _ref;
};

class ImageAtom : public Atom {
public:
    // `width` and `height` arrive as bare numbers in big points. They are
    // kept as text and handed to Units::getDimen with a "bp" suffix, so the
    // conversion to the engine's 1000-per-em design units is MicroTeX's own
    // -- the same route RuleAtom::createBox takes.
    ImageAtom(std::string ref, std::string width, std::string height)
        : _ref(std::move(ref)), _width(std::move(width)), _height(std::move(height)) {}

    sptr<Box> createBox(Env& env) override;

private:
    std::string _ref;
    std::string _width;
    std::string _height;
};

// Register `\gmgraphics`, the resolved form R emits.
//
// Idempotent, and registered for the life of the process:
// microtex_release() deliberately leaves the macro registry standing
// (see init.cpp), so this never needs re-running.
void register_image_macros();

}  // namespace microtex
