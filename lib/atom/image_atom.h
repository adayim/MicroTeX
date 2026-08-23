#pragma once

// ImageAtom / ImageBox: an inline raster or vector figure, reserved as a
// box of a fixed size and emitted as an IMAGE draw record.
//
// The engine deliberately knows nothing about image formats. R resolves
// `\includegraphics[opts]{path}` into the private, fully-normalised
//
//     \gmgraphics{<hex>}{<width_bp>}{<height_bp>}
//
// before MicroTeX ever sees it, so this box only has to be the right size
// and say which file it stands for. What is finally drawn into that box --
// a rasterGrob for a bitmap, a grImport2 pictureGrob for an SVG -- is
// chosen R-side in build_latex_children(). A new reader therefore never
// needs a change here.
//
// `<hex>` is a hex-encoded payload rather than the path itself because a
// real path is hostile to every stage in between: Windows paths hold
// backslashes, paths hold spaces, .strip_document_wrappers() eats
// `%`-to-end-of-line, and .expand_macros() would rewrite `\Users` or
// `\Temp` mid-path if a user had defined a macro by that name.

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

// Register `\gmgraphics` (the resolved form R emits) and override the
// built-in `\includegraphics`.
//
// The override is a backstop, not the main path. R's resolver reaches an
// \includegraphics written literally anywhere in the string, including
// inside a \newcommand's replacement text -- that is just characters as
// far as it is concerned. What it declines rather than guess at is
// malformed input: no braces (`x\includegraphics y`) or unbalanced ones
// (`\includegraphics{oops`). Those reach the parser, where the vendored
// stub returns nullptr and draws nothing at all. This draws the argument
// instead, so a broken \includegraphics is visible rather than a silent
// gap. Verified reachable; see test-images.R.
//
// Idempotent, and registered for the life of the process:
// microtex_release() deliberately leaves the macro registry standing
// (see init.cpp), so this never needs re-running.
void register_image_macros();

}  // namespace microtex
