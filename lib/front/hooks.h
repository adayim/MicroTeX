#ifndef GRIDMICROTEX_FRONT_HOOKS_H
#define GRIDMICROTEX_FRONT_HOOKS_H

#include <functional>
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

}  // namespace microtex::front

#endif
