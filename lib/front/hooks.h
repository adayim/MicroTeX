#ifndef GRIDMICROTEX_FRONT_HOOKS_H
#define GRIDMICROTEX_FRONT_HOOKS_H

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace microtex::front {

/**
 * What the host makes of `\includegraphics[options]{path}`: the LaTeX that
 * stands for the image, read as a piece of input of its own. The engine
 * never reads a file. The host finds it -- in `dirs`, those the last
 * \graphicspath named, when it is not where the path says -- sizes it, and
 * returns the command that reserves its box (\gmgraphics, in the R
 * package). An empty result draws the file's name, as does having no
 * resolver at all. Whatever the host throws is not caught on the way out.
 *
 * It is called while the tree is lowered, so it sees an \includegraphics a
 * macro produced, and never one in a comment.
 */
using ImageResolver = std::function<std::string(
  const std::string& path, const std::string& options, const std::vector<std::string>& dirs)>;

/** The resolver for the parses that follow; an empty one draws names. */
void setImageResolver(ImageResolver resolver);

const ImageResolver& imageResolver();

/**
 * What the host makes of a font a document names -- `\setmainfont[options]{Name}`,
 * `\fontspec{Name}`, `\fontfamily{code}` -- as the host's own name for it: the
 * family text is drawn in (the host measures and draws with it), or, for the
 * `math` role, the name of a loaded math font. The engine reads no font file.
 *
 * `role` is `main`, `sans`, `mono`, `math`, `font` (a declaration for the rest
 * of its group) or `nfss` (`\fontfamily`, whose argument may be one of LaTeX's
 * family codes). `options` are the `key=value` pairs of fontspec's options
 * that the engine knows (Path, Extension, UprightFont, BoldFont, ItalicFont,
 * BoldItalicFont); a flag has an empty value. An empty result means the font
 * was not found, and the default is used. With no resolver the name stands for
 * itself. Whatever the host throws is not caught on the way out.
 *
 * It is called while the tree is lowered.
 */
using FontResolver = std::function<std::string(
  const std::string& name, const std::vector<std::pair<std::string, std::string>>& options,
  const std::string& role)>;

/** The resolver for the parses that follow; an empty one lets names stand. */
void setFontResolver(FontResolver resolver);

const FontResolver& fontResolver();


/** The size of a box, in ems of the font it is set in: how far it reaches right of its reference point, above the baseline and
 *  below it. */
struct BoxSize {
  double width = 0, height = 0, depth = 0;
};

/**
 * What the layout makes of a piece of LaTeX set on a line by itself (`text` as in \hbox{...}, or `$...$` for math): the size of
 * its box, for \settowidth and the like to put in a register while the input is read. The engine installs it for the
 * length of a parse (MicroTeX::parse), with that parse's fonts; outside one there is none and nothing can be measured.
 */
using BoxMeasurer = std::function<std::optional<BoxSize>(const std::string& latex)>;

void setBoxMeasurer(BoxMeasurer measurer);

const BoxMeasurer& boxMeasurer();

}  // namespace microtex::front

#endif
